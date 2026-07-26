# Prompt instance identity: multi-shot re-entrant shift

## Status

Implemented on the `solve-amb` branch. Successor to
[delimited-handler-frame-ownership.md](delimited-handler-frame-ownership.md),
whose plan was largely superseded by the fixes on the same branch.

The formerly failing shape, `reentrant-multishot` in
`tests/sweep/delimited_sweep.py`:

```koskari
(reset (+ (shift k (+ (k 1) (k 2))) (shift j (j 10))))   ;; was 11, now 23
```

The same limit capped a function-valued `amb` at its first outer choice;
the full product and `require`/`fail` pruning now work (sweep shapes
`reentrant-inner-abandon`, `reentrant-k-of-k`, `reentrant-amb-product`;
tests/reproducers/test_delimited_reentrant.py).

Findings from implementation amend the proposal below; the sections are
kept as proposed, with the deltas here:

- **Falsification happens once, at capture.** §1 proposed minting the
  fresh id at instantiation (per graft). Landed instead as a one-time
  rewrite of every prompt clone in the template inside
  `kos_shift_capture`: fresh node_id, KOS_DYNF_INVOCATION, and
  in-template SHIFT nodes remapped from each live id. Invokes then
  clone the template verbatim, so grafts of one k share the falsified
  ids. That degrades resolution within one k to nearest-with-that-id,
  which is safe by construction: grafts land at dyn_top and capture
  bounds at the nearest prompt, so a same-id instance can never sit
  between a SHIFT and its true delimiter (sweep shape
  `reentrant-nested-same-k`). "Every prompt clone" matters: the walk to
  the owning frame can pass heritage prompts (a reset inside an outer
  shift's handler) whose clones ride below the boundary as
  never-executed padding while their originals stay live. Cross-k and
  original-vs-graft ambiguity stay impossible since each capture mints
  its own ids. `kos_dynprompt_clone` stays id-preserving for the
  undelimited path, where INSTALL replaces the chain and the reinstated
  prompt is the same delimiter re-materialized.

- **The prompt records the answer, not the hint.** PUSH_PROMPT resolves
  its PROMPT extent once at push and stores the region's closing pc
  (`KosDynPrompt_POP_PC`); POP_SHIFT reads it and resumes at pop_pc + 1.
  This replaces both the lexical extent walk and the push-pc-keyed
  dynamic fallback that §3 mentions -- a stored answer cannot go stale
  the way a live acting pc does, so the whole resolution split
  disappears.
- **§5 as written was wrong: in-place does not survive untouched.** An
  inner shift during an in-place resume must capture up to the
  *invocation* boundary, not the original prompt -- the same trace
  argument as the graft case. The in-place invoke therefore installs a
  fresh, stackless invocation-prompt instance
  (`kos_dynprompt_new_invocation`) between the INPLACE delim and the
  slice. It owns no block (the live region's operands stay where they
  are), acts as the capture bound and POP_SHIFT resolution target inside
  the slice, and routes its completion through the delim below it.
- **An invocation-bounded capture always needs the compressed baseline.**
  Cloning stops at the invocation prompt, so the template loses the
  frame that executes the region's own bytecode. A plain return falling
  through the invocation prompt (return-at-frame case 3) must land in a
  copy of that frame and run the remaining region bytecode to
  POP_PROMPT -- otherwise the value "arrives" at the invoke site by
  accident while the grafted prompt and delim stay stranded on the
  chain (surfaced as "cannot unwind past barrier node" at the next
  loop CONTINUE). So the capture appends a clone of the first frame
  below the invocation prompt (code, namespace, mid-region pc) at the
  template bottom unconditionally, and -- when the live invocation
  prompt owns no block (the in-place marker) -- backfills the template's
  prompt clone with a deep copy of the region-bottom operands from the
  innermost stack owner below. This is the owner's compressed-frame
  stitch from the predecessor doc, landed as part of capture.

## The semantics being missed

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
undelimited on re-entry. The two operators genuinely differ on exactly our
failing shape; see Shan, *Shift to Control* (Scheme Workshop 2004), which
builds each from the other and pinpoints this as the distinction.

Traced on the failing shape ([INV] instrumentation, this branch): only two
invokes ever occur -- `(k 1)` in-place, `(j 10)` grafted. `(k 2)` is never
reached. The inner shift's POP_SHIFT unwinds to the **original** outer
prompt and jumps past the outer POP_PROMPT, discarding the remainder of
the outer handler body (`+ (k 2)`). The first invocation of `k` ran
undelimited -- `control` semantics where `shift` was promised.

`(reset (+ (shift k (k 1)) (shift j (j 10))))` returning 11 does not
contradict this: its handler body is exhausted after `(k 1)`, so
early termination is indistinguishable from correct termination there.

## Why the machinery cannot express the fix today

`compile/reset` emits `PUSH_PROMPT 0`; `compile/shift` emits
`PUSH_SHIFT 0` (`koskari/compile/specials/continuations.py`, both
hardcoded). Every prompt in every program shares id 0, so
`kos_find_prompt(es, prompt_id)` -- match id walking down from dyn_top --
degenerates to *nearest prompt*, and `kos_find_nearest_prompt` says
explicitly what the rest of the system does implicitly: prompts are
resolved by **position**.

Position-resolution is sound only under an invariant: **at most one
candidate prompt on the chain**. The proposal below (and the graft today)
violates that invariant by design -- an invocation prompt must coexist
with the live original. Nothing downstream can currently tell them apart.

This is the sixth instance of one session-spanning bug class, resolving a
relationship by position where identity was meant. For the record, since
the pattern is the real lesson:

| # | Site | Positional assumption | Fixed |
|---|------|----------------------|-------|
| 1 | POP_PROMPT / restitch | region operands distinguishable by depth | prompt owns its stack block (7003873c) |
| 2 | POP_SHIFT | enclosing PROMPT extent is in *this* code object | dynamic fallback via the prompt node (a63a77a5) |
| 3 | invoke/restitch | handler frame = nearest frame at-or-below prompt | frame owning the SHIFT; snapshot (969a2b1d) |
| 4 | dyn_delim clone | RETURN_NODE copyable verbatim | self-reference re-pointed (56f3fa2e) |
| 5 | inplace_ready | SHIFT→prompt stretch barrier-free | DELIM barrier (2ae55610) |
| 6 | prompt resolution itself | ≤1 candidate prompt on the chain | **this proposal** |

## Attempts that failed, and what each proved

Patches under `tmp/attempts/` (worktree, gitignored); numbered here in
order.

**A. Gate in-place to same-frame, route cross-frame to the graft.**
Sweep 27→22. Every cross-frame shape died with "POP_SHIFT: SHIFT extent
has no parent". Proved the graft is not a drop-in wherever a *live*
handler is suspended mid-body: the graft's clone competes with the live
SHIFT, and (then undiagnosed) POP_SHIFT could not resolve its prompt from
a callee. Both causes were later fixed separately, but the structural
lesson stands -- the graft serves re-runs of a *template*, not returns
into a *live* handler.

**B. Template-frame snapshot, first cut.** Same 22, same symptom. Proved
the failure was upstream of both edits: POP_SHIFT's prompt resolution
was static-only (extent tree of the current code object), so *any*
routing change that moved shapes onto cross-frame POP_SHIFT hit the same
wall. Fixing that (a63a77a5) plus the snapshot done right (969a2b1d) is
what took the sweep to 31.

**C. Force everything onto the graft** (probe, this final bug). Broke
even `(reset (+ (shift k (k 1)) (shift j (j 10))))` with "POP_SHIFT: top
is not a shift node". Proved (i) in-place is load-bearing, not an
optimization that can be retired; (ii) the graft cannot coexist with a
live prompt of the same id -- its cloned prompt makes two identical
candidates and nearest-match picks wrong. This is the direct evidence for
the invariant stated above.

**D. (Fixed en route, kept as design input.)** The INPLACE delim must
never be cloned into a captured template (2bbab2ae): it is transient
invoke bookkeeping, not region content. The proposal below preserves
this: invocation prompts are *created at invoke time*, never captured.

## Proposal

Owner's framing, adopted: **treat the nested invoke the same as the
post-reset invoke** -- stitch a fresh copy (compressed baseline frame +
segment) and continue evaluation, rather than resuming into the live
region. The graft already embodies this for the reset-unwound case; what
prevents reusing it while the original prompt is live is only that the
copy is indistinguishable from the original.

Therefore: give prompts **instance identity**, and resolve "the prompt
this SHIFT/k belongs to" by instance everywhere.

### 1. Identity

`KosDynNode.node_id` already exists, is unique per allocation, and is
*copied on clone for LCA matching*. That copy-on-clone is correct for
LCA and wrong for us -- a grafted prompt must be a **new instance**.

- Grafted/invocation prompts get a **fresh** node_id at instantiation.
  Confine the change to prompt nodes in the graft path (a
  `kos_dynprompt_clone_fresh` used by continuation instantiation);
  leave kos_dyn_clone's id-preserving behaviour for every other kind and
  for LCA use.
- The static `prompt_id` (constant-pool, today always 0) stays what it
  is: a *tag namespace*, needed only if named/tagged prompts arrive
  later. Instance identity is node_id, not a reworked prompt_id. No
  compiler or bytecode change.

### 2. Capture binds to an instance

`kos_shift_capture` already records `LIVE_PROMPT_NODE` +
`LIVE_PROMPT_NODE_ID`. Extend `PUSH_SHIFT`'s capture so the SHIFT *node*
also records the instance id of the prompt it captured against
(`shift.u.shift.prompt_node_id`, mirroring what the continuation struct
already holds). Everything that later asks "which reset does this shift
belong to" reads that, not the chain top.

### 3. POP_SHIFT resolves by instance

`kos_find_nearest_prompt(es)` in POP_SHIFT becomes: walk down from
dyn_top for `IS_PROMPT(node) && NODE_ID(node) == shift's recorded id`.
Two prompts on one chain stop being ambiguous. The existing dynamic
fallback for the resume pc (via `KosDynPrompt_PUSH_PC`, a63a77a5) is
already instance-based once the right node is found.

### 4. Invocation prompts route to their delim, not to bytecode

The blocker identified before this proposal: POP_SHIFT computes its
resume as `prompt_pop_pc + 1`, i.e. "jump past the POP_PROMPT
instruction". An invocation prompt is created by `k`'s invoke, not by a
PUSH_PROMPT instruction; there is no POP_PROMPT in any code object for
it. Resolution:

- The graft already installs a POP_PROMPT-mode delim bookmark holding
  return frame/pc; POP_PROMPT routes through it. Mark the grafted
  prompt (a `KOS_DYNF_INVOCATION` flag bit, or equivalently "created by
  clone_fresh") and pair it with its delim.
- POP_SHIFT, on resolving its prompt to an *invocation* prompt: build
  the same pop-shift unwind plan, but the resume target is the paired
  delim's return frame/pc -- the invoke site -- rather than
  `prompt_pop_pc + 1`. This is the one genuinely new branch in the
  system. Everything it needs (delim bookmark, unwind plan machinery,
  KOS_XFER_EVENT_SHIFT walk) already exists; the branch only selects a
  different resume target.

An abandoning inner shift (body never invokes its k) then naturally
yields its body's value *to the outer invoke site* -- `(k 1)` evaluates
to the inner shift-body's value -- which is exactly Danvy/Filinski.

### 5. In-place stays, as the uncontended fast path

Unchanged in behaviour: first invoke, no barrier, no delim between SHIFT
and prompt (2ae55610's check). In-place resumes into the live region and
its inner shifts capture against the live prompt -- correct there,
because with no second candidate the live prompt *is* the right
instance. All contended cases (re-invoke, re-entrant second capture,
post-unwind) go through the graft, which after (1)-(4) is finally safe
in the presence of the live original.

The multi-shot fix then falls out with no further mechanism: `(k 1)`
in-place; the inner `(shift j ...)` fires, its delim-barrier check
routes...; `(k 2)` has LIVE_SLICE_CONSUMED set, grafts a fresh prompt
instance, runs the copy, its inner shift captures against the copy and
returns to the `(k 2)` site; the outer handler completes; 23.

### Published-implementation cross-check

The design matches how the literature says this must behave, adapted to
the dynchain:

- Danvy & Filinski, *Abstracting Control* (LFP 1990): the reified k is
  `λx.⟨E[x]⟩`; §4 above is the ⟨·⟩.
- Shan, *Shift to Control* (2004): shift/control differ exactly in
  whether re-entry re-delimits; today's behaviour is control-flavoured
  by accident.
- Dybvig, Peyton Jones & Sabry, *A Monadic Framework for Delimited
  Continuations* (JFP 2007): prompts are **first-class marks with
  individual identity** pushed per-use onto the meta-continuation;
  capture *removes* the segment up to the mark, re-instatement *pushes a
  copy back with the mark*. Their pushPrompt/withSubCont pair is
  §1+§4 of this proposal in monadic clothing, and is the standard answer
  to "how do two activations of one reset coexist".
- Kiselyov's caml-shift (delimcc) implements the same via unique prompt
  values compared by identity; Racket's full implementation
  (Flatt/Yu/Findler/Felleisen, ICFP 2007) likewise keys on prompt tags
  and re-instates delimiters on jump. Guile and Wasm stack-switching
  follow the same tagged-prompt shape.

The recurring element across all of them: **the delimiter re-appears on
every re-entry, and delimiters are compared by identity, never by
position.** No published shift implementation resolves "my prompt" as
"the nearest prompt".

### dynnode changes, summarized

| Piece | Change | Size |
|-------|--------|------|
| prompt clone for graft | fresh node_id variant (`clone_fresh`), invocation flag | small; graft path only |
| SHIFT node | record captured prompt's node_id | one field in the shift union arm + one store in capture |
| POP_SHIFT | resolve prompt by recorded instance id; invocation-prompt branch resumes to paired delim | the substantive edit |
| kos_find_nearest_prompt | superseded by instance lookup (keep for undelimited/other callers if any) | small |
| everything else | none -- prompt_id stays 0, no compiler change, no new opcode | -- |

### Verification

- `reentrant-multishot` → 23 (the red sweep case).
- `tmp/amb_delimited.ki`: t2 `[3, 4]`, t3 six pairs, t4 the three
  triples -- `require`/`fail` and multiple choice points are the real
  acceptance test.
- Full sweep (34) green; suite; valgrind (`make test-valgrind`) -- the
  fresh-instance graft allocates per re-invoke, same class the last
  valgrind pass cleared.
- New sweep shapes: inner shift **abandoning** inside an invoked k
  (value delivered to the invoke site, outer handler continues);
  three-deep `(k (k (k 1)))` re-entrancy; multi-shot where the *inner*
  shift is also multi-shot (the amb×amb product case, t3).

## Risks

- **kos_compute_lca** relies on node_id equality across clones; auditing
  that fresh-id prompts never participate in an LCA match they were
  depended on for is the main correctness risk of §1.
- The invocation-prompt branch in POP_SHIFT touches the subsystem's
  hottest invariant (resume pc selection); the sweep's one-shape-per-
  subprocess isolation is the guard that has caught every regression so
  far and must be run per phase, not at the end.
- `LIVE_SLICE_CONSUMED` currently flips on first in-place invoke; with
  re-invokes grafting fresh instances, confirm consumed-ness is per-k
  (it is, on the continuation struct) and that clones for multi-shot
  reset it (kos_continuation_clone already does).

# The end.
