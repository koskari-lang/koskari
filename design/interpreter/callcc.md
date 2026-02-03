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

Captured continuations **cannot span Python/C call boundaries**:
- `KOS_DYN_PYCALL` nodes mark Python boundaries in the chain
- `shift` capture rejects if any boundary exists between current and prompt
- This ensures all captured state is VM-resumable

## Implementation

### KosContinuation Structure

```c
struct KosContinuation {
  KosDynNode *captured_chain;   /* cloned chain segment */
  KosDynFrame *resume_frame;    /* frame to resume into */
  uint32_t resume_pc;           /* pc to resume at */
  KosStackBlock *resume_stack;  /* stack state (COW reference) */
  uint32_t prompt_id;           /* target prompt tag */
  uint8_t one_shot;             /* 0 = multi-shot, 1 = one-shot */
};
```

### KosPyContinuation Python Type

Python-facing continuation wrapper:

```c
typedef struct {
  PyObject_HEAD
  KosContinuation *captured;  /* captured continuation state */
} KosPyContinuation;
```

**Behavior**:
- Callable from Python
- `tp_call(value)` returns a `KosPyCoroutine` that resumes with `value`
- Multi-shot: clones captured state on each call
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

2. **Build transfer plan**: Create action list for wind/unwind hooks
   ```c
   int kos_build_transfer_plan(KosContext *ctx, KosDynNode *target,
           PyObject *value, int is_throw, KosTransferPlan **out_plan);
   ```

3. **Execute transfer**: Run the plan to completion
   ```c
   int kos_run_transfer_step(KosContext *ctx);
   ```

### Transfer Plan

Transfer plans orchestrate continuation application with proper hook execution:

```c
typedef enum KosTransferStep {
  KOS_XFER_RUN_HOOK,  /* execute a wind/unwind hook block */
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

**Plan structure**:
1. `RUN_HOOK` actions for `after` hooks from current to LCA (inner-to-outer)
2. `INSTALL` action to splice target chain
3. `RUN_HOOK` actions for `before` hooks from LCA to target (outer-to-inner)
4. `INJECT` action to push resume value

### LCA Computation

```c
KosDynNode *kos_compute_lca(KosDynNode *a, KosDynNode *b);
```

Finds the lowest common ancestor between two chain positions. Used to determine
which hooks need to run during transfer.

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
- `KosPyCoroutine`: traverses context, pending await state

**Not GC-tracked directly**:
- `KosDynNode` chain nodes (traversed via wrappers)
- `KosContinuation` (internal struct)
- `KosStackBlock` (traversed via chain)

Wrapper `tp_traverse` implementations walk all reachable PyObjects:
- Frame chain: namespace, stack values
- Continuation: captured chain
- Context: await iterator, pending exception, transfer plan values

## Opcodes

### Prompt Control
- `PUSH_PROMPT` — Push prompt node with tag from constant pool
- `POP_PROMPT` — Pop prompt node (normal completion)
- `SHIFT` — Capture continuation, abort to prompt

### Wind Control
- `PUSH_WIND` — Push wind node with hook entry points
- `POP_WIND` — Pop wind node
- `END_WIND` — Sentinel: terminate hook block

### Continuation Invocation
- `INVOKE_K` — Invoke continuation from TOS

### Region Control
- `PUSH_REGION` — Push region node for try/finally/loop/with
- `POP_REGION` — Pop region node

## Related Documentation

- [evaluator.md](evaluator.md) — Overall evaluation model and API
- [evaluator-restructure.md](evaluator-restructure.md) — Design document for
  the DynNode chain restructure
- [execution-invariants.md](execution-invariants.md) — Semantic commitments
- [bytecode.md](bytecode.md) — Bytecode format and opcodes
- [../language/control-flow.md](../language/control-flow.md) — Language-level
  control flow including reset/shift
