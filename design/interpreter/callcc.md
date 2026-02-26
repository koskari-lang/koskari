# Delimited Continuation Implementation

This document describes the delimited continuation implementation in Koskari,
as realized in `eval.c`/`eval.h` and `dynchain.c`/`dynchain.h`.

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
  the root of its chain (see [evaluator-token-continuation.md §5](evaluator-token-continuation.md))

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
  KosDynNode *captured_chain;   /* cloned chain segment */
  KosDynFrame *resume_frame;    /* frame to resume into */
  uint32_t resume_pc;           /* pc to resume at */
  KosStackBlock *resume_stack;  /* stack state (COW reference) */
  uint32_t prompt_id;           /* target prompt tag (KOS_PROMPT_ID_UNDELIMITED for call/cc) */
  uint32_t graft_point_id;      /* node_id of graft point for LCA fallback; 0 for capture-cc */
  uint8_t one_shot;             /* 0 = multi-shot, 1 = one-shot */
  PyObject *token_ref;          /* KosEvaluatorToken (ownership); NULL for legacy undelimited */
};
```

Both delimited and undelimited continuations use this structure. Undelimited capture (`kos_capture_cc`) sets `prompt_id` to `KOS_PROMPT_ID_UNDELIMITED` and stores the owning context’s token in `token_ref`. Invocation (via the CALL opcode or from Python) checks the current evaluator token against `token_ref`; mismatch raises `KoskariContinuationApply`.

### KosPyContinuation Python Type

Python-facing continuation wrapper:

```c
typedef struct {
  PyObject_HEAD
  KosContinuation *captured;  /* captured continuation state */
} KosPyContinuation;
```

**Behavior**:
- Not callable from Python: `tp_call` always raises `TypeError` (invocation only via CALL inside koskari_eval)
- Multi-shot: clones captured state on each apply
- GC-tracked: traverses captured chain

### Continuation Capture (shift)

The `kos_shift_capture()` function handles capture:

```c
int kos_shift_capture(KosContext *ctx, uint32_t prompt_id,
        KosContinuation **out_k);
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

3. **Clone chain segment**: Copy nodes from current to prompt (exclusive)
   ```c
   KosDynNode *kos_clone_chain_segment(KosDynNode *top, KosDynNode *stop);
   ```

4. **Create continuation**: Allocate `KosContinuation` with cloned state

5. **Abort to prompt**: The caller unwinds to the prompt and runs the handler

### Continuation Invocation

The `kos_invoke_continuation()` function handles resumption:

```c
int kos_invoke_continuation(KosContext *ctx, KosContinuation *k,
        PyObject *value, int is_throw);
```

**Algorithm**:

1. **Clone if multi-shot**: For multi-shot continuations, clone before use

2. **Build transfer plan**:
   - **Delimited (push model)**: Use `kos_build_delimited_push_plan()`. The
     prompt need not be in the current chain; invoke grafts the continuation
     segment onto the current frame_top and sets `DELIMITED_RETURN_FRAME` /
     `DELIMITED_RETURN_PC` so that when the continuation hits `POP_PROMPT`,
     control unwinds to the call site and resumes with the value.
   - **Undelimited**: Use `kos_build_transfer_plan()` with INSTALL:
     ```c
     int kos_build_transfer_plan(KosContext *ctx, KosDynNode *target,
             PyObject *value, int is_throw, uint32_t graft_point_id,
             KosTransferPlan **out_plan);
     ```

3. **Execute transfer**: Run the plan to completion
   ```c
   int kos_run_transfer_step(KosContext *ctx);
   ```

### KosDynCCRaise Sentinel

`KosDynCCRaise` (kind `KOS_DYN_CC_RAISE = 12`) is a dynamic chain node used
exclusively on the foreign-token transfer path. It holds a reference to a
`KoskariContinuationApply` exception value and carries the
`KOS_DYN_CAP_NEEDS_UNWIND_ACTION` capability.

`kos_build_cc_unwind_plan` inserts it at the **root** of the chain (below all
existing nodes) before building a NULL-target transfer plan. As the plan
unwinds from innermost to outermost, normal WITH/dyn-with/finally extents run
their exit handlers first (with no active exception). When the sentinel is
reached last, its `cc_raise_unwind` callback calls `PyErr_SetObject` with the
stored exception and returns `-1`, causing the transfer to exit with
`KOS_EV_RAISE` and propagate the exception upward.

The sentinel is never cloned (cloning raises `RuntimeError`) and is freed by
`kos_dynnode_free` via the standard freelist.


### Transfer Plan

Transfer plans orchestrate continuation application with proper hook execution:

```c
typedef enum KosTransferStep {
  KOS_XFER_EXTENT_ACTION,  /* execute extent wind or unwind for a node */
  KOS_XFER_PUSH,      /* append continuation segment to current chain (delimited) */
  KOS_XFER_INSTALL,   /* splice target chain segment */
  KOS_XFER_INJECT,    /* push resume value onto stack */
} KosTransferStep;

struct KosTransferPlan {
  KosTransferPlan *suspended;   /* outer plan if nested */
  KosTransferAction *actions;   /* action array */
  size_t action_count;          /* total actions */
  size_t action_index;          /* next action index */
  KosDynNode *original_target;  /* for chain-change detection */
  uint32_t chain_version;       /* version at plan creation */
};
```

**Plan structure** (undelimited / INSTALL):
1. `EXTENT_ACTION` (unwind) for WIND after hooks and PARAM/DYN_WITH from current to LCA (inner-to-outer)
2. `INSTALL` action to splice target chain
3. `EXTENT_ACTION` (wind) for WIND before hooks from LCA to target (outer-to-inner)
4. `INJECT` action to push resume value

**Plan structure** (delimited / PUSH):
1. `EXTENT_ACTION` (wind) for PARAM nodes in the continuation segment, if any
2. `PUSH` action to graft the continuation frames onto the current chain
   (caller stays below; no INSTALL)
3. `INJECT` action to push resume value
4. When the continuation runs and hits `POP_PROMPT`, unwind to `DELIMITED_RETURN_FRAME`,
   push the value, set PC to `DELIMITED_RETURN_PC`, and continue the eval loop

### LCA Computation

```c
KosDynNode *kos_compute_lca(KosDynNode *a, KosDynNode *b, uint32_t graft_point_id);
```

Finds the lowest common ancestor between two chain positions. Uses pointer
equality first; when chains are disjoint (e.g. template vs current), a nonzero
`graft_point_id` triggers an ID-based fallback to locate the graft point in the
first chain. Used to determine which hooks need to run during transfer.

### Re-Entrant Transfers

When a hook invokes another continuation:

1. **Suspend current plan**: Push onto `ctx->plan_stack`
2. **Create new plan**: Build transfer plan for new continuation
3. **Execute new plan**: Run to completion (may recurse)
4. **Resume suspended plan**: Pop and check chain consistency
5. **Replan if needed**: If chain changed, recompute remaining actions

```c
void kos_suspend_transfer_plan(KosContext *ctx);
int kos_resume_transfer_plan(KosContext *ctx);
```

### Chain-Change Detection

After a nested transfer completes, the dynamic chain may have changed. The
suspended plan detects this via `chain_version`:

```c
if (ctx->chain_version != plan->chain_version) {
  /* Chain changed during nested transfer - may need to replan */
}
```

## Wind/Unwind Semantics

### dynamic-wind

`(dynamic-wind before thunk after)`:

1. Push a `WIND` node with `before_code`/`after_code` pointers
2. Execute `before` hook block (via HookRunner)
3. Mark WIND as `entered = 1`
4. Execute `thunk`
5. Execute `after` hook block
6. Pop the WIND node
7. Return thunk's result

### Hook Execution

Hooks are NOT Python callables. They are bytecode blocks executed via
`KosHookRunner`:

```c
struct KosHookRunner {
  KosCode *code;          /* hook bytecode */
  uint32_t pc;            /* entry point */
  KosStackBlock *scratch; /* isolated operand stack */
  int active;             /* nonzero if executing */
};
```

**Execution**:
1. Allocate (or reuse) a scratch stack
2. Execute bytecode starting at `pc` until `END_WIND` sentinel opcode
3. `END_WIND` terminates hook; any TOS value is discarded (hooks return void)
4. Deallocate scratch stack

Hooks run with the current namespace but with an isolated operand stack.

### Hook Ordering

Wind hooks (`before`) are invoked when:
- Entering a dynamic-wind protected region normally
- Resuming into a region via continuation application

Unwind hooks (`after`) are invoked when:
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
- `KosStackBlock` (traversed via chain)

Wrapper `tp_traverse` implementations walk all reachable PyObjects:
- Frame chain: namespace, stack values
- Continuation: captured chain
- Context: await iterator, pending exception, transfer plan values

## Opcodes

### Undelimited continuations (call/cc)
- `PUSH_CC` — Push CC block; arg = resume label (instruction index after POP_CC)
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
- `INVOKE_K` / `INVOKE_K_VAR` — Invoke delimited continuation; push model (graft frames onto current chain; return to call site when POP_PROMPT runs). Prompt need not be in chain.

## Related Documentation

- [evaluator-token-continuation.md](evaluator-token-continuation.md) — Evaluator
  token, KosEvaluatorToken, KoskariContinuationApply, and continuation
  application model
- [evaluator.md](evaluator.md) — Overall evaluation model and API
- [../archive/evaluator-restructure.md](../archive/evaluator-restructure.md) — Design document for
  the DynNode chain restructure (implemented; archived)
- [execution-invariants.md](execution-invariants.md) — Semantic commitments
- [bytecode.md](bytecode.md) — Bytecode format and opcodes
- [../language/control-flow.md](../language/control-flow.md) — Language-level
  control flow including reset/shift
