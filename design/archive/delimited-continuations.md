# Delimited and undelimited continuations: redesign

## Status

Proposal. Read against `further-fixes` at 48181db8. Replaces the
delimited half of [callcc.md](callcc.md). Retires
[archive/prompt-instance-identity.md](../archive/prompt-instance-identity.md),
[archive/delimited-handler-frame-ownership.md](../archive/delimited-handler-frame-ownership.md)
and Path A of
[archive/delimited-invoke-model.md](../archive/delimited-invoke-model.md).
Supersedes the `RETURN` frame-only proposal (return-return-shift.md,
removed): a transfer across a continuation boundary is an error, so
there is no `return-shift` form.
The user contract it changes is
[compile-shift](../../docs/reference/specials/compile-shift.md) and
[compile-reset](../../docs/reference/specials/compile-reset.md).

## Diagnosis

One choice explains most of the machinery: a shift handler body runs on
top of the live region it has just captured. `PUSH_SHIFT` clones the
segment, pushes a SHIFT node on dyn_top, and runs the handler above the
region; `POP_SHIFT` aborts to the prompt only when the handler finishes
(opcode/PUSH_SHIFT.c, opcode/POP_SHIFT.c). In Danvy and Filinski's
reduction the handler runs with the region already removed:

```
<E[shift k. M]>  ->  <M[k := \x. <E[x]>]>
```

Applying k inside the handler therefore meets a live copy of the same
region, and attaching a clone there runs the region's winds twice, nested.
Path A (xfer_plan.c:kos_delimited_invoke_inplace) avoids that by
resuming the live region in place, which needs the INPLACE delim, the
stashed span, the handler snapshot, the saved pc and resume fields, the
restitch (dynchain.c:kos_dyn_inplace_restitch), invocation prompts,
KOS_DYNF_INVOCATION and per-instance prompt ids, since a live prompt and
its clone now coexist. The RETURN_FRAME_NODE remaps and the
falsification pass keep pointers and ids coherent across clones of that
structure.

The proposal removes the cause: a shift aborts to its prompt before the
handler runs, as every published shift does. With the live region gone
there is one way to apply a continuation, a prompt is found by tag,
nearest first, and nothing needs instance identity.

## 1. The model

**Tags.** A prompt tag is any Python object, compared by identity, as
Racket compares prompt tags with `eq?` and as Common Lisp compares catch
tags. The default tag, the one `reset` and `shift` use when none is
given, is None: a prompt for it holds a NULL tag, None is never stored,
and there is no default tag object. A fresh opaque tag is a gensym, so
a library delimiter (koskari.solve) cannot be intercepted by a user's
`reset`; there is no tag type.

**Prompt.** `(reset-at TAG BODY...)` installs a delimiter for TAG and
runs BODY under it (Racket `call-with-continuation-prompt`, DPJS
`pushPrompt`). Its value is BODY's value.

**Shift.** `(shift-at TAG K BODY...)` finds the nearest prompt for TAG
on the chain, crossing prompts of other tags, which become part of the
captured segment. It captures the segment from the shift point up to
and including that prompt as a template, then aborts: every node above
the prompt is left, as a raise would leave it. BODY then runs directly
on the prompt, with K bound, and its value is the value of the prompt.
This is racket/control's `shift-at`: `call-with-composable-continuation`
to get k, then an abort that keeps the prompt and runs the body there.

**Application.** Applying k with v pushes a return bookmark (a DELIM)
at the application site and attaches a fresh copy of the template on it:
the copy of the prompt is the re-delimiting `<.>` of `\x. <E[x]>`.
Execution resumes at the shift's resume point with v. When the copied
region finishes, its value is delivered at the application site. Each
application makes its own copy, so k is multi-shot.

**Extents.** Following Flatt, Yu, Findler and Felleisen (ICFP 2007):

- The abort leaves the region's extents innermost first: WIND after
  thunks run, single-shot WITH `__exit__` runs, a FINALLY body runs, a
  PARAM restores, a TRY is dropped (the existing goal LEAVING with
  KOS_XFER_EVENT_TRANSFER, xfer_goal.c:goal_leave).
- An application enters the copy's extents outermost first: WIND before
  thunks, PARAM rebinding, multi-shot WITH re-entry
  (xfer_goal.c:goal_enter). Single-shot WITHs are not re-entered; their
  shared exit-done record stays as today.
- The application site's extents are neither left nor re-entered. They
  stay below the DELIM; the copy's parameters are consulted first and
  the application site's below them.
- Extents outside the prompt are not part of k. A PARAM, WIND or TRY in
  the reset's own frame but outside the reset is not captured (today it
  is, as padding; see section 3).
- A raise in an installed copy propagates through the DELIM to the
  application site. A `return`, `break`, `continue` or `yield` in a
  installed copy does not cross the DELIM (Transfers, below).

The handler body runs outside the region's extents. Exceptions it
raises are not caught by a TRY inside the region; parameters bound
inside the region are not visible to it; a FINALLY inside the region
runs before the handler, not after.

**Transfers.** A `return`, `break`, `continue` or `yield` whose target
lies across a SHIFT or a DELIM is an error. No transfer takes a second
meaning at a continuation boundary.

| Transfer | Where | Result |
|---|---|---|
| `return` | the frame sitting directly on a DELIM (an installed copy's bottom frame) | error |
| `return` | a callee handler's frame copy on a SHIFT | error |
| `return` | a handler in the reset's own function; a callee frame inside a copy | ordinary return |
| `break`, `continue` | the loop search stops at a SHIFT and at a DELIM | "outside loop" unless the loop is inside the handler or inside the copy |
| `yield` | the generator search stops at a DELIM | error |
| `yield` | a handler in the reset's own generator; a generator defined in the handler | suspends that generator |
| `raise` | anywhere | propagates from the prompt, or through the DELIM |

- `return`. An installed copy's bottom frame is a copy of the prompt's
  owning frame. Its function boundary is outside the prompt, so outside
  k. A callee handler's frame copy has a caller that was in the region
  and has been left. The handler's frame copy records the fact on
  itself (KOS_FRAME_SHIFT_HANDLER, set when the abort makes it). Its
  parent being a SHIFT does not identify it: every function the handler
  calls sits directly on the SHIFT too, and returns to the handler as
  usual. The copy's bottom frame is recognised by its parent being a
  DELIM, and there the parent test is sound: the copy is attached as the
  application pushes the DELIM, and a thunk the attachment runs sits above
  the copy, so nothing else is ever a frame directly on a DELIM.
- `break` and `continue`. The compiler does not resolve a loop; BREAK
  and CONTINUE take the nearest enabled LOOP or FOREACH at run time
  (dyn_walk.c:kos_dyn_find_loop). After the abort a loop inside the
  region is gone, and a search from the handler would find the next
  loop out. The search stops at the SHIFT. A handler's `break` therefore
  does not reach a loop around the reset either, though that loop is
  live: the search cannot tell it from the next loop out of one the
  abort left.
- `yield`. A capture across a generator boundary is refused, so a
  handler always runs in its own generator's activation and yields as a
  `begin` at the reset would. An installed copy is different: its `yield`
  would suspend whichever generator is nearest below the application
  site, or none. The generator search stops at the DELIM.

Both of these work:

```
def foo []:
  reset:
    + 100:
      shift k:
        yield (k 1)
        yield (k 2)
```

`foo` yields 101 then 102: each copy finishes and delivers to the
handler before the handler yields.

```
def foo []:
  reset:
    + 100:
      shift k:
        def bar []:
          yield (k 1)
          yield (k 2)
        list (bar)
```

Each application composes on `bar`'s chain and completes before `bar`
yields; the reset's value is `[101, 102]`.

This one is refused wherever k is applied:

```
def foo []:
  reset:
    + (shift k: ...) (yield 5)
```

**Mutable state.** A copy shares namespaces with the template and with
every other copy (dyn_frame.c:kos_dynframe_clone takes a reference to
the same namespace) and gets its own operand stacks. Variables are
shared across shots, as Scheme locations are; pending operands are not.

**call/cc.** Captures from the CC block to the root of the context
(dynchain.c:kos_capture_cc). Application rebuilds the node stack: leave
the live chain down to the lowest common ancestor with the template,
put the captured operands back on the shared nodes, attach a copy of the
rest, enter it (xfer_plan.c:kos_chain_common_ancestor, xfer_goal.c).
This is the Scheme dynamic-wind algorithm, with "same winder" read as
"same id and same phase" because attached nodes are copies. A PROMPT in a
call/cc template is attached as a live reset again. This path is unchanged
except for the deletions in section 6.

**From Python.** call/cc keeps the upward KoskariContinuationApply
transport (cont.c:call_call_continuation). A delimited continuation
called from Python stays refused (cont.c:delimited_continuation_call):
application composes onto the applier's chain, and a Python caller has
none.

## 2. Identity

There are no prompt instance ids. A prompt is found by tag, nearest
first, walking parents from dyn_top. That suffices because a shift's
handler runs directly on its prompt (POP_SHIFT's prompt is the SHIFT's
parent by construction), an application is attached on top of the chain (a
shift inside the copy meets the copy's prompt first), and no node is
ever moved under a live copy of itself. DPJS and delimcc work this way:
`withSubCont p` takes the segment up to the nearest mark for p, and two
marks for p coexist without being told apart.

**Return point.** A DELIM's return frame is the first FRAME below it,
found at delivery and never stored. The DELIM keeps RETURN_PC, a value
that cannot go stale across a clone.

**Ids on copies.** `kos_dyn_clone_segment` gains a flag. Delimited
captures and delimited applications clone with id 0, so every node of a
installed copy is a new instance, minted on first read by kos_dynnode_id.
call/cc captures and applications keep the source id, because three readers
need an installed call/cc copy to count as the same extent as its source:
kos_chain_common_ancestor (the seam), kos_xfer_goal_capture_restore
(the template must mirror the chain below the seam), and
test_attached_clone_counts_as_shared, which asserts Scheme's rule that
re-entering through the same continuation does not re-run shared winds.

The node id stays. Its remaining readers are the call/cc seam and
restore records above; the ABA checks on borrowed pointers in goals
(xfer_goal.c: seam_id, frontier_id in goal_leave and goal_attach); the
traceback's per-frame record (eval.c:dynchain_record_has_frame); and
snapshots (dynchain.c:dynnode_to_dict_generic, tests/lib/test_domain.py).

## 3. The chain mapping

| Item | Today | Decision |
|---|---|---|
| PROMPT `prompt_id` (uint32) | tag number | becomes `tag` (PyObject *, owned; NULL is the default tag) |
| PROMPT `pop_pc` | POP_PROMPT of the region | keep |
| PROMPT block | region operands | keep; the shift's abort empties it (those operands are in the template) |
| KOS_DYNF_INVOCATION | marks prompts with no POP_PROMPT | delete; "applied" is structural: the prompt's parent is a FRAME whose parent is a DELIM |
| kos_dynprompt_new_invocation | in-place re-delimiter | delete |
| SHIFT `prompt_node_id` | instance it captured against | delete; the prompt is the SHIFT's parent |
| SHIFT block | handler operands | keep |
| DELIM `mode` (POP_PROMPT, INPLACE) | two return disciplines | delete; one meaning |
| DELIM RETURN_CHAIN | stashed span | delete |
| DELIM RETURN_FRAME_NODE | cached first frame below | delete; derived |
| DELIM RETURN_NODE | always the delim itself (xfer_plan.c:199, 427) | delete |
| DELIM RETURN_PC | application pc | keep |
| DELIM SAVED_PC, SAVED_ACTING_PC, SAVED_RESUME_* | Path A displacement | delete |
| Heritage padding below the prompt | captured, entered on application | delete; template bottom is one frame copy |
| Baseline frame (invocation-bounded captures only) | compressed region bottom | generalize to every delimited capture |
| KosContinuation `prompt_id` | tag, or UNDELIMITED | replace with a `delimited` bit |
| KosContinuation `one_shot`, `resume_stack` | written, never read | delete |
| KosContinuation `live_*`, `live_slice_consumed` | Path A detection | delete |

The template of a delimited capture is the segment from the shift point
through the prompt, on one copy of the prompt's owning frame: same code
and pc, the namespace live at the prompt (as kos_shift_capture's
baseline_ns does today), an empty operand stack (the region's operands
are on the prompt's block). Nodes between the owning frame and the
prompt are not captured: an NS there is folded into the frame copy's
namespace, and a WIND, PARAM, TRY or LOOP there is outside the reset.

Diagrams run root at the left, top at the right. `[..]` is a node's
operand block.

**(a) `(reset (+ 1 (shift k (k 5))))`, k applied once, all in frame F.**

```
at PUSH_SHIFT:     F[..] <- P(d)[1]
template:          F'(ns@P, pc) <- P'(d)[1]          resume: after POP_SHIFT
after the abort:   F[..] <- P(d)[] <- S[k]           handler runs in F
(k 5):             F <- P(d) <- S[k] <- D(pc) <- F'' <- P''(d)[1 5]
P'' POP_PROMPT:    P''.parent F'' is a frame on D: applied; 6 goes to the first
                   block below D (S), F.pc = D.pc
POP_SHIFT:         P is S's parent, not applied: leave to F, push 6 at pop_pc+1
```

**(b) the same with `(+ (k 5) (k 10))`.** The second application attaches
an unrelated copy; the first is gone.

```
second (k 10):     F <- P(d) <- S[6] <- D(pc) <- F''' <- P'''(d)[1 10]
result:            S[6 11] -> 17 -> POP_SHIFT -> 17
```

**(c) Nested tags, outer shift crossing an inner prompt, one frame.**
`(reset-at A (+ 100 (reset-at B (+ 10 (shift-at A k (k 1))))))`

```
at PUSH_SHIFT:     F <- P(A)[100] <- P(B)[10]        walk crosses P(B), stops at P(A)
template:          F'(ns@P(A)) <- P'(A)[100] <- P'(B)[10]
after the abort:   F <- P(A)[] <- S[k]               P(B) was left
(k 1):             F <- P(A) <- S <- D <- F'' <- P''(A)[100] <- P''(B)[10 1]
P''(B) POP_PROMPT: parent is P''(A), not a frame: plain pop, 11 onto P''(A)
P''(A) POP_PROMPT: parent F'' on D: applied; 111 to S
POP_SHIFT:         111 is the value of reset-at A
```

DPJS gives the same: k = `\v. <A 100 + <B 10 + v>>`, `(k 1)` = 111.

**(d) call/cc inside a reset body, applied after the reset returned.**

```
(+ 1000 (reset (+ 1 (let/cc c (setq saved c) 1))))   ; then later in F: (saved 10)

at CAPTURE_CC:     R .. F[1000] <- P(d)[1] <- CC
template (ids kept): R' .. F'[1000] <- P'(d)[1]     resume: after POP_CC
at (saved 10):     R .. F[..]                       P is gone
seam (LCA):        F, if still live and in the same phase
attaching:         restore F's block from F' ([1000]), splice P'(d)[1] on F
P' POP_PROMPT:     parent F is not on a DELIM: plain pop; F continues past
                   the reset with 1000 + 11
```

No DELIM is involved. A call/cc captured inside a delimited application
captures that application's DELIM; applying it again re-creates a DELIM whose
return frame is derived from where it now sits, which removes the bug
class behind kos_dyn_remap_delim_frames.

## 4. Capture and application

**Capture** (replaces dynchain.c:kos_shift_capture, about 230 lines, and
the baseline half of kos_continuation_capture_segment):

```c
int kos_shift_capture(KosContext *ctx, PyObject *tag,
        KosOp *resume_pc, KosContinuation **out) {
  KosDynNode *p;
  for (p = DYN_TOP(ctx); p; p = p->parent) {
    if (kos_dyn_handle(ctx, p, SHIFT) == ERROR)   /* GEN refuses */
      return kos_capture_note(tag, "shift"), -1;
    if (IS_PROMPT(p) && PROMPT_TAG(p) == tag)
      break;
  }
  if (! p)
    return kos_raise_prompt_not_found(tag);
  owner = first FRAME at or below p;
  seg = kos_dyn_clone_segment(DYN_TOP(ctx), p->parent, CLONE_FRESH_IDS);
  base = kos_dynframe_clone_bare(owner, p->ns);   /* code, pc, empty stack */
  bottom_of(seg)->parent = base;
  *out = kos_continuation_create(seg, resume_pc, /* delimited */ 1, NULL);
}
```

No remap, no falsification, no backfill.

**Shift** (PUSH_SHIFT; the abort reuses the goal machinery POP_SHIFT
already uses):

```c
tag = COUNT(insn) ? pop() : NULL;                 /* set aside before capture */
kos_shift_capture(ctx, tag, kos_shift_ic_for(...), &k);
shift = kos_dynshift_new(); shift->ns = live ns;  target = shift;
if (FRAME(ctx) is above p) {                      /* shift ran in a callee */
  h = kos_dynframe_clone_bare(FRAME(ctx), live ns);
  h->parent = shift;  target = h;
}
goal = kos_xfer_goal_new(target, p, wrapper(k), insn + 1, NULL, 0);
```

LEAVING unlinks everything above p; ATTACHING empties p's block, splices
the target on p and sets the pc; DELIVERING pushes k on the SHIFT's
block, or on h's. A handler written in a callee runs in a copy of the
callee's frame on the SHIFT, because the callee's own activation was
part of the region and has been left.

**POP_SHIFT:**

```c
top = DYN_TOP(ctx);
s = IS_SHIFT(top) ? top : (IS_FRAME(top) && IS_SHIFT(top->parent)) ? top->parent : NULL;
result = pop();  p = s->parent;
if (kos_prompt_is_applied(p))                      /* p->parent frame on a DELIM */
  return kos_eval_route_delim_return(ctx, p->parent->parent, result);
goal = kos_xfer_goal_new(NULL, p->parent, result, POP_PC(p) + 1, NULL, 0);
```

A `return` in h raises: h is marked as a handler frame (Transfer
checks, below).

**POP_PROMPT:** top is the prompt. If kos_prompt_is_applied, route to the
DELIM; otherwise pop as today. The walk past INVOCATION prompts goes.

**Application** (kos_invoke_continuation, delimited branch):

```c
check_frame_room(FRAME(ctx), frame_count(tmpl));
target = kos_dyn_clone_segment(tmpl, NULL, CLONE_FRESH_IDS);
d = kos_dyn_push_delim(ctx, PC(ctx));
kos_xfer_goal_push(ctx, kos_xfer_goal_new(target, d, v, k->resume_pc, ref, 0));
```

The undelimited branch is unchanged minus the INPLACE return-frame loop
(xfer_plan.c:378-410).

**Return from an attached segment** (kos_eval_route_delim_return and
kos_dyn_finish_delimited_pop_return, simplified): unwind to the DELIM;
push the value on the first stack owner below it; set the first FRAME
below it to RETURN_PC; pop the DELIM. The INPLACE branch goes.

**Abort and unwind through prompts.** A prompt has no leave action;
raise, return, break and transfer pass it. A DELIM ends a loop search
and a generator search, and a raise pops it and continues at the
application site (dyn_walk.c: the "live bookmark" test becomes IS_DELIM
and the restitch hook in the raise walk is deleted).

**Transfer checks.**

- `return`: a handler frame copy (KOS_FRAME_SHIFT_HANDLER) raises, and
  in step 8 so does an installed copy's bottom frame. The check runs before
  the unwind walk, so the error is raised at the `return` and a TRY in
  the region or the handler can catch it. It sits at the head of
  eval_event_return, and the two region-return arms of
  kos_return_deliver decline such a frame so it reaches there; the
  frame-to-frame return, whose parent is a frame, does not reach it.
  kos_dyn_unwind_return loses its DELIM stop and its `took` result, and
  eval_event_return_unwind loses the routing branch.
  kos_eval_route_delim_return stays for POP_PROMPT and POP_SHIFT.
- `break`, `continue`: kos_dyn_find_loop stops at a SHIFT as it stops
  at a DELIM. The same-frame test in kos_dyn_unwind_to_loop already
  refuses a callee handler.
- `yield`: a lookup of its own, kos_dyn_yield_gen, stops at a DELIM and
  raises KoskariPromptUnreachable there. kos_dyn_find_gen is left
  crossing every node: transform.c starts from it to collect every
  generator on the chain.

## 5. Surface

- `(reset BODY...)` and `(shift K BODY...)` keep their meaning with
  the default tag, under the abort-first rule.
- `(reset-at TAG BODY...)` and `(shift-at TAG K BODY...)` take an
  evaluated TAG, after racket/control. Separate names avoid the
  ambiguity of an optional first argument in `(reset x (f))`. Layout
  forms `reset-at TAG:` and `shift-at TAG k:` follow the existing
  `reset:` and `shift k:` handlers.
- No tag builtins. None is the default tag, so `(reset-at None ...)` is
  `(reset ...)`; `gensym` makes a fresh one. Any object works as a tag;
  keywords are interned, so `:search` is a stable tag.
- Operands: PUSH_PROMPT and PUSH_SHIFT are COUNT style, the count being
  how many operands name the tag, as YIELD and BREAK count their value.
  0 is the default tag. 1 pops the tag before pushing the prompt or
  capturing, and a popped None is the default tag. Any other count is
  refused when the code is built. No constant is involved. `reset` and
  `shift` emit 0; `reset-at` and `shift-at` compile TAG and emit 1.
  Opcode names are unchanged.
- koskari.solve: `(var search-tag (gensym "solve"))`, `solve`
  and `solve-first` use `reset-at search-tag`, `amb` and `fail` use
  `shift-at search-tag`, and the NoActiveSearch translation tests
  `(is e.tag search-tag)`. A user `reset` inside a solve thunk no longer
  captures amb's shift.
- Errors: KoskariPromptNotFound ("shift has no enclosing reset") and
  KoskariPromptUnreachable keep their meaning. The `prompt_id` attribute
  becomes `tag`, holding the tag object. The stale mention
  of INVOKE_K in compile-shift.md goes. KoskariPromptUnreachable gains
  RuntimeError as a base and is what a refused `return` or `yield`
  raises (section 8).

## 6. What is deleted

| Function or field | Where | Lines (approx.) |
|---|---|---|
| kos_delimited_inplace_ready, kos_delimited_invoke_inplace | xfer_plan.c:42-300 | 260 |
| INPLACE return-frame loop in kos_invoke_continuation | xfer_plan.c:378-410 | 33 |
| kos_dyn_inplace_restitch, kos_dyn_finish_inplace_pop_return | dynchain.c:3194-3330 | 135 |
| kos_dyn_remap_delim_frames | dynchain.c:1262-1293 | 32 |
| kos_shift_capture INVOCATION bound, backfill, falsification, live ids | dynchain.c:2579-2810 | 230, replaced by about 40 |
| baseline handling in kos_continuation_capture_segment | dynchain.c:2489-2577 | 35, moved into capture |
| kos_dyndelim_clone stash re-point, saved resume clone; kos_dyndelim_return_frame_in_span; chain traverse and free | dyn_delim.c | 80 |
| restitch hook in the raise walk | dyn_walk.c:597-610 | 15 |
| kos_dynprompt_new_invocation | dyn_prompt.c | 25 |
| POP_SHIFT instance resolution and invocation branch | opcode/POP_SHIFT.c | 50, replaced by about 20 |
| POP_PROMPT bookmark walk | opcode/POP_PROMPT.c | 25, replaced by about 8 |
| DELIM, SHIFT, prompt, continuation fields, macros, KOS_DYNF_INVOCATION | dynchain.h | 60 |

About 900 lines out, about 250 in (tag objects, abort goal,
applied check, the transfer checks, surface forms).

Tests that exercise only deleted machinery, or assert behavior this
changes (ruled with the premise, section 8):

- tests/native/test_dynchain.c: test_dyndelim_clone_owns_chain_copy and
  test_shift_capture_at_invocation_prompt_frames_inside (deleted
  structures); test_dynprompt_new_free and test_chain_clone_segment
  assert integer prompt ids.
- tests/lib/opcode/test_continuations.py:
  test_tagged_shift_passes_inner_prompt_graft and _inplace use integer
  tag operands; their values (16) hold under the model once rewritten to
  push tag objects.
- tests/compile/specials/test_reset_shift.py:
  test_dyn_wind_thunk_count_invoke_inside_shift_body,
  test_dyn_wind_thunk_count_invoke_in_deeper_frame and
  test_inplace_slice_raise_runs_after_thunk expect `[before, after]`;
  abort-first gives `[before, after, before, after]`.
  test_dyn_wind_thunk_count_multishot_in_shift expects four events;
  abort-first gives six. test_delimited_param_outside_reset_not_captured
  asserts 109 for `(stolen 9)` under amt=200; the compressed bottom gives
  209, which is what its own docstring states.
- tests/reproducers/test_delimited_shapes.py: test_dynwind_around
  (`['in', 'out']` becomes four entries), test_dynwind_multishot (4
  becomes 6).
- tests/reproducers/test_finally_under_transfer.py:
  test_shift_handler_abandoning_slice expects `["handler", "finally", 10]`;
  abort-first gives `["finally", "handler", 10]`.
- tests/reproducers/test_break_across_reset.py:
  test_break_in_shift_handler (9) and test_continue_in_shift_handler
  (`[1, 2, 3]`) reach the loop around the reset from the handler; the
  SHIFT stop makes them "break outside loop" and "continue outside
  loop". The module docstring states the old rule.
- tests/exceptions/test_continuation_errors.py, test_reset_shift.py:657
  and 707, test_continuations.py:522 and 578: the `prompt_id` attribute.
- Values that should hold under docstrings naming Path A, grafts or
  snapshots: test_inplace_invoke_operands.py
  (test_second_invoke_takes_the_graft_path names a path that goes),
  test_delimited_handler_helper.py, test_delimited_cross_frame.py,
  test_delimited_reentrant.py.

None of these were run; the expected values come from reducing each
shape by hand.

## 7. Migration order

Each step should leave the suite green with the ruled expectations.

1. A `yield` with no generator on the chain raises instead of answering
   EVAL_FATAL. First a test for the suspected path: a reset inside a
   generator, k escapes, applied outside any generator, and the region
   yields. Not reproduced yet; if it does not reproduce, the step is
   dropped.
2. Delete the never-read KosContinuation fields `one_shot` and
   `resume_stack`. No behavior change.
3. Compressed template bottom for every delimited
   capture; delete the heritage padding. Checked by test_reset_shift's
   param tests, test_delimited_shapes bind/encl groups,
   test_break_across_reset. Changes
   test_delimited_param_outside_reset_not_captured. New tests: a WIND
   and a TRY outside the reset in the same frame are not re-entered on
   application; a `let` around the reset is readable from a remote
   application.
4. Tag objects in the VM: NULL default tag, count 0 and 1, PROMPT
   `tag`, identity comparison, exception `tag` attribute (None for the
   default tag).
   Independent of the shift model; can go first. The
   encoding and the attribute are ruled. Rewrites the two tagged opcode
   tests. New tests: identity not equality (two equal-looking tags do
   not match); a tag taken from the stack is not in the captured
   operands; a count above 1 is refused at build.
5. Abort-first shift with the handler-side transfer checks: PUSH_SHIFT
   capture plus abort goal, handler frame copy for callee shifts,
   structural POP_SHIFT; the loop search stops at a SHIFT; a `return`
   in a handler's frame copy raises KoskariPromptUnreachable, which gains
   its RuntimeError base here. Delete Path A in the same step, because
   its readiness check would still match a live SHIFT on a live prompt.
   The handler-side checks belong here because the abort would
   otherwise let a handler's `break` reach the next loop out. Checked by
   test_delimited_reentrant, test_delimited_shapes, test_reset_shift_nested,
   test_delimited_cross_frame, test_delimited_handler_helper,
   tests/solve, the corpus reset/shift records (all value-only, unchanged).
   Changes the wind, finally, param and handler-loop tests listed in
   section 6. New tests: the two `yield` examples of section 1; a loop
   inside the handler breaks and continues; a `return` in a callee
   handler raises; a TRY inside the region does not catch a handler
   raise; a parameter bound inside the region is not visible to the
   handler.
6. Delete instance identity: the falsification pass, SHIFT
   `prompt_node_id`, KOS_DYNF_INVOCATION, invocation prompts; POP_PROMPT
   uses the applied check. No behavior change. New test: hand-built
   bytecode with two tags where a shift fires inside a copy attached from a
   handler (the shape that today fails with "captured prompt instance
   not on chain").
7. DELIM reduced to RETURN_PC: delete mode, chain, frame, node, saved
   fields, kos_dyn_remap_delim_frames; delimited clones get id 0. No
   behavior change. Delete the two native DELIM tests (ruled). New
   native test: a call/cc captured inside a delimited application, applied
   after that application returned, delivers to the frame below the
   re-created DELIM.
8. Installed-copy transfer checks: a `return` in a frame on a DELIM raises,
   and the return walk loses its DELIM stop; the generator search stops
   at a DELIM. New tests: a `return` in a copy's bottom frame raises and
   a TRY in the region catches it; a `return` in a callee frame inside
   a copy returns; a region `yield` raises when k is applied in its own
   handler, in another generator, and after k escaped.
9. Surface: `reset-at`, `shift-at`, layouts, docs, corpus
   records; koskari.solve on its own tag. The transfer rules go in the
   user pages for reset, shift, the loops and yield, with a corpus
   record for each refused shape. New tests in tests/compile/specials
   and tests/solve, including a user `reset` inside a solve thunk.

Step 5 changes cost on the shift path: one extra clone per shift (the
first application no longer runs in place) and a frame copy when the
shift is in a callee. Step 8 touches RETURN's unwind path and YIELD's
generator search. Baseline the perf counters before each and compare
after.

## 8. Decisions

Ruled 2026-11-02.

**The premise.** The handler runs after the abort to its prompt
(Danvy-Filinski, DPJS, racket/control), standing where the region
stood. Every other part of this document follows from it, and it is
not open. The test changes in section 6 are its consequences.

Two alternatives were weighed and set aside, so the record shows the
cost that was accepted. Both remove the wind pass that the first
application of k pays under abort-first, where E's extents are left at
the shift and entered again by the copy:

- In place: leave E on the chain, run the handler on top of it, and
  let the first `(k v)` resume E where it stands by freezing the
  handler's span and pushing it back on the prompt when E completes.
  One source, no wasted pass, the handler inside E's extents, and a
  second suspended span to carry: the yo-yo the current implementation
  grew its machinery around.
- Frozen E: lift E off the chain intact at the shift, as a generator
  yield freezes its span, run the handler on the prompt, and let the
  first `(k v)` install the original E on the DELIM instead of a copy.
  No wasted pass and no yo-yo, at the price of two sources behind one
  k and a fresh-or-thawed bit at attachment, the distinction a generator
  resume already makes against a continuation application.

Every Scheme with shift/reset is abort-first and pays the pass. The
ruling takes the same model, with the pass, for one source and one
attachment.

**Consequences of the premise, not separate choices:**

- The template is the segment through the prompt on one copy of the
  owning frame, so extents outside the reset in the same frame are not
  part of k. The one test asserting otherwise was asserting the padding.
- PUSH_PROMPT and PUSH_SHIFT take a count, 0 (default tag) or 1 (tag on
  TOS). An argument is a count or a constant, never both, so a tag is
  never a pool value.
- `prompt_id` becomes `tag`, no alias.
- koskari.solve delimits with a private tag, so a user `reset` cannot
  intercept `amb`.
- Delimited application from Python stays refused: a Python caller has
  no chain to compose onto.
- No `call-with-composable-continuation` and no `abort-at`;
  `(shift-at TAG _ v)` is the abort.
- The surface is `reset-at TAG` and `shift-at TAG K`, with `reset` and
  `shift` as the default-tag forms.

**A transfer across a SHIFT or a DELIM is an error.** Ruled
2026-11-10; the rules are in section 1, Transfers. A `return` in a
callee handler, a `break` or `continue` in a handler whose loop is not
inside the handler, and a `return` or `yield` in an installed copy's bottom
frame all name a target that is not theirs to reach.

Set aside: giving each a second meaning (the `return` completes the
shift or ends the application with its value; the `break` reaches the
next loop out). Which meaning is right is unclear, and an error says so.
The earlier `RETURN` frame-only proposal wanted a separate
`return-shift` form for the DELIM case; with the error there is nothing
for it to do. Common Lisp exit points, Smalltalk's `cannotReturn:`,
Ruby's LocalJumpError and an escape continuation in Racket or Guile
refuse a transfer whose target's extent has ended (from memory, not
checked against the sources).

The refusal is at the transfer, not at compile time: the compiler does
not track loops, and the matching prompt of a shift is dynamic.

**The refused `return` and `yield` raise KoskariPromptUnreachable, which
becomes a RuntimeError.** Ruled 2026-11-10. The class keeps
KoskariContinuationError as a base and gains RuntimeError, so the error
is catchable either way, as "break outside loop" and "continue outside
loop" (plain RuntimeError, unchanged) already are by the second.
`boundary` names the node that refused: the DELIM or the SHIFT.
KoskariContinuationError itself stays outside RuntimeError; its
docstring's statement that the category is deliberately not one gains
this exception.

Nothing is open.

## 9. Sources

- Danvy and Filinski, "Abstracting Control", LFP 1990: shift/reset; the
  reified continuation carries its own delimiter, `\x. <E[x]>`.
- Felleisen, "The Theory and Practice of First-Class Prompts", POPL
  1988, and Shan, "Shift to Control", Scheme Workshop 2004: control/prompt
  differs from shift/reset exactly in whether re-entry re-delimits.
- Dybvig, Peyton Jones and Sabry, "A Monadic Framework for Delimited
  Continuations", JFP 2007: `newPrompt`, `pushPrompt`, `withSubCont`
  (removes the segment up to the nearest mark), `pushSubCont` (pushes a
  copy on the current stack); `shift p f = withSubCont p (\k -> pushPrompt p
  (f (\v -> pushPrompt p (pushSubCont k (return v)))))`. Taken: abort
  before the handler; application composes on the current chain;
  nearest mark by prompt value.
- Kiselyov, delimcc and "Delimited Control in OCaml, Abstractly and
  Concretely" (FLOPS 2010; TCS 2012): prompts are fresh values found
  nearest on the stack; segments are copied out and copied back per
  application. Taken: multi-shot by copy, no instance identity.
- Flatt, Yu, Findler and Felleisen, "Adding Delimited and Composable
  Control to a Production Programming Language", ICFP 2007: prompt tags,
  composable continuations, and their interaction with dynamic-wind,
  parameterize (continuation marks) and exception handlers. Taken:
  application enters the winds in k and leaves none at the application
  site; parameters in k shadow the application site's.
- Racket reference and racket/collects/racket/control.rkt (checked):
  `call-with-continuation-prompt`, `call-with-composable-continuation`,
  `abort-current-continuation`, `default-continuation-prompt-tag`,
  `make-continuation-prompt-tag`; `shift-at` is call/comp, then an abort
  that keeps the prompt and calls the body with k wrapped under a fresh
  prompt. Taken: the `-at` surface and the order of capture and abort.
- R7RS `dynamic-wind` with `call/cc`, and Common Lisp `catch`/`throw`:
  common-ancestor wind replay for undelimited application; evaluated tags
  compared by identity.

Design notes this keeps: the transfer goal, the call/cc seam by id and
phase, restore records, foreign-token unwind, single-shot WITH policy
([callcc.md](callcc.md)); push-time `pop_pc`; the push graft on a DELIM
(Path B). It retires Path A, the INPLACE delim, the handler snapshot,
prompt instance identity, invocation prompts, padding, and RETURN's
stop at a DELIM.

# The end.
