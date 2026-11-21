# Prompt instance identity: multi-shot re-entrant shift

## Status

Implemented. Successor to
[delimited-handler-frame-ownership.md](delimited-handler-frame-ownership.md).

The behaviour this design guarantees, in its minimal shape
(`reentrant-multishot` in `tests/sweep/delimited_sweep.py`):

```koskari
(reset (+ (shift k (+ (k 1) (k 2))) (shift j (j 10))))   ;; 23
```

The same mechanism is what lets a function-valued `amb` compose: the
full choice product across nested `amb`s, with `require`/`fail`
pruning. Guarding shapes: sweep cases `reentrant-inner-abandon`,
`reentrant-k-of-k`, `reentrant-nested-same-k`, `reentrant-amb-product`,
and tests/reproducers/test_delimited_reentrant.py.

## The semantics

Danvy/Filinski `shift` reifies the continuation **with its own delimiter**:

```
⟨E[shift k. M]⟩ → ⟨M[k := λx. ⟨E[x]⟩]⟩
                              ^^^^^^
```

`k` is not `λx. E[x]` -- it is `λx. ⟨E[x]⟩`. Every invocation of `k` runs
the captured region under a **fresh prompt**, so a shift *inside* that run
captures up to the fresh prompt, terminates only that invocation, and
yields its value to the invoke site. This is precisely what distinguishes
`shift` from Felleisen's `control` (`F`), whose reified continuation is
undelimited on re-entry; see Shan, *Shift to Control* (Scheme Workshop
2004), which builds each from the other and pinpoints this as the
distinction.

Without re-delimitation, the minimal shape above degrades to `control`
semantics: an inner shift firing during an invocation of `k` unwinds to
the *original* outer prompt, jumps past the outer POP_PROMPT, and
discards the remainder of the suspended outer handler (`+ (k 2)` is
never reached; the reset yields 11). The sibling shape
`(reset (+ (shift k (k 1)) (shift j (j 10))))` yields 11 either way,
because its handler body is exhausted after `(k 1)` -- early termination
is indistinguishable from correct termination there, which is how the
defect stayed hidden.

## Why identity, not position

`compile/reset` emits `PUSH_PROMPT 0`; `compile/shift` emits
`PUSH_SHIFT 0` (`koskari/compile/specials/continuations.py`, both
hardcoded). Every prompt in every program shares tag 0, so tag matching
degenerates to *nearest prompt*: resolution by **position**.

Position-resolution is sound only under an invariant: **at most one
candidate prompt on the chain**. Re-delimitation violates that
invariant by design -- an invocation's delimiter instance must coexist
with the live original -- so everything that asks "which reset" must
key on instance identity instead. Positional assumptions in this
subsystem produced a whole family of defects beyond the headline one;
for the record, since the pattern is the durable lesson:

| # | Site | Positional assumption | Resolution |
|---|------|----------------------|-----------|
| 1 | POP_PROMPT / restitch | region operands distinguishable by depth | prompt owns its region's stack block |
| 2 | POP_SHIFT | enclosing PROMPT extent is in *this* code object | closing pc recorded on the node at push |
| 3 | invoke/restitch | handler frame = nearest frame at-or-below prompt | frame owning the SHIFT; snapshot |
| 4 | dyn_delim clone | RETURN_NODE copyable verbatim | self-reference re-pointed at the clone |
| 5 | inplace_ready | SHIFT→prompt stretch barrier-free | DELIM barrier check |
| 6 | prompt resolution itself | ≤1 candidate prompt on the chain | instance identity (this design) |

## Rejected approaches, and what each proved

**A. Gate in-place to same-frame, route cross-frame to the graft.**
Every cross-frame shape died with "POP_SHIFT: SHIFT extent has no
parent". The structural lesson: the graft serves re-runs of a
*template*, not returns into a *live* handler -- its clone competes
with the live SHIFT.

**B. Reroute without instance identity.** Any routing change that moved
shapes onto cross-frame POP_SHIFT hit the same wall, because POP_SHIFT's
prompt resolution was static-only (the extent tree of the current code
object). Resolution has to be a property of the prompt *node*, not of
whichever code object happens to be executing.

**C. Force everything onto the graft.** Broke even
`(reset (+ (shift k (k 1)) (shift j (j 10))))` with "POP_SHIFT: top is
not a shift node". Proved (i) in-place is load-bearing, not an
optimization that can be retired; (ii) a graft cannot coexist with a
live prompt it is indistinguishable from -- two identical candidates,
and nearest-match picks wrong. This is the direct evidence for the
invariant stated above.

**D. Cloned INPLACE delims.** An INPLACE delim must never be cloned
into a captured template: it is transient invoke bookkeeping, not
region content. Capture links around them; invocation delimiters are
created at invoke time, never captured.

## The design

The nested invoke is treated the same as the post-reset invoke: stitch
a fresh copy (compressed baseline frame + segment) and continue
evaluation, rather than resuming into the live region. What makes that
safe in the presence of the live original is that prompts carry
**instance identity**, and everything that resolves "the prompt this
SHIFT/k belongs to" keys on instance.

### 1. Identity: falsified once, at capture

`KosDynNode.node_id` is unique per allocation and *copied on clone* for
LCA matching. That copy-on-clone is correct for LCA and for the
undelimited (call/cc) path -- there, INSTALL replaces the whole chain,
so a reinstated prompt clone is the same delimiter re-materialized and
never coexists with its original.

A delimited capture template is different: its instantiations coexist
with the live original. So `kos_shift_capture` **falsifies** every
prompt clone in the template, once: a freshly minted node_id,
`KOS_DYNF_INVOCATION`, and any in-template SHIFT node recording the
corresponding live id remapped to the falsified one. Invokes then clone
the template verbatim (node_id and flags preserved), so the invoke path
never touches prompt identity.

Two consequences of the once-at-capture placement:

- Grafts of one k share the falsified ids, degrading resolution within
  a single k to nearest-with-that-id. That is safe by construction:
  grafts attach at dyn_top and capture bounds at the nearest prompt, so a
  same-id instance can never sit between a SHIFT and its true delimiter
  (guarded by `reentrant-nested-same-k`).
- "Every prompt clone" matters, not just the boundary: the capture's
  walk to the owning frame can pass heritage prompts (a reset inside an
  outer shift's handler), whose clones ride below the boundary as
  never-executed padding while their originals stay live.

The static `prompt_id` (constant-pool, always 0) stays what it is: a
*tag namespace*, needed only if named/tagged prompts arrive later.
Instance identity is node_id. No compiler or bytecode change.

### 2. Capture binds to an instance

The SHIFT node records the instance id of the prompt it captured
against (`KosDynShift_PROMPT_NODE_ID`, mirroring the continuation's
`LIVE_PROMPT_NODE_ID`). Everything that later asks "which reset does
this shift belong to" reads that, never the chain top.

### 3. POP_SHIFT resolves by instance; the prompt records its exit

POP_SHIFT walks down from the SHIFT for
`IS_PROMPT(node) && NODE_ID(node) == recorded id`. Two prompts on one
chain are never ambiguous.

For a live reset, the resume target is the instruction after the
region's POP_PROMPT. PUSH_PROMPT resolves its PROMPT extent once, at
push, and stores the closing pc on the node (`KosDynPrompt_POP_PC`);
POP_SHIFT reads the answer and resumes at `pop_pc + 1`. This is the
same push-time pattern the loop forms use for their jump targets -- the
extent table is the fast lookup, consulted only at the push site. A
stored answer cannot go stale the way a live acting pc does, so no
lexical/dynamic resolution split exists at the pop site.

### 4. Invocation prompts route to their delim, not to bytecode

An invocation prompt is created by `k`'s invoke, not by a PUSH_PROMPT
instruction; no POP_PROMPT anywhere pairs with it. Its completion --
whether the region runs out normally (POP_PROMPT's bookmark walk passes
invocation prompts) or an inner shift's handler finishes (POP_SHIFT's
invocation branch) -- routes through the paired DELIM bookmark below
it, delivering the value at the invoke site via
`kos_eval_route_delim_return`.

An abandoning inner shift (body never invokes its k) thereby yields its
body's value *to the invoke site* -- `(k 1)` evaluates to the inner
shift-body's value -- which is exactly Danvy/Filinski.

### 5. In-place re-delimits with a marker instance

The in-place path (first invoke, no barrier or delim between SHIFT and
prompt) resumes into the live region rather than a copy, and it
re-delimits like every other invocation: a fresh, stackless
invocation-prompt instance (`kos_dynprompt_new_invocation`) sits
between the INPLACE delim and the slice. It owns no block -- the live
region's operands stay where they are -- and serves purely as the
capture bound and POP_SHIFT resolution target for shifts inside the
slice, routing its completion through the delim below it. All contended
cases (re-invoke, re-entrant second capture, post-unwind) go through
the graft, which instance identity makes safe alongside the live
original.

### 6. Invocation-bounded captures carry the compressed baseline

A capture bounded at an invocation prompt must not clone below it (the
pairing delim and the outer chain are not region content), which costs
the template the frame that executes the region's own bytecode. A plain
return falling through the invocation prompt (return-at-frame case 3)
must arrive in a copy of that frame and run the remaining region bytecode
to POP_PROMPT; without it, the value arrives at the invoke site by
accident while the grafted prompt and delim sit stranded on the chain
(surfacing as "cannot unwind past barrier node" at the next loop
CONTINUE).

So the capture appends a clone of the first frame below the invocation
prompt -- code, namespace, mid-region pc -- at the template bottom
unconditionally, and, when the live invocation prompt owns no block
(the in-place marker), backfills the template's prompt clone with a
deep copy of the region-bottom operands from the innermost stack owner
below. This is the compressed-frame stitch from the predecessor doc,
done at capture.

### Published-implementation cross-check

The design matches how the literature says this must behave, adapted to
the dynchain:

- Danvy & Filinski, *Abstracting Control* (LFP 1990): the reified k is
  `λx.⟨E[x]⟩`; §5's marker instance and the graft's falsified instance
  are the ⟨·⟩.
- Shan, *Shift to Control* (2004): shift/control differ exactly in
  whether re-entry re-delimits; positional resolution is
  control-flavoured by accident.
- Dybvig, Peyton Jones & Sabry, *A Monadic Framework for Delimited
  Continuations* (JFP 2007): prompts are **first-class marks with
  individual identity** pushed per-use onto the meta-continuation;
  capture *removes* the segment up to the mark, re-instatement *pushes a
  copy back with the mark*. Their pushPrompt/withSubCont pair is
  §1+§4 in monadic clothing, and is the standard answer to "how do two
  activations of one reset coexist".
- Kiselyov's caml-shift (delimcc) implements the same via unique prompt
  values compared by identity; Racket's full implementation
  (Flatt/Yu/Findler/Felleisen, ICFP 2007) likewise keys on prompt tags
  and re-instates delimiters on jump. Guile and Wasm stack-switching
  follow the same tagged-prompt shape.

The recurring element across all of them: **the delimiter re-appears on
every re-entry, and delimiters are compared by identity, never by
position.** No published shift implementation resolves "my prompt" as
"the nearest prompt".

## Guarding tests

- `tests/sweep/delimited_sweep.py`, one subprocess per shape (a
  dynchain fault corrupts whatever runs next in-process); the
  `reentrant` group carries the shapes specific to this design,
  including the amb×amb product and same-k nesting.
- `tests/reproducers/test_delimited_reentrant.py` pins the semantics in
  the phased suite: multi-shot with a second shift, inner abandonment,
  `(k (k (k 0)))`, the amb product, and `require`/`fail` pruning.

## Invariants to watch

- **kos_compute_lca** relies on node_id equality across clones;
  falsified prompts must never participate in an LCA match they were
  depended on for. Delimited captures pass graft_point_id 0 and the
  undelimited path is untouched, which is what keeps this true.
- The invocation-prompt routing sits on the subsystem's hottest
  invariant (resume target selection); the sweep's one-shape-per-
  subprocess isolation is the guard, and belongs in any verification
  pass over this code.
- `LIVE_SLICE_CONSUMED` flips on the first in-place invoke and is
  per-continuation; `kos_continuation_clone` must keep copying it so
  multi-shot re-invokes stay on the graft.

# The end.
