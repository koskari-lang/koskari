# Evaluator Restructure for Prompts, Winds, and Delimited Continuations

**STATUS: IMPLEMENTED**

Design-decision document recording settled direction and invariants for
restructuring Koskari's evaluator core. This design has been implemented in:
- `koskari/lib/dynchain.h` — DynNode type definitions
- `koskari/lib/dynchain.c` — DynNode implementation
- `koskari/lib/eval.h` — Updated context and frame definitions
- `koskari/lib/eval.c` — Updated evaluator with transfer plan support
- `koskari/lib/stack.h/c` — Refactored to storage-only
- `koskari/lib/opcodes.incl` — New opcodes added

---

## Motivation

The current implementation (eval.c/eval.h) supports undelimited multi-shot
continuations with Python-callable wind/unwind hooks. This is insufficient for:

1. **Delimited continuations**: shift/reset requires prompt markers that
   delimit captured continuation scope.

2. **Composable semantics**: Undelimited continuations capture too much state;
   delimited continuations are more practical and composable.

3. **VM-native hooks**: Wind/unwind hooks as Python callables require crossing
   the Python/C boundary and cannot participate in continuation capture.

4. **Re-entrant transfers**: A wind hook may invoke another continuation,
   requiring nested transfer plans and chain recomputation.

5. **Concern separation**: `KosStackBlock` conflates operand storage with
   control metadata (block_style, labels, enabled).

---

## Goals / Preserved Semantics

- **call/cc preservation**: Undelimited `call/cc` is preserved. Delimited
  shift/reset is supported in addition; both continuation styles are
  supported going forward.

---

## Non-Goals

1. **Python-callable hooks**: Wind/unwind hooks will NOT be arbitrary Python
   callables. They are bytecode blocks executed within the VM.

2. **Backward compatibility**: Existing `KosHandler` and `kos_apply_continuation`
   APIs will be replaced, not extended.

3. **Escape continuations**: Single-shot escape-only continuations (like
   exceptions) remain separate from multi-shot delimited continuations.

4. **Cross-boundary capture**: Continuations cannot span Python/C call stack
   segments. Capture rejects if a Python boundary frame is in scope.

---

## New Data Model Overview

### Concern Separation: Storage vs Control

**KosStackBlock** becomes storage-only:

```c
struct KosStackBlock {
  KosStackBlock *back;    /* previous block (for block-local scopes) */
  size_t refcount;        /* COW reference count */
  size_t max;             /* allocated capacity */
  size_t len;             /* used count */
  PyObject *objs[0];      /* flexible array of values */
};
```

Control metadata (block_style, enabled, labels) moves to DynNode chain.

### Dynamic Chain: DynNode

Unify the "parent chain" as a linked list of typed nodes. This replaces both
`KosFrame.parent` and `KosHandler.parent` with a single traversable chain.

```c
typedef enum KosDynKind {
  KOS_DYN_FRAME,    /* function activation */
  KOS_DYN_PROMPT,   /* reset delimiter */
  KOS_DYN_WIND,     /* dynamic-wind extent */
  KOS_DYN_REGION,   /* block-local control (try, with, loop) */
} KosDynKind;

struct KosDynNode {
  KosDynNode *parent;     /* enclosing node (toward root) */
  KosDynKind kind;
  uint32_t flags;
  /* kind-specific payload follows */
};
```

**FRAME node**: Function activation record.

```c
struct KosDynFrame {
  KosDynNode base;
  KosCode *code;          /* bytecode (borrowed) */
  uint32_t pc;            /* program counter */
  KosStackBlock *stack;   /* operand stack (owned) */
  PyObject *ns;           /* namespace (owned ref) */
};
```

**PROMPT node**: Delimiter for delimited continuations.

```c
struct KosDynPrompt {
  KosDynNode base;
  PyObject *tag;          /* prompt tag for matching (or NULL for default) */
  uint32_t handler_pc;    /* handler body entry point (for shift handler) */
  KosCode *handler_code;  /* handler code object (may differ from enclosing) */
};
```

**WIND node**: Dynamic-wind extent with hook blocks.

```c
struct KosDynWind {
  KosDynNode base;
  KosCode *before_code;   /* before hook bytecode (or NULL) */
  uint32_t before_pc;     /* entry point in before_code */
  KosCode *after_code;    /* after hook bytecode (or NULL) */
  uint32_t after_pc;      /* entry point in after_code */
  uint8_t entered;        /* nonzero if before has run */
};
```

**REGION node**: Block-local control (try/finally, with, loop).

```c
struct KosDynRegion {
  KosDynNode base;
  KosRegionStyle style;   /* TRY, FINALLY, LOOP, WITH, etc. */
  uint32_t a_label;       /* primary jump target */
  uint32_t b_label;       /* secondary jump target (or 0) */
  uint8_t enabled;        /* handler active */
};

typedef enum KosRegionStyle {
  KOS_REGION_TRY,
  KOS_REGION_FINALLY,
  KOS_REGION_WITH,
  KOS_REGION_LOOP,
  KOS_REGION_FOREACH,
  KOS_REGION_AWAIT_WITH,
  KOS_REGION_AWAIT_FOREACH,
} KosRegionStyle;
```

### KosContext Revision

```c
struct KosContext {
  KosDynNode *dyn_top;      /* top of dynamic chain */
  KosDynFrame *frame_top;   /* cached pointer to topmost FRAME node */

  /* Python await bridging (unchanged) */
  PyObject *py_await_iter;
  PyObject *py_last_yielded;

  /* exception injection (unchanged) */
  int pending_throw;
  PyObject *pending_exc;

  uint32_t flags;           /* RUNNING / FROZEN / DEAD */
};
```

---

## Semantics of reset/shift

### reset (PUSH_PROMPT)

`(reset expr)` or `(reset TAG expr)`:

1. Push a PROMPT node onto the dynamic chain.
2. Continue evaluation of `expr`.
3. On normal completion, pop the PROMPT and return the value.

A PROMPT does NOT create a new FRAME. It is a lightweight marker in the chain.

### shift (SHIFT / ABORT_TO_PROMPT)

`(shift k expr)` or `(shift TAG k expr)`:

1. Locate the nearest PROMPT with matching tag (or default prompt).
2. If no prompt found, raise `PromptNotFound` error.
3. Capture continuation from current point up to (but not including) the
   PROMPT as a callable object `k`.
4. Abort computation to the prompt: discard stack/control above the prompt.
5. Bind `k` in the handler context and evaluate `expr`.

**Capture semantics**:

- Clone the dynamic chain segment from current to prompt (exclusive).
- The captured continuation is a `KosPyContinuation` containing the cloned
  segment plus entry point info.

**Invocation of k**:

- Calling `(k value)` splices the captured segment back onto the current
  dynamic chain and resumes at the saved entry point with `value`.

---

## Semantics of dynamic-wind

### dynamic-wind (PUSH_WIND / POP_WIND)

`(dynamic-wind before thunk after)`:

1. Push a WIND node with `before_code`/`after_code` pointers.
2. Execute `before` hook block (via HookRunner).
3. Mark WIND as `entered = 1`.
4. Execute `thunk`.
5. Execute `after` hook block.
6. Pop the WIND node.
7. Return thunk's result.

### Hook Execution via HookRunner

Hooks are NOT Python callables. They are bytecode blocks executed within the
VM using a dedicated micro-execution context:

```c
struct KosHookRunner {
  KosCode *code;          /* hook bytecode */
  uint32_t pc;            /* entry point */
  KosStackBlock *scratch; /* temporary operand stack */
};
```

**Execution**:

1. Allocate (or reuse) a scratch stack.
2. Execute bytecode starting at `pc` until `END_WIND` sentinel opcode.
3. `END_WIND` terminates hook; any TOS value is discarded (hooks return void).
4. Deallocate scratch stack.

Hooks run with the current namespace inherited from the enclosing FRAME but
with an isolated operand stack.

### Wind/Unwind Ordering

On continuation transfer from `src` chain to `dst` chain:

1. **Unwind phase**: Execute `after` hooks from `src` toward LCA.
   - Order: innermost (top) to outermost (toward LCA).
   - Only WIND nodes with `entered = 1`.

2. **Install phase**: Splice `dst` chain segment onto current chain.

3. **Wind phase**: Execute `before` hooks from LCA toward `dst`.
   - Order: outermost (from LCA) to innermost (toward dst top).
   - Mark each WIND as `entered = 1` after its before hook completes.

---

## TransferPlan Algorithm

### Motivation

Continuation application (and shift abort) requires executing wind/unwind
hooks. These hooks may themselves invoke continuations, requiring nested
transfer plans.

### TransferPlan Structure

```c
typedef enum KosTransferStep {
  KOS_XFER_RUN_HOOK,      /* execute a hook block */
  KOS_XFER_INSTALL,       /* splice target chain segment */
  KOS_XFER_INJECT,        /* push resume value onto stack */
} KosTransferStep;

struct KosTransferAction {
  KosTransferStep step;
  union {
    struct { KosCode *code; uint32_t pc; } hook;
    struct { KosDynNode *segment; } install;
    struct { PyObject *value; int is_throw; } inject;
  } data;
};

struct KosTransferPlan {
  KosTransferPlan *suspended;   /* outer plan if nested */
  KosTransferAction *actions;   /* action stack (grows downward) */
  size_t action_count;
  size_t action_index;          /* next action to execute */
  KosDynNode *original_target;  /* for chain-change detection */
};
```

### Algorithm: kos_transfer_execute

```
function kos_transfer_execute(ctx, plan):
  while plan.action_index < plan.action_count:
    action = plan.actions[plan.action_index]
    plan.action_index++

    switch action.step:
      case RUN_HOOK:
        result = kos_run_hook(ctx, action.hook.code, action.hook.pc)
        if result == NESTED_TRANSFER:
          // Hook invoked another continuation
          plan.suspended = ctx.active_plan
          ctx.active_plan = plan
          return SUSPENDED

      case INSTALL:
        kos_install_chain_segment(ctx, action.install.segment)

      case INJECT:
        if action.inject.is_throw:
          ctx.pending_throw = 1
          ctx.pending_exc = action.inject.value
        else:
          kos_stack_push(ctx.frame_top.stack, action.inject.value)

  // Plan complete
  return COMPLETE
```

### Re-Entrancy Handling

When a hook invokes another continuation:

1. **Suspend current plan**: Push onto `ctx.active_plan` stack.
2. **Create new plan**: Build transfer plan for new continuation.
3. **Execute new plan**: Run to completion (may recurse).
4. **Resume suspended plan**: Pop and check chain consistency.

### Chain-Change Detection

After a nested transfer completes, the dynamic chain may have changed. The
suspended plan must detect this:

```
function kos_transfer_resume(ctx, plan):
  if chain_differs(ctx.dyn_top, plan.original_target):
    // Recompute remaining actions from current chain to original target
    kos_transfer_replan(ctx, plan)

  return kos_transfer_execute(ctx, plan)
```

**Recomputation**: Re-walk from current `dyn_top` to the original target,
rebuilding the action list for remaining unwind/wind hooks.

---

## Invariants and Safety Constraints

### INV-1: Python Boundary Constraint

Captured continuations MUST NOT include Python/C call stack segments. If the
dynamic chain contains a boundary marker (e.g., `KOS_DYN_PYCALL`), shift
capture is rejected with an error.

**Rationale**: Python call frames cannot be cloned or resumed; including them
would create non-resumable continuations.

### INV-2: Single Active Driver

A `KosContext` has exactly one active driver at any time. Re-entrant resume
attempts are errors.

**Enforcement**: `in_send` guard in `KosPyCoroutine`, checked before
`kos_drive`.

### INV-3: Hook Isolation

Hooks execute with isolated operand stacks. They cannot observe or modify the
enclosing frame's stack.

**Rationale**: Hooks are cleanup/setup actions, not computations that return
values to the caller.

### INV-4: WIND Idempotence

A WIND node's `before` hook runs at most once per entry. The `entered` flag
prevents re-execution during chain manipulation.

### INV-5: TransferPlan Atomicity

A transfer plan executes atomically with respect to normal VM execution. The
VM does not interleave opcode dispatch with mid-transfer state.

**Exception**: Hooks may suspend on AWAIT, in which case the transfer plan is
suspended along with the context.

### INV-6: Prompt Tag Matching

Shift searches for prompts by tag identity (`is` comparison, not equality).
`NULL` tag matches the default prompt.

### INV-7: Captured Continuation Scope

A captured continuation contains:
- Cloned dynamic chain segment (FRAME, WIND, REGION nodes; not PROMPT).
- Cloned stack blocks (COW).
- Entry point (code + pc) for resumption.

It does NOT contain:
- The prompt itself.
- Any state above the prompt.
- The enclosing context's await state.

### INV-8: Continuation Multi-Shot

All captured continuations are multi-shot. Each invocation clones the captured
segment before splicing.

---

## Minimal Opcode Requirements

### New Opcodes

| Opcode | Args | Description |
|--------|------|-------------|
| `PUSH_PROMPT` | tag_slot | Push PROMPT node with optional tag |
| `POP_PROMPT` | | Pop PROMPT node (normal completion) |
| `SHIFT` | tag_slot, k_slot | Capture continuation, abort to prompt |
| `PUSH_WIND` | before_pc, after_pc | Push WIND node with hook entry points |
| `POP_WIND` | | Pop WIND node (runs after hook) |
| `END_WIND` | | Sentinel: terminate hook execution |
| `PUSH_REGION` | style, label_a, label_b | Push REGION node |
| `POP_REGION` | | Pop REGION node |
| `INVOKE_CONTINUATION` | | Apply continuation from TOS |

### Sentinel Opcode: END_WIND

`END_WIND` terminates a hook block. When the HookRunner encounters this:
1. Discard any TOS value.
2. Return control to the transfer plan.

### Structured Region Metadata

Regions (try, finally, loop, with) use REGION nodes instead of stack block
metadata. This separates control flow from operand storage.

---

## Implementation Staging Plan

### Stage 1: Introduce DynNode Infrastructure

1. Define `KosDynNode`, `KosDynKind`, and node subtypes.
2. Add `dyn_top` to `KosContext`; keep `top` (KosFrame*) as alias.
3. Implement node allocation/free functions.
4. Add GC traversal for new node types.

**Deliverable**: DynNode types compile and link; no behavioral change yet.

### Stage 2: Migrate KosFrame to KosDynFrame

1. Change `KosFrame` to embed `KosDynNode base`.
2. Update `kos_frame_new/clone/free` to use DynNode linkage.
3. Migrate `kos_push_wind/pop_wind` to create WIND nodes.
4. Remove `KosHandler` type; wind hooks remain Python callables temporarily.

**Deliverable**: Existing tests pass with new frame structure.

### Stage 3: Separate KosStackBlock Concerns

1. Remove `block_style`, `enabled`, `a_label`, `b_label` from `KosStackBlock`.
2. Introduce `KosDynRegion` for block-local control metadata.
3. Update bytecode emission to use PUSH_REGION/POP_REGION.
4. Update exception/loop dispatch to consult REGION nodes.

**Deliverable**: Block-structured control flow uses REGION nodes.

### Stage 4: Implement Prompt/Shift Basics

1. Add `KosDynPrompt` node type.
2. Implement `PUSH_PROMPT`, `POP_PROMPT` opcodes.
3. Implement `SHIFT` opcode with continuation capture.
4. Create `KosPyDelimitedContinuation` Python wrapper.
5. Implement basic `kos_apply_delimited_continuation` (no hooks yet).

**Deliverable**: reset/shift work for trivial cases (no winds crossed).

### Stage 5: Hook Execution as Bytecode

1. Implement `KosHookRunner` micro-execution context.
2. Change WIND nodes to store code+pc instead of PyObject callables.
3. Implement `END_WIND` opcode and hook termination.
4. Update compiler to emit hook blocks with `END_WIND` terminator.

**Deliverable**: dynamic-wind hooks execute as bytecode.

### Stage 6: TransferPlan and Re-Entrancy

1. Implement `KosTransferPlan` and `KosTransferAction`.
2. Implement `kos_transfer_build` to construct action list.
3. Implement `kos_transfer_execute` with suspension support.
4. Implement chain-change detection and replanning.
5. Update continuation application to use TransferPlan.

**Deliverable**: Full re-entrant transfer semantics.

---

## Related Documentation

- [evaluator.md](evaluator.md) — Current evaluation model
- [callcc.md](callcc.md) — Legacy undelimited continuation design
- [execution-invariants.md](execution-invariants.md) — Execution model invariants
- [bytecode.md](bytecode.md) — Bytecode format and opcodes
- [../language/control-flow.md](../language/control-flow.md) — Language-level control flow
