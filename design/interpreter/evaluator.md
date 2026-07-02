# Evaluation Model

This document describes the Koskari evaluator implementation in `eval.c`/`eval.h`
and the supporting DynNode chain infrastructure in `koskari/lib/dynchain.c`,
`koskari/lib/dynchain.h`, and the `#include` units under `koskari/lib/dynchain/`.
The KosCoroutine type and `kos_run_code_as_coroutine` (the Python-facing
entry that creates a coroutine from code+namespace) live in `func.c`/`func.h`;
driving a KosCoroutine (send/await) invokes `koskari_eval()` from func.c.

**Scope**: VM structures and C API reference. For the numbered execution invariants list, see [execution-invariants.md](execution-invariants.md). For the combined master reference (invariants + type details + implementation status), see [../INVARIANTS-AND-CLARIFICATIONS.md](../INVARIANTS-AND-CLARIFICATIONS.md). For the staged “signal out / cross frame” vs “deliver to parent consumer” split (RETURN/YIELD/RAISE post-opcode helpers), see [boundary-control-reemit.md](boundary-control-reemit.md).

## Design Philosophy

Koskari's evaluator implements:
- **Delimited multi-shot continuations** (reset/shift)
- **VM-native dynamic wind/unwind hooks** (bytecode blocks, not Python callables)
- **Flattened async execution**
- **Unified dynamic chain** for all control flow concerns
- **Eval-owned boundary staging** where frame crossing (unwind/pop) is separated from parent-consumer delivery using `KosOpResult` and `eval_event_*` helpers in `eval.c` (no `KosEvent` struct); see [boundary-control-reemit.md](boundary-control-reemit.md)

## Core VM Structures

### Dynamic Chain (DynNode)

The evaluator uses a unified **dynamic chain** that links all execution context:
frames, prompts, wind extents, and control regions. This replaces the previous
separate frame chain and handler chain.

**KosDynNode** — Single tagged-union struct for all chain node kinds. Base fields
`parent`, `kind`, `flags`, `caps`, `node_id`, and **`stack`** (optional owned operand
extent; `NULL` = inherit) precede union `u`. Payload variants live in `u` (see
`struct KosDynNode` in [`dynchain.h`](../../koskari/lib/dynchain.h)). **`KosDynFrame_STACK`**
reads this **`stack`** field on the node, not a member of `u.frame`.
Typedefs (KosDynFrame, KosDynPrompt, KosDynWind, KosDynNs, KosDynCC, KosDynWith,
KosDynShift, KosDynParam, KosDynYieldFrom, …) all refer to this same struct.
**All field access must use the macros** defined in dynchain.h; do not touch union
members or base fields directly. Node kinds are enumerated by `KosDynKind`:
`KOS_DYN_FRAME` through `KOS_DYN_GEN` (see `dynchain.h`; `KOS_DYN_UNKNOWN` is a
sentinel). The macro `KosDynNode_IS_REGION` matches TRY, FINALLY, LOOP, and
FOREACH for convenience. Behavior is driven by capability bits (`KOS_DYN_CAP_*`);
see [dynamic-node-unified-api.md](../archive/dynamic-node-unified-api.md) for the capability model.

**Context managers (`KOS_DYN_WITH`)** — Lexical `with` and `dyn-with` share one
node kind. Multi-shot vs single-shot is **not** a second `KosDynKind`; it is
`u.with.shot` (`KOS_DYN_WITH_SHOT_MULTI`, `KOS_DYN_WITH_SHOT_SINGLE_ACTIVE`,
`KOS_DYN_WITH_SHOT_SINGLE_DONE`) together with caps set at construction (e.g.
`KOS_DYN_CAP_NEEDS_INSTALL_WIND` only for multi-shot). Policy stays in
`dynchain/dyn_with.c`; generic `dynchain.c` dispatches on kind and caps only.
See [with-dyn-with.md](with-dyn-with.md) and `dynchain.h`.

- **Frame** (u.frame): code (borrowed), pc (next instruction), acting_pc (instruction currently being executed), ns (owned), frame_flags, native_handler (optional; when non-NULL, handler is frame evaluator; see [api/special-call-protocol.md](../api/special-call-protocol.md)).
  Macros: KosDynFrame_CODE, KosDynFrame_PC, KosDynFrame_ACTING_PC, KosDynFrame_STACK, KosDynFrame_NS, KosDynFrame_FLAGS, KosDynFrame_NATIVE_HANDLER.
- **Prompt** (u.prompt): prompt_id; pending_shift_wrapper (KosPyDelimitedContinuation when PUSH_SHIFT plan completed; owned).
  Macros: KosDynPrompt_ID, KosDynPrompt_PENDING_SHIFT_WRAPPER.
- **Wind** (u.wind): before_thunk, after_thunk, body_result, entered, phase (`KosDynWindPhase`).
  Macros: KosDynWind_BEFORE_THUNK, KosDynWind_AFTER_THUNK, KosDynWind_BODY_RESULT, KosDynWind_ENTERED, KosDynWind_PHASE.
- **Try** (u.try_): handler_pc, enabled. Macros: KosDynTry_HANDLER_PC, KosDynTry_ENABLED.
- **Finally** (u.finally_): handler_pc, enabled, handling_raise, handling_return, handling_result. Macros: KosDynFinally_HANDLER_PC, KosDynFinally_ENABLED, KosDynFinally_HANDLING_RAISE, KosDynFinally_HANDLING_RETURN, KosDynFinally_HANDLING_RESULT.
- **Loop** (u.loop): break_pc, continue_pc, enabled, result. Macros: KosDynLoop_BREAK_PC, KosDynLoop_CONTINUE_PC, KosDynLoop_ENABLED, KosDynLoop_RESULT.
- **Foreach** (u.foreach): break_pc, continue_pc, enabled, result, iter, exhausted, iter_next_else_pc (else PC when ITER_NEXT gen exhausts; 0 = not set). Macros: KosDynForeach_ITER, KosDynForeach_EXHAUSTED, KosDynForeach_ITER_NEXT_ELSE_PC, and the shared Loop macros.
- **Ns** (u.ns): saved_ns. Macro: KosDynNs_SAVED_NS.
- **With** (u.with): context manager enter/exit, optional `as` name, `resume_pc`,
  async phases (`KOS_WITH_PHASE_*`), and **`shot`** (`KOS_DYN_WITH_SHOT_*`).
  Macros: `KosDynWith_MGR`, `KosDynWith_ENTER`, `KosDynWith_EXIT`, `KosDynWith_SHOT`, etc.
- **CC** (u.cc): resume_pc. Macro: KosDynCC_RESUME_PC.

All nodes are allocated from a single-size pool in dynchain.c (one freelist for
all kinds). KosContinuation and KosContext have separate freelists/caches (see
dynchain.c and eval.c).

### KosContext — Top-level VM Execution State

Authoritative layout is `struct KosContext` in [`eval.h`](../../koskari/lib/eval.h).
It holds `dyn_top`, `frame_top`, **`block`** (address of the innermost operand-stack
`KosStackBlock *`; maintained with the dyn chain), `globals_frame` / cache version,
transfer-plan fields, unwind/return staging, Python await bridge (`py_await_iter`,
`py_resume_inject`, `py_resume_kind`), exception inject fields, `suspension_event`,
and `flags`. Delimited **invoke return** metadata lives on **`KOS_DYN_DELIM`**
nodes (`kos_dyndelim_*`, `KosDynDelim_*`), not in separate context slots.
Async **WITH** enter/exit suspension does **not** use separate `KosContext`
slots: state lives on the `KosDynWith` node (`phase`, `exc_*`, `normal_exit`, etc.);
see `kos_dyn_complete_pending_exit` in `dynchain.c` and `revive_await` in `eval.c`.

```c
/* Abbreviated; see eval.h for full struct KosContext */
struct KosContext {
  KosDynNode *dyn_top;
  KosDynFrame *frame_top;
  KosStackBlock **block;
  PyObject *namespace;
  KosEvaluatorToken *token;
  PyObject *domain;
  PyObject *pending_return_value;
  int pending_return_to_parent;
  KosTransferPlan *plan;
  KosTransferPlan *plan_stack;
  PyObject *py_resume_inject;
  uint8_t py_resume_kind;
  PyObject *py_await_iter;
  int inject_mode;
  PyObject *inject_type;
  PyObject *inject_value;
  uint8_t suspension_event;
  uint32_t flags;
  struct KosContext *context_freelist_next;
};
```

### KosStackBlock — Storage-Only Operand Stack

Control flow metadata (block style, labels, enabled flag) lives in the region
nodes on the dynamic chain (TRY, FINALLY, LOOP, FOREACH). `KosStackBlock` is purely storage:

```c
struct KosStackBlock {
  KosStackBlock *back;    /* previous block */
  size_t refcount;        /* for copy-on-write */
  size_t max;             /* allocated capacity */
  size_t len;             /* used count */
  PyObject *objs[0];      /* flexible array of values */
};
```

### Operand stack authority

The active operand stack for execution is **`KosContext.block`** (during RUN,
mirrored on **`KosEvalState`** via **`KosEvalState_Stack*`** helpers). That
pointer tracks the innermost **`KosDynNode.stack`** extent installed on the
dynamic chain — not a separate “frame stack” authority.

Rules (landed; archived proposal [node-owned-operand-stack.md](../archive/node-owned-operand-stack.md)):

1. **Single source of truth** — opcodes push/pop via **`KosEvalState_Stack*`** /
   **`ctx->block`**; they do not infer region boundaries from
   **`KosStackBlock.back`** on the hot path.
2. **Symmetric lifecycle** — installing a dyn node that owns a stack extent
   sets **`ctx->block`** to that node's **`stack`**; uninstalling restores the
   previous extent (TRY, FINALLY, LOOP, FOREACH, WITH, COLLECT, etc.).
3. **Node-owned snapshots** — continuation capture and transfer plans clone or
   reinstall nodes together with their owned **`stack`** blocks so chain and
   operand storage stay aligned across delimited transfer.

Region nodes without an owned stack (**`stack == NULL`**) inherit the current
block from an ancestor frame or enclosing extent.

### koskari_eval Return Value

`koskari_eval` returns a `KosOpResult {KosOpResultKind kind; PyObject *val}`.
Driver-facing stop kinds are `KOS_RES_RETURN`, `KOS_RES_RAISE`, `KOS_RES_YIELD`,
and `KOS_RES_AWAIT`.  `KOS_RES_RAISE` uses `PyErr` as authority (`val` is NULL).
For the others `val` carries the boundary value (new reference; caller owns it).

Internal staging of parent delivery after a callee **RETURN** does **not** add new
public kinds: opcodes return `EVAL_PARENT_RETURN(val)` (`KOS_RES_PARENT_RETURN`)
and `cg_handle_op_result` dispatches to `eval_parent_return_deliver`; the outer
driver never observes that intermediate step.

## Continuation Infrastructure

### KosContinuation — Captured Delimited Continuation

```c
struct KosContinuation {
  KosDynNode *captured_chain;   /* cloned chain segment */
  KosDynFrame *resume_frame;    /* frame to resume into */
  uint32_t resume_pc;           /* pc to resume at */
  KosStackBlock *resume_stack;  /* stack state (COW reference) */
  uint32_t prompt_id;           /* target prompt tag */
  uint32_t graft_point_id;      /* node_id of graft point for LCA fallback; 0 for capture-cc */
  uint8_t one_shot;             /* 0 = multi-shot, 1 = one-shot */
};
```

### KosTransferPlan — Continuation Transfer Execution

When a continuation is invoked, a transfer plan orchestrates the execution.
Plan and actions are a single allocation (flexible array at end). Default
capacity is 32 actions; the whole block is reallocated when more are needed.
One freed plan is cached for reuse; `koskari_dynchain_clear` frees the cache.

```c
#define KOS_XFER_PLAN_DEFAULT_ACTIONS 32

typedef enum KosTransferStep {
  KOS_XFER_EXTENT_ACTION, /* execute extent wind or unwind for a node */
  KOS_XFER_INSTALL,       /* splice target chain segment */
  KOS_XFER_INJECT,        /* push resume value onto stack */
  KOS_XFER_PARAM_RESTORE, /* restore param bindings on unwind */
} KosTransferStep;

struct KosTransferPlan {
  KosTransferPlan *suspended;   /* outer plan if nested */
  size_t action_count;          /* total actions */
  size_t action_index;          /* next action index */
  size_t action_capacity;       /* slots allocated (>= action_count) */
  KosDynNode *original_target;  /* for chain-change detection */
  uint32_t chain_version;       /* version at plan creation */
  uint32_t resume_pc;           /* UINT32_MAX = not set; set for invocation */
  KosDynFrame *resume_frame;    /* frame to resume into (invocation only) */
  KosStackBlock *resume_stack;  /* stack state at capture (invocation only) */
  PyObject *continuation_ref;   /* keep continuation alive until plan is freed */
  KosContinuation *invoke_clone;/* release when plan freed (re-invocation clone) */
  PyObject *step_awaitable;     /* when step suspended on await (owned) */
  PyObject *step_resume_inject; /* value/exception to inject on resume (owned) */
  KosTransferAction actions[];  /* flexible array; size = action_capacity */
};
```

### Dynchain source layout

`koskari/lib/dynchain.c` is the translation-unit shell. It `#include`s per-kind
units (`dynchain/dyn_*.c`), **`dynchain/dyn_walk.c`** (chain walks, pop/unwind,
transition dispatch, local raise), and **`dynchain/xfer_plan.c`** (transfer plan
build and step execution). The layout compiles to a single `dynchain.o`; the
public C API is **`dynchain.h`**.

### Transfer plan construction (`xfer_plan.c`)

Plan builders populate **`KosXferPlanOptions`** and call **`kos_build_xfer_plan`**
(static). Options fields:

| Field | Role |
|-------|------|
| `target` | Segment root for INSTALL/PUSH or plan metadata |
| `unwind_from_current` | When set, emit unwind `EXTENT_ACTION`s from `dyn_top` to `unwind_stop` |
| `unwind_stop` | Unwind bound (often LCA or prompt node) |
| `segment_op` | `KOS_XFER_INSTALL`, `KOS_XFER_PUSH`, or `0` (no segment step) |
| `with_wind` | When set, emit wind-before `EXTENT_ACTION`s from target toward `wind_lca` |
| `wind_lca` | Wind bound (LCA or `NULL` for delimited PUSH) |
| `inject_kind` | `KOS_XFER_PLAN_INJECT`, `KOS_XFER_PLAN_TRUNCATE_AND_INJECT`, or none |
| `truncate_node` | Node for `TRUNCATE_AND_INJECT` (e.g. prompt on POP_SHIFT) |
| `graft_point_id` | Stored on the plan; LCA fallback when chains are disjoint |

Public builders (`kos_build_transfer_plan`, `kos_build_return_plan`,
`kos_build_delimited_push_plan`, `kos_build_direct_install_plan`,
`kos_build_install_only_plan`, `kos_dyn_build_pop_shift_plan`) fill options and
delegate to **`kos_build_xfer_plan`**. LCA edge cases for direct install and
install-only paths use **`kos_xfer_resolve_lca`**.

### Transfer step execution (`xfer_plan.c`)

**`kos_run_transfer_step`** dispatches one plan action per call. INSTALL and PUSH
share **`xfer_step_land_segment`** (`KOS_OP_INLINE`): `graft_parent == NULL`
replaces the chain; a non-NULL `graft_parent` grafts the segment bottom onto the
current `dyn_top`. **`kos_chain_segment_bounds`** and
**`xfer_plan_apply_resume_state`** are `KOS_OP_INLINE` helpers on the step path.
INJECT delivers throw or stack push inline in **`xfer_step_inject`**.

Plan-build unwind counting and filling share **`xfer_plan_unwind_includes`**
(`static inline` predicate).

### Continuation capture (`dynchain.c`)

**`kos_shift_capture`** and **`kos_capture_cc`** call
**`kos_continuation_capture_segment`**: clone `segment_top..stop`, detach the
template bottom (**`kos_chain_detach_template`**), resolve resume frame
(**`kos_chain_first_frame`**) and stack (**`kos_chain_resume_stack`**), then
**`kos_continuation_create`**.

## Python-Facing Wrapper Types

**KosPyContinuation** (cont.c):
- Wraps undelimited `KosContinuation` (call/cc)
- Callable from Python: does not return; raises `KoskariContinuationApply` to perform upwards transfer via exception
- Multi-shot: clones captured state on each apply
- GC-tracked: traverses captured chain

**KosCoroutine** (func.c):
- Wraps `KosContext` for Python coroutine protocol
- Implements `__await__`, `send`, `throw`, `close`
- State machine: NEW → RUNNING → WAITING_PY → DONE
- Created by Expression/Function call (via `kos_run_code_as_coroutine`); drive calls `koskari_eval()`. (Continuation invocation from Python raises; it does not create a KosCoroutine.)

**KosPyAsyncGen** / **KosPyAsyncGenASend** (eval.c):
- Wraps `KosContext` for async generator protocol
- `__aiter__` returns self
- `__anext__` returns `KosPyAsyncGenASend` awaitable
- When driven from **within** a Koskari eval (e.g. `await-for-each`), the gen’s frame is **attached** to the current context and stepped in the same loop; when driven from Python (`__anext__`), `koskari_eval(agen->ctx, &ev, NULL, NULL, NULL)` is used directly (see **Drive Loop** for the full `koskari_eval` signature).

### Synchronous entry points (demand / tp_iter)

For **synchronous** Python code (e.g. tests, scripts) that does not use `asyncio`:

- **KosCoroutine.demand()**: Runs the coroutine to completion on the demand path (no asyncio). If the coroutine suspends on a Python awaitable, `demand()` raises; use `async`/`await` for that case.
- **`iter(agen)` / `for x in agen`**: `tp_iter` returns a **DemandGenerator** that drives the Koskari async generator synchronously via the evaluator (not `koskari._sync`).

Use **`asyncio.run(expr())`** or **`await expr()`** when you need the full asyncio stack.

## In-loop flattening (same koskari_eval run)

To avoid recursive C calls and keep a single state machine:

1. **CALL (Koskari callables)**: The CALL opcode invokes `kos_callable_call`, which dispatches to the callable's install slot. The install pushes a new frame (or, for TAIL_CALL, resets the current frame) and configures it for bytecode or native execution. The evaluator does **not** call Python or recurse into `koskari_eval`; it continues the dispatch loop. See [api/special-call-protocol.md](../api/special-call-protocol.md) for the install-slot design.

2. **RETURN (frame has parent, normal call return)**: When the callee’s dynamic parent is not a GEN, the RETURN opcode runs `KOS_TRANS_WALK_RETURN_TO_FRAME` via `kos_dyn_transition`, then `kos_dyn_pop_frame` so the **caller** becomes `frame_top`. It does **not** push onto the parent stack in the opcode handler. Instead it returns `EVAL_PARENT_RETURN(val)` (`KOS_RES_PARENT_RETURN` in `KosOpResult`). `cg_handle_op_result` dispatches to **`eval_parent_return_deliver`**: optional flattened Koskari async-gen iterator delivery (`kos_dyn_try_deliver_iterator_boundary_after_pop`), else native-frame handler or `KosStackBlock_PushRef` onto the parent stack, then **`kos_dynnode_on_control_returned`** (WIND/WITH). The outer `koskari_eval` caller still does not observe a completion event for this path unless the eval loop eventually stops for another reason.

3. **NEXT/ITER_NEXT (Koskari async gen)**: **`NEXT`** pushes a **`KOS_DYN_NEXT`** boundary (`kos_dyn_push_next_for_kos_agen`, …) as needed, then **attaches** the generator’s frame with **`op_attach_koskari_agen_frame(es, …)`** (live **`KosEvalState *es`** from the opcode). **`ITER_NEXT`** records state on the innermost **`KOS_DYN_FOREACH`** region and uses **`op_attach_koskari_agen_frame_for_iter_next(es, …)`**. Each performs its own validation and failure cleanup, then **`kos_attach_agen_gen_flatten(es, agen, gen)`** → **`kos_dyn_gen_install(es, gen)`** so **`KosEvalState_RefreshDynCaches`** updates the same **`es`** the dispatch loop uses. The gen’s frame runs until it YIELD or RETURN. On YIELD/RETURN, the **frame-has-parent** rule runs: the gen frame is **detached** (without freeing), the value (or exhausted/default) is delivered to the awaiting frame, and the loop continues with the caller as `frame_top`. No recursive `koskari_eval(agen->ctx, ...)`.

4. **YIELD (top-level)**: When a top-level or otherwise surfaced YIELD returns `EVAL_YIELD(val)`, `cg_handle_op_result` routes to **`eval_event_yield`**, which performs stage A (`eval_yield_handle_walk_and_freeze`: `KOS_TRANS_WALK_HANDLE_YIELD`) and stage B (optional NEXT/FOREACH/YIELD_FROM delivery for flattened Koskari async gens via **`eval_event_yield_deliver_to_boundary`**), or tags suspension for the outer driver. In-loop child-frame YIELD (parent non-NULL) still avoids stopping `koskari_eval` with a public boundary; the exact unwind/freeze steps live in `kos_dyn_transition` / dynchain helpers.

5. **RETURN/YIELD (root / no in-loop parent delivery)**: When RETURN completes without a normal parent (or parent is GEN), or when the opcode path sets a stop reason, `koskari_eval` returns a `KosOpResult` with kind `KOS_RES_RETURN`, `KOS_RES_YIELD`, etc.

6. **Attach/detach (dynchain)**: `kos_dyn_attach_frame(ctx, frame)` reparents an existing frame onto `ctx->dyn_top` and sets it as the new top (no allocation). `kos_dyn_detach_top_frame(ctx, &frame_out)` removes the top frame from the chain without freeing it and returns it; used when the gen yields or returns so the frame can remain stored in the async gen’s context.

## Pushing results and auto-await

The evaluator **auto-awaits** only in two places:

1. **CALL (Python path)**: When the callable is a Python object and the call returns a coroutine/awaitable, the VM awaits it (eager step or suspend/resume) and pushes the final value; otherwise the call result is pushed as-is.
2. **AWAIT opcode**: The value on TOS is explicitly awaited; the VM suspends or injects the result.

All other opcodes push their result with **no** automatic await. That includes: CONST, VAR, GLOBALS, LOCALS, FAST, getattr, subscript, binary ops, yield-from yielded or final value, and any value that was already awaited or is not an awaitable. The implementation uses a push-only path (e.g. `push_result`) for every opcode except CALL's Python result and AWAIT. This avoids re-awaiting delegate results from YIELD_FROM and keeps semantics predictable.

### AWAIT resume and pending WITH exit

When `koskari_eval` returns `KOS_RES_AWAIT`, the driver (e.g. KosCoroutine.send or
the sync adapter) sends the await result and calls `koskari_eval` again. On
re-entry, **revive** runs before the next opcode is dispatched. **`revive_await`**
(in `eval.c`) pushes the await result (from `py_resume_inject` or the send value)
onto the current frame stack, then calls **`kos_dyn_complete_pending_exit(ctx, 1)`**.
That walks the dynamic chain for a `KosDynWith` whose **`phase`** is
`KOS_WITH_PHASE_PENDING_EXIT_AWAIT` (async `__aexit__` had suspended) and runs
`kos_dyn_with_on_control_returned`: it pops TOS (the `__aexit__` return), applies
swallow logic (exception and truthy ⇒ swallow), drops the WITH stack block,
optionally pops the node, sets frame PC to the post-`POP_WITH` instruction, and
pushes `None` or the body value as appropriate. Thus the with-expression value is
always `None` on swallow/normal paths; the `__aexit__` return is not left on the
stack for RETURN to use.

Wrappers: **`kos_complete_pending_with_exit(ctx)`** / **`kos_complete_pending_with_exit_semantic(ctx)`**
delegate to `kos_dyn_complete_pending_exit` with `do_pop_node` 1 or 0 (full unlink
vs semantic-only for paths where the caller unlinks). See
[with-dyn-with.md](with-dyn-with.md) for the full async exit flow.

## Public C API

### DynNode Chain Management

Chain operations dispatch on `KosDynKind` and `KOS_DYN_CAP_*` across
`dynchain.c` and the included `dynchain/dyn_*.c` units (switch-based helpers
per kind). Walk/pop/transition helpers live in **`dynchain/dyn_walk.c`**. See
[dynamic-node-unified-api.md](../archive/dynamic-node-unified-api.md) for the capability model.

**Eval state:** Mutators and chain-aware helpers exposed from `dynchain.h` take
**`KosEvalState *es`** as their first parameter (not `KosContext *ctx`). Use
**`KosEvalState_CONTEXT(es)`** when you need the heap context. After edits to
**`dyn_top`** / **`frame_top`** / operand stack ownership, call
**`KosEvalState_RefreshDynCaches(es)`** and then **`KosEvalState_SyncFromContext(es)`**
where the hot-path **`frame`** / **`code`** caches must match **`ctx`**.

Representative signatures (see **`koskari/lib/dynchain.h`** for the full set):

```c
/* Generic push/pop */
int kos_dyn_push(KosEvalState *es, KosDynNode *node);
int kos_dyn_uninstall(KosEvalState *es, KosDynNode *node,
                      KosXferEvent event);  /* kos_dyn_pop */
int kos_dyn_pop_top(KosEvalState *es, KosXferEvent event);

/* Push/pop frames */
KosDynFrame *kos_dyn_push_frame(KosEvalState *es, KosCode *code, PyObject *ns);
int kos_dyn_pop_frame(KosEvalState *es);

/* Attach/detach existing frame (no allocation/free); for in-loop async gen stepping */
int kos_dyn_attach_frame(KosEvalState *es, KosDynFrame *frame);
int kos_dyn_detach_top_frame(KosEvalState *es, KosDynFrame **frame_out);

/* Push/pop prompts (also used during POP_SHIFT truncation and exception unwind) */
KosDynPrompt *kos_dyn_push_prompt(KosEvalState *es, uint32_t prompt_id);
int kos_dyn_pop_prompt(KosEvalState *es, KosDynPrompt *prompt);

/* Wind, regions, NS, COLLECT, CC, SHIFT, … — all first-arg es */

/* Find topmost LOOP or FOREACH node (for break/continue) */
KosDynNode *kos_find_node_for_loop(KosEvalState *es);

/* Async WITH exit completion (eval.h: still takes ctx; builds es internally) */
int kos_complete_pending_with_exit(KosContext *ctx);
int kos_complete_pending_with_exit_semantic(KosContext *ctx);

/* Unwind to specific chain position (transfer-plan paths) */
int kos_dyn_unwind_to_node(KosEvalState *es, KosDynNode *target);
int kos_dyn_unwind_to_frame(KosEvalState *es, KosDynFrame *frame);
```

### Continuation Capture and Invocation

```c
/* Find prompt by tag */
KosDynPrompt *kos_find_prompt(KosEvalState *es, uint32_t prompt_id);

/* Validate capture (prompt reachability in chain) */
int kos_validate_capture(KosEvalState *es, KosDynPrompt *prompt);

/* Capture continuation (shift) */
int kos_shift_capture(KosEvalState *es, uint32_t prompt_id, uint32_t resume_pc,
                      KosContinuation **out_k);

/* Invoke continuation (continuation_ref keeps k alive until plan is freed) */
int kos_invoke_continuation(KosEvalState *es,
        KosContinuation *k, PyObject *value, int is_throw,
        PyObject *continuation_ref);

/* Free or clone continuation */
void kos_continuation_free(KosContinuation *k);
KosContinuation *kos_continuation_clone(KosContinuation *k);
```

### Transfer Planning

```c
/* Compute LCA between two chain positions; graft_point_id enables ID fallback when chains disjoint */
KosDynNode *kos_compute_lca(KosDynNode *a, KosDynNode *b, uint32_t graft_point_id);

/* Build transfer plan (and siblings: return, delimited_push, direct_install, install_only) */
int kos_build_transfer_plan(KosEvalState *es,
        KosDynNode *target, PyObject *value, int is_throw,
        uint32_t graft_point_id, KosTransferPlan **out_plan);

/* Execute next step of transfer plan */
int kos_run_transfer_step(KosEvalState *es);

/* Free transfer plan */
void kos_free_transfer_plan(KosTransferPlan *plan);

/* Suspend/resume for nested transfers */
void kos_suspend_transfer_plan(KosEvalState *es);
int kos_resume_transfer_plan(KosEvalState *es);

/* Nested delimited PUSH detection; abandon active plan + plan_stack */
int kos_xfer_plan_is_delimited_push(const KosTransferPlan *plan);
void kos_abandon_transfer_nest(KosEvalState *es);
```

### Context and Frame Lifecycle

```c
KosContext *KosContext_New(KosDynFrame *top);
void KosContext_Free(KosContext *ctx);

/* Frame helpers (wrappers around kos_dynframe_new/clone/free) */
KosFrame *kos_frame_new(KosCode *code, PyObject *ns,
        KosStackBlock *stack, KosDynNode *parent);
KosFrame *kos_frame_clone(KosFrame *frame);
void kos_frame_free(KosFrame *frame);
```

### Call Operations

Koskari-to-Koskari calls (CALL/TAIL_CALL opcodes) invoke `kos_callable_call` or `kos_callable_tail_call`, which dispatch to the callable's install slots. The install pushes a new frame (CALL) or resets the current frame (TAIL_CALL), and execution continues in the same drive loop. See [api/special-call-protocol.md](../api/special-call-protocol.md). The implementation is internal to eval.c.

### Drive Loop

```c
KosOpResult koskari_eval(KosContext *ctx,
                         PyObject *send_val, PyObject *throw_typ,
                         PyObject *throw_exc);
```

Main entry point: runs opcodes until boundary (RETURN/RAISE/YIELD/AWAIT).
Returns a `KosOpResult`: kind is `KOS_RES_RETURN`, `KOS_RES_RAISE`,
`KOS_RES_YIELD`, or `KOS_RES_AWAIT` on a normal boundary; `KOS_RES_FATAL` on
a hard internal fault (PyErr also set).

`send_val`, `throw_typ`, and `throw_exc` are the resume arguments (all may be
NULL for a fresh start or a NULL resume after YIELD). On re-entry after
suspension the appropriate `revive_*` helper is selected via
`KosContext_SUSPENSION_EVENT`.

#### Internal Phase Structure

`koskari_eval` runs shared entry/transfer setup, then dispatches to either
`koskari_eval_dispatch_computed_goto` (GCC/Clang: optable, `&&cg_TARGET_*`,
`goto *optable[op]` in one static function) or `koskari_eval_dispatch_switch`
(MSVC: `for(;;)` + `switch`).  Phase helpers 1–4 and 6 stay shared; phase 5
lives entirely inside those two dispatchers.

| Phase | Helper | Responsibility |
|-------|--------|----------------|
| 1 | `eval_phase_entry` | Entry/revive normalization; `entry_mode_dispatch` for the EXCEPTION outcome |
| 2 | `eval_phase_transfer` | Transfer-plan drain; terminal RETURN(None) when no frame or hook remains |
| 3 | `eval_phase_pending_exception` | `PyErr_Occurred()` → **`process_raise`** at each loop-top iteration |
| 4 | `KOS_EVAL_PREPARE_NEXT_INSN` | Copy insn into local `KosOp insn`, set `acting_pc`, pre-advance `pc` (assumes valid frame/PC; no checks) |
| 5 | *(inline)* | Computed-goto / switch opcode dispatch; handlers return **`KosOpResult`** via `EVAL_*` macros (see `eval.h`) |
| 6 | `cg_handle_op_result` | Switch on `op_res.kind`; delegates to `eval_event_yield`, `eval_event_await`, `eval_event_return`, `process_raise`, `eval_parent_return_deliver`, or `kos_eval_post_opcode_control_returned_after_hook` |

Phase 4 sets a local `KosOp insn` (and frame `acting_pc` / `pc`) for phase 5;
opcodes are called with `&insn`. Phase 6 (`cg_handle_op_result`) dispatches on
the `KosOpResult.kind` returned by the opcode. Phase helpers signal the loop
by returning `KosOpResult` directly — there is no separate `KosEvalStep`
intermediary.

#### Opcode result dispatch (`cg_handle_op_result`)

Opcode handlers return a **`KosOpResult {KosOpResultKind kind; PyObject *val}`**
via the `EVAL_*` macros defined in `eval.h`:

| Macro | Kind | Meaning |
|-------|------|---------|
| `EVAL_NORMAL()` | `KOS_RES_NORMAL` | Hot path: advance PC, loop without prologue |
| `EVAL_REPROLOGUE()` | `KOS_RES_REPROLOGUE` | Re-run full prologue before next opcode |
| `EVAL_RAISE()` | `KOS_RES_RAISE` | PyErr set; route to **`process_raise`** |
| `EVAL_YIELD(v)` | `KOS_RES_YIELD` | Generator yield; route to **`eval_event_yield`** |
| `EVAL_AWAIT(v)` | `KOS_RES_AWAIT` | Suspend on awaitable; route to **`eval_event_await`** |
| `EVAL_RETURN(v)` | `KOS_RES_RETURN` | Frame return; route to **`eval_event_return`** (walk/unwind/pop/deliver) |
| `EVAL_PARENT_RETURN(v)` | `KOS_RES_PARENT_RETURN` | Post-pop parent delivery; route to **`eval_parent_return_deliver`** |
| `EVAL_CONTROL_RETURNED()` | `KOS_RES_CONTROL_RETURNED` | WIND/WITH thunk completion; route to **`kos_eval_post_opcode_control_returned_after_hook`** |
| `EVAL_FATAL()` | `KOS_RES_FATAL` | Non-PyErr internal fault; `koskari_eval` returns -1 immediately |

`EVAL_NORMAL()` is handled on the hot path before `cg_handle_op_result` is
entered; `cg_handle_op_result` only runs for the non-normal cases.

**`process_raise`** is the common entry for all exception dispatch (the
unified RAISE ingress; see EVALUATOR.md §4.1): called from
`cg_handle_op_result` (opcode errors), `eval_phase_pending_exception`
(pending at prologue), `kos_eval_post_opcode_control_returned_after_hook`
(hook error path), entry revive-throw, and transfer-plan step failures.

**CONTROL_RETURNED path:** after a WIND/WITH unwind hook completes and
`kos_dynnode_on_control_returned` returns non-zero, the result is dispatched
as AWAIT, YIELD, RETURN-boundary, or PyErr by
`kos_eval_post_opcode_control_returned_after_hook`. All paths use `KosOpResult`
directly.

See [boundary-control-reemit.md](boundary-control-reemit.md) for the design intent and [../archive/kosevent-elimination.md](../archive/kosevent-elimination.md) for the archived elimination spec.

#### Exception ownership note

When `kos_dyn_try_local_raise_body` returns `EVAL_RAISE()` (no handler
found), it has injected traceback and left `PyErr` set. The
`KOS_RES_RAISE` result signals the unhandled raise to the caller;
`PyErr` is authoritative.

### GC Helpers

```c
int kos_dynnode_traverse(KosDynNode *node, visitproc visit, void *arg);
int kos_dynchain_traverse(KosDynNode *node, KosDynNode *stop,
        visitproc visit, void *arg);
int kos_continuation_traverse(KosContinuation *k, visitproc visit, void *arg);
int KosStackBlock_Traverse(KosStackBlock *block, visitproc visit, void *arg);
int KosContext_Traverse(KosContext *ctx, visitproc visit, void *arg);
```

## Ownership and Invariants

1. **Context ownership**: A `KosContext` is owned by exactly one Python
   wrapper (`KosCoroutine` or `KosPyAsyncGen`) during its lifetime.

2. **Chain ownership**: The dynamic chain is owned by the context. On
   continuation capture, chains are cloned for multi-shot semantics.

3. **Suspension**: When `koskari_eval` returns `KOS_RES_AWAIT`, the context's
   `py_await_iter` holds the iterator. Resumption injects result onto stack.

4. **Multi-shot**: Continuations clone all state on invocation, allowing
   multiple independent resumptions.

5. **GC safety**: Only Python-visible wrapper objects are GC-tracked.
   Internal C structs are traversed via wrapper `tp_traverse`.

6. **Python boundary**: The chain is per-context. Delimited capture only finds
   prompts in the current chain. Delimited invocation from Python is blocked
   at `DelimitedContinuation.tp_call` (always raises).

7. **Hook isolation**: Wind/unwind hooks execute with isolated operand stacks
   and cannot observe or modify the enclosing frame's stack.

## Wind/Unwind Ordering

On continuation transfer from `src` chain to `dst` chain:

1. **Unwind phase**: Execute `after` hooks from `src` toward LCA.
   - Order: innermost (top) to outermost (toward LCA).
   - Only WIND nodes with `entered = 1`.

2. **Install phase**: Splice `dst` chain segment onto current chain.

3. **Wind phase**: Execute `before` hooks from LCA toward `dst`.
   - Order: outermost (from LCA) to innermost (toward dst top).
   - Mark each WIND as `entered = 1` after its before hook completes.

This matches Scheme's `dynamic-wind` semantics.

## Related Documentation

- [boundary-control-reemit.md](boundary-control-reemit.md) — Staged parent delivery and eval post-opcode helpers (RETURN/YIELD/RAISE)
- [../archive/kosevent-elimination.md](../archive/kosevent-elimination.md) — Archived: KosEvent elimination (implemented)
- [../archive/evaluator-restructure.md](../archive/evaluator-restructure.md) — Design document for
  the DynNode chain restructure (implemented; archived)
- [introspection-trace.md](introspection-trace.md) — Read-only introspection
  and trace snapshot design (TraceView; proposed CAPTURE_FRAME in introspection-trace.md §5)
- [callcc.md](callcc.md) — Delimited continuation implementation details
- [execution-invariants.md](execution-invariants.md) — Execution model invariants
- [bytecode.md](bytecode.md) — Bytecode format and opcodes
- [../language/control-flow.md](../language/control-flow.md) — Language-level
  control flow including reset/shift
