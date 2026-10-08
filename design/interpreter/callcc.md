# Delimited Continuation Implementation

This document describes the delimited continuation implementation in Koskari,
as realized in `eval.c`/`eval.h`, `eval/eval_chain.c`, `dynchain.c`/`dynchain.h`,
and the included units `dynchain/dyn_walk.c` and `dynchain/xfer_plan.c`.

**Invoke model:** two-path per [delimited-invoke-model.md](../archive/delimited-invoke-model.md) — resume-in-place (Path A) when the capture-time prompt is live in dyn_top ancestry; PUSH graft (Path B) otherwise. Capture is bounded at the prompt's owning frame. Both are implemented; the sections below reflect the current behavior.

## Design Decisions (Implemented)

### Delimited Continuations (reset/shift)

Koskari implements **delimited continuations** using the reset/shift paradigm:
- `reset` establishes a prompt delimiter that bounds continuation capture
- `shift` captures the continuation from the current point up to the prompt
- Captured continuations are composable and do not include the prompt itself

This is provided alongside undelimited `call/cc`; both continuation styles
are supported.

### Multi-Shot by Default

All captured continuations are **multi-shot** (reusable):
- Each invocation clones the captured state
- Multiple independent resumptions of the same continuation are supported
- One-shot optimization is available but not the default

### VM-Native Hooks

Wind/unwind hooks are **bytecode blocks**, not Python callables:
- Hooks are code+pc pairs executed within the VM
- No Python/C boundary crossing during hook execution
- Hooks can participate in continuation capture
- Terminated by `END_WIND` sentinel opcode

### Python Boundary Constraint

**Delimited continuations** (shift):
- Captured continuations **cannot span Python/C call boundaries**
- The dynamic chain is scoped to a single evaluator context (driver); each
  driver has its own chain. Delimited capture only finds prompts in that chain.
- Delimited invocation from Python is **blocked**: `DelimitedContinuation.tp_call`
  always raises. Delimited continuations may only be invoked via CALL inside
  koskari_eval (same context).
- This ensures all captured state is VM-resumable

**Full continuations** (call/cc):
- May cross the CPython boundary only in the *upwards* direction
- Crossing is implemented by raising a specialized `BaseException`
  (`KoskariContinuationApply`), forcing a single return through intervening
  Python/C frames until the owning Koskari evaluator loop is reached
- Python and C frames are not embedded in the Koskari dynamic chain; only
  Koskari frames are re-enterable
- When the exception passes through an *intermediate* Koskari eval loop (one
  whose token does not match the continuation's), that context performs a
  **segmented transfer**: it clears the exception, unwinds all its own
  WITH/finally extents cleanly (exit handlers see `(None, None, None)`), then
  re-raises the original exception unchanged via a `KosDynCCRaise` sentinel at
  the root of its chain (see [../archive/evaluator-token-continuation.md §5](../archive/evaluator-token-continuation.md))

## Implementation

### Context Lifecycle

Each evaluator context records an explicit lifecycle state (at least: active,
sleeping/await/yield, exited). Continuations capture a reference to their owning
evaluator context. It is an error to apply a continuation whose owning context
state does not permit invocation. Attempting to invoke an unusable continuation
(delimited or full) raises an exception at the invocation site.

### KosContinuation Structure

```c
struct KosContinuation {
  KosDynNode *captured_chain;   /* cloned chain segment (the template) */
  uint32_t resume_pc;           /* pc to resume at; the frame is resolved
                                   from the landed chain at invoke time */
  KosSegment *resume_stack;     /* aliases a node-owned stack on captured_chain */
  uint32_t prompt_id;           /* target prompt tag (KOS_PROMPT_ID_UNDELIMITED for call/cc) */
  uint8_t one_shot;             /* 0 = multi-shot, 1 = one-shot */
  uint8_t live_slice_consumed;  /* 1 = an in-place invoke consumed the live slice */
  PyObject *token_ref;          /* KosEvaluatorToken (ownership marker) */
  /* capture-time live prompt and shift identities for in-place invoke
     detection; borrowed, confirmed by pointer + node_id ancestry from
     dyn_top before use; zero for undelimited continuations */
  KosDynNode *live_prompt_node;  uint32_t live_prompt_node_id;
  KosDynNode *live_shift_node;   uint32_t live_shift_node_id;
};
```

Both delimited and undelimited continuations use this structure. Undelimited capture (`kos_capture_cc`) sets `prompt_id` to `KOS_PROMPT_ID_UNDELIMITED` and stores the owning context's token in `token_ref`; the template is never landed itself, each invoke clones it.

### KosPyContinuation Python Type

Python-facing continuation wrapper:

```c
typedef struct {
  PyObject_HEAD
  KosContinuation *captured;  /* captured continuation state */
} KosPyContinuation;
```

**Behavior**:
- Calling it from Python never resumes in place: the call raises `KoskariContinuationDead` when the owning evaluator is gone, and otherwise `KoskariContinuationApply` carrying the owner token, the continuation and the packed value, which the owner delivers (in-VM invocation is via CALL inside koskari_eval)
- Multi-shot: clones captured state on each apply
- GC-tracked: traverses captured chain

### Continuation Capture (shift)

The `kos_shift_capture()` function handles capture:

```c
int kos_shift_capture(KosEvalState *es, uint32_t prompt_id,
        uint32_t resume_pc, KosContinuation **out_k);
```

**Algorithm**:

1. **Find prompt**: Locate nearest `KOS_DYN_PROMPT` with matching `prompt_id`
   ```c
   KosDynPrompt *kos_find_prompt(KosContext *ctx, uint32_t prompt_id);
   ```

2. **Validate capture**: Check for Python boundaries in the capture range
   ```c
   int kos_validate_capture(KosContext *ctx, KosDynPrompt *prompt);
   ```

3. **Clone chain segment**: Copy nodes from dyn_top down through the prompt
   and its owning FRAME (stop = the owning frame's parent, exclusive).
   Heritage nodes between dyn_top and the frame (WIND, PARAM) are captured;
   caller frames below the bound are not, so their params resolve from the
   invoke-time environment.
   ```c
   KosDynNode *kos_clone_chain_segment(KosDynNode *top, KosDynNode *stop);
   ```

4. **Create continuation**: Allocate `KosContinuation` with cloned state.
   Record the live prompt node identity (pointer + node_id) for in-place
   invoke detection; `PUSH_SHIFT` records the live SHIFT node right after
   pushing it (capture runs before the node exists).

5. **Abort to prompt**: The caller unwinds to the prompt and runs the handler

### Continuation Invocation

The `kos_invoke_continuation()` function handles resumption:

```c
int kos_invoke_continuation(KosEvalState *es, KosContinuation *k,
        PyObject *value, int is_throw, PyObject *continuation_ref);
```

**Algorithm**:

0. **Delimited in-place (Path A)**: when the capture-time live prompt and
   SHIFT nodes are still on the chain (pointer + node_id ancestry match from
   dyn_top), the SHIFT lies above the prompt, no barrier (foreign
   DELIM/GEN/NEXT/PROMPT) sits between dyn_top and the SHIFT node, no
   transfer goal is active, the invoke is not a throw, and the live slice has
   not already been consumed: `kos_delimited_invoke_inplace` detaches the
   invoke span (dyn_top through the SHIFT node) onto a
   **`KOS_DYN_DELIM_MODE_INPLACE`** delim inserted directly above the live
   prompt, puts an invocation prompt where the live prompt stood under
   the slice, sets the resume frame's pc to the captured resume pc, and
   continues on the live chain: no clone, no goal, no enter replay. The
   operands are already in place: the region's pending operands sit on
   the live prompt and the region nodes between it and the shift, as
   capture left them, and the handler's operands sit on the SHIFT node
   (which owns its own segment) inside the stashed span. The invocation
   prompt takes a copy of the live prompt's operands, so the slice
   resumes over the region's own operands and the live prompt keeps its
   own for the handler's return; the frame's segment is never touched.
   Only the first invoke runs in place; re-invokes fall through to the
   landing path (multi-shot). The slice's `POP_PROMPT` restitches the
   stashed span and restores the invoke-site pc and the resume the
   frame carried into the invoke, both displaced onto the delimiter
   while the slice runs in that frame
   (`kos_dyn_inplace_restitch`); an exception raised in the slice
   restitches likewise and continues the raise from the invoke site.

1. **Seam**: a delimited invoke pushes a **`KOS_DYN_DELIM`** in
   `KOS_DYN_DELIM_MODE_POP_PROMPT` at dyn_top as the seam. An undelimited
   invoke's seam is the lowest common ancestor of the live chain and the
   captured template (`kos_chain_common_ancestor`, matched by node id from
   both roots; clones keep their source's id), so nothing at or below it
   leaves or re-enters; the chains share no node only when the capture
   context is gone, and then the seam is NULL and the whole chain is
   replaced.

2. **Recursion limit**: the template's frame count above the ancestor must
   fit above the seam's frame (undelimited) or above the invoker's frame
   (delimited); `kos_eval_check_frame_room` refuses with `RecursionError`
   before anything moves, so the error raises at the invoke site.

3. **Clone the template**: `kos_dyn_clone_segment` instantiates the template
   above the ancestor's counterpart anew, so multi-shot invokes never share
   nodes; `kos_chain_detach_template` severs the clone's bottom from the
   template. When the slice reaches `POP_PROMPT` (delimited), the return
   walk hands the delim back and the value lands at the invoke site.

4. **Push a goal** (`kos_xfer_goal_new` + `kos_xfer_goal_push`) with the clone
   as target and the value (or exception, `is_throw`) as payload; answer
   REPROLOGUE. The evaluator's prologue drives it (§ Transfer goal).

### Foreign-token unwind

A continuation applied from a context that does not own it (a different
evaluator token) reaches `process_raise` as a `KoskariContinuationApply`
signal. `handle_continuation_apply` pushes a **cc-unwind goal**: no target, the
seam is the root GEN (the driver boundary) when the chain is a generator's,
otherwise NULL, and `is_cc_unwind` is set. LEAVING runs every extent's leave
with `KOS_XFER_EVENT_TRANSFER` (after thunks, `__exit__`s and `finally`
bodies see no active exception); DELIVERING then raises the stored signal as a **boundary raise**
(`kos_eval_prepare_boundary_raise`), frees every goal, and reports it through
`kos_xfer_goal_drive`'s `out_boundary` so the drain returns it to the driver
as it stands, never through `try_local`. The owning context catches the
signal on its own boundary and invokes for real.

### Transfer goal

Landing a continuation is a **`KosXferGoal`** (`xfer_goal.h`): a record of
where control is going, never what to do. `kos_xfer_goal_drive` re-derives
its next step from the live chain on every pass.

```c
typedef enum KosXferGoalPhase {
  KOS_GOAL_LEAVING,     /* unlink dyn_top down to the seam */
  KOS_GOAL_LANDING,     /* splice the target on the seam, set the resume pc */
  KOS_GOAL_ENTERING,    /* enter each landed node, outermost first */
  KOS_GOAL_DELIVERING,  /* push the payload or set the throw slots; retire */
} KosXferGoalPhase;

struct KosXferGoal {
  KosXferGoal *outer;          /* waiting enclosing goal, or NULL */
  KosDynNode *target;          /* owned clone to land; NULL once landed */
  KosDynNode *seam;            /* borrowed; NULL means the root */
  uint32_t seam_id;
  KosDynNode *frontier;        /* node whose thunk we wait on, or NULL */
  uint32_t frontier_id;
  KosDynNode *wait_top;        /* dyn_top as it was when frontier fired */
  PyObject *payload;
  PyObject *continuation_ref;
  uint32_t resume_pc;
  uint8_t phase, is_throw, is_cc_unwind;
};
```

**Delimited landing** (Path B):
1. LEAVING: nothing leaves; the seam (the fresh DELIM) is already dyn_top.
2. LANDING: splice the cloned slice onto the DELIM; renumber its frames.
3. ENTERING: `before` thunks of the slice's WIND nodes, outermost first;
   PARAM extents re-bind; multi-shot withs re-enter.
4. DELIVERING: push the value, stage it for the landed frame's resume when the restore put one back, or raise it into the landed frame.
5. `POP_PROMPT` in the slice returns through the DELIM to the invoke site.

**Undelimited landing** (call/cc):
1. LEAVING: unlink from dyn_top down to the seam through
   `kos_dyn_unlink_top` with `KOS_XFER_EVENT_TRANSFER`: `after` thunks
   and `__exit__`s above the seam run, innermost first; obligations the
   nodes held are dropped. An enabled FINALLY above the seam is not
   unlinked but handed the transfer (`kos_dyn_finally_take_transfer`):
   its body runs in its own frame with the region still at dyn_top,
   disabled and marked `KOS_PENDING_RETURN_TRANSFER`, the goal waiting
   on it with a NULL `wait_top`; `POP_FINALLY` resumes the goal before
   the region leaves and the next prologue continues LEAVING from the
   new dyn_top. Nothing at or below the seam leaves. The seam
   is the lowest common ancestor of the live chain and the template,
   matched from the roots by node id and by phase
   (`kos_dyn_node_same_phase`): a live node that has moved on since the
   capture -- a TRY running its handler, a FINALLY handling a raise, a
   WIND or WITH outside its body, a loop in its else clause, a for-each
   with a step in flight -- is not the state the continuation describes,
   so the match stops below it, the transfer leaves it, and the
   template's copy of it lands fresh with the enter replay that implies
   (a `before` thunk runs again on re-entry, as dynamic-wind requires).
2. LANDING: restore the captured operand stacks of the shared nodes from the
   seam down to its frame (and that frame's registers), then splice the
   clone onto the seam. When the seam is NULL (the chains share no node)
   the clone becomes the whole chain and every outer goal is freed.
3. ENTERING and DELIVERING as above, over the landed nodes only.

A wind, parameterize, or with that encloses both the capture point and the
invoke point is at or below the ancestor, so it neither leaves nor
re-enters, matching Scheme's `dynamic-wind`.

### Re-Entrant Transfers

A thunk or `__exit__` run by LEAVING or ENTERING is an ordinary frame. When
its call installs a frame (REPROLOGUE) or suspends (AWAIT), the goal
**waits** on that node with `wait_top = dyn_top`, and the driver does
nothing until `dyn_top` is `wait_top` again, which means the thunk's frame
came and went. Everything above `wait_top` belongs to the thunk:

1. A continuation invoked from inside the thunk pushes an **inner goal**
   whose `outer` is the waiting one.
2. The inner goal drives on the chain above `wait_top`; a delimited inner
   invoke returns its reset's value to the thunk, which then returns.
3. The thunk's return settles the node (`kos_dyn_settle_top`); dyn_top is
   `wait_top` again and the drive resumes the outer goal where it left off.
4. An inner landing whose LEAVING unlinked the node the outer was waiting on frees the outer goal along with that extent.

A step that raises enters `process_raise`, which frees the whole goal stack
(`kos_xfer_goal_abandon_all`) and raises through the chain as the driver
left it; there is no rollback to the invoke site.

## Wind/Unwind Semantics

### dynamic-wind

`(dynamic-wind before thunk after)`:

1. Push a `WIND` node with before/after thunk callables
2. Run wind phase: invoke `before` thunk if present (sets `entered` when it returns)
3. Execute `thunk`
4. On pop: invoke `after` thunk if present and `entered`; thunks run as normal frames
5. Pop the WIND node
6. Return thunk's result

### Thunk Execution

Before/after thunks are null-arity Koskari callables invoked through the
extent contract (`kos_dyn_extent_enter` / `kos_dyn_extent_leave` on the WIND
node). They run as normal frames; when a thunk returns into the WIND,
`kos_dyn_settle_top` advances the node's phase, a finished leave unlinks it,
and a walk or a transfer goal that was waiting on it continues.

### Thunk Ordering

Wind thunks (`before`) are invoked when:
- Entering a dynamic-wind protected region normally
- Resuming into a region via continuation application

Unwind thunks (`after`) are invoked when:
- Exiting a dynamic-wind protected region normally
- Leaving a region via continuation application
- Exception propagation crosses the region boundary
- Return crosses the region boundary

This matches Scheme's `dynamic-wind` semantics.

## Prompt Tags

### Matching

Shift searches for prompts by `prompt_id`:
- `0` is the default prompt tag
- Non-zero tags enable multiple distinct prompt scopes
- Matching uses numeric equality

### Prompt Scope

A captured continuation contains:
- Cloned dynamic chain segment (FRAME, WIND, REGION nodes)
- Cloned stack blocks (COW)
- Entry point (code + pc) for resumption

It does NOT contain:
- The prompt itself
- Any state above the prompt
- The enclosing context's await state

## GC Considerations

**GC-tracked objects**:
- `KosPyContinuation`: traverses captured chain via `kos_continuation_traverse`
- `KosCoroutine`: traverses context, pending await state

**Not GC-tracked directly**:
- `KosDynNode` chain nodes (traversed via wrappers)
- `KosContinuation` (internal struct)
- `KosSegment` (traversed via chain)

Wrapper `tp_traverse` implementations walk all reachable PyObjects:
- Frame chain: namespace, stack values
- Continuation: captured chain
- Context: await iterator, exception inject slots, the active transfer goal (target clone, payload, continuation reference, and each waiting outer goal)
- Node obligations: a WIND or WITH's stored body value, exception, or return

## Opcodes

### Undelimited continuations (call/cc)
- `PUSH_CC` — Push CC block; NOARG. The resume pc (the instruction after the closing POP_CC) comes from the CC extent, resolved once per site into the cache slot
- `CAPTURE_CC` — Capture undelimited continuation up to the current CC block; push continuation on stack; stores owning evaluator token in the continuation
- `POP_CC` — Pop CC block (normal completion)
- **Invocation**: Use the general `CALL` opcode with stack `[callable, arg_0, ...]`. Same evaluator token → direct apply; different token → VM sets `KoskariContinuationApply` and returns -1 so the exception propagates to the caller. CALL 0 = resume with one value (Py_None); CALL 1 = resume with one argument.

### Prompt Control (delimited)
- `PUSH_PROMPT` — Push prompt node with tag from constant pool
- `POP_PROMPT` — Pop prompt node (normal completion); when returning from an invoked delimited continuation, unwinds to call site and resumes with value
- `PUSH_SHIFT` / `POP_SHIFT` — Capture continuation, enter shift block; POP_SHIFT aborts to past POP_PROMPT

### Wind Control
- `PUSH_WIND` — Push wind node with hook entry points
- `POP_WIND` — Pop wind node
- `END_WIND` — Sentinel: terminate hook block

### Continuation Invocation
- **Undelimited**: Invoke via `CALL` 0 or 1 (continuation and optional arg on stack; stack order [callable, arg_0, ...]).
- **Delimited**: also the `CALL` family; the continuation types install through `kos_vinstall_continuation`, which packs the arguments as return does and runs the two-path invoke (resume in place when the capture-time prompt is live in ancestry, PUSH graft otherwise; return to call site when POP_PROMPT runs). Prompt need not be in chain for the graft path.

## Related Documentation

- [../archive/evaluator-token-continuation.md](../archive/evaluator-token-continuation.md) — Evaluator
  token, KosEvaluatorToken, KoskariContinuationApply, and continuation
  application model
- [../EVALUATOR.md](../EVALUATOR.md) -- Overall evaluation model and API
- [../archive/evaluator-restructure.md](../archive/evaluator-restructure.md) — Design document for
  the DynNode chain restructure (implemented; archived)
- [execution-invariants.md](execution-invariants.md) — Semantic commitments
- [bytecode.md](bytecode.md) — Bytecode format and opcodes
- [../language/control-flow.md](../language/control-flow.md) — Language-level
  control flow including reset/shift
