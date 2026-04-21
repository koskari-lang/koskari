# Evaluation Model

This document describes the Koskari evaluator implementation in `eval.c`/`eval.h`
and the supporting DynNode chain infrastructure in `dynchain.c`/`dynchain.h`.
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
- **Eval-owned boundary staging** where frame crossing (unwind/pop) is separated from parent-consumer delivery using `KosOpOutcome` post-opcode tags and helpers in `eval.c` (no extra public `KosEventKind` values); see [boundary-control-reemit.md](boundary-control-reemit.md)

## Core VM Structures

### Dynamic Chain (DynNode)

The evaluator uses a unified **dynamic chain** that links all execution context:
frames, prompts, wind extents, and control regions. This replaces the previous
separate frame chain and handler chain.

**KosDynNode** — Single tagged-union struct for all chain node kinds. Base fields
`parent`, `kind`, `flags`, `caps`, `node_id` are followed by a union `u` with variants:
Payload variants live in union `u` (see `struct KosDynNode` in [`dynchain.h`](../../koskari/lib/dynchain.h)).
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

- **Frame** (u.frame): code (borrowed), pc (next instruction), acting_pc (instruction currently being executed), stack (owned), ns (owned), frame_flags, native_handler (optional; when non-NULL, handler is frame evaluator; see [api/special-call-protocol.md](../api/special-call-protocol.md)).
  Macros: KosDynFrame_CODE, KosDynFrame_PC, KosDynFrame_ACTING_PC, KosDynFrame_STACK, KosDynFrame_NS, KosDynFrame_FLAGS, KosDynFrame_NATIVE_HANDLER.
- **Prompt** (u.prompt): prompt_id, handler_code, handler_pc, pending_shift_wrapper (KosPyDelimitedContinuation wrapper when PUSH_SHIFT plan completed; owned).
  Macros: KosDynPrompt_ID, KosDynPrompt_HANDLER_CODE, KosDynPrompt_HANDLER_PC, KosDynPrompt_PENDING_SHIFT_WRAPPER.
- **Wind** (u.wind): before_code, before_pc, after_code, after_pc, entered.
  Macros: KosDynWind_BEFORE_CODE, KosDynWind_BEFORE_PC, KosDynWind_AFTER_CODE, KosDynWind_AFTER_PC, KosDynWind_ENTERED.
- **Try** (u.try_): handler_pc, enabled. Macros: KosDynTry_HANDLER_PC, KosDynTry_ENABLED.
- **Finally** (u.finally_): handler_pc, enabled, handling_raise (1 when entered due to exception), result (stashed return value). Macros: KosDynFinally_HANDLER_PC, KosDynFinally_ENABLED, KosDynFinally_HANDLING_RAISE, KosDynFinally_RESULT.
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
It holds `dyn_top`, `frame_top`, `globals_frame`, transfer-plan fields, delimited
continuation return targets, Python await/yield bridge (`py_await_iter`,
`py_resume_inject`, `py_resume_kind`), `pending_defer` (AWAIT on KosDefer inner),
exception inject mode, async-gen `pending_agen` / `resume_gen`, and `flags`.
Async **WITH** enter/exit suspension does **not** use separate `KosContext`
slots: state lives on the `KosDynWith` node (`phase`, `exc_*`, `normal_exit`, etc.);
see `kos_dyn_complete_pending_exit` in `dynchain.c` and `revive_await` in `eval.c`.

```c
/* Abbreviated; see eval.h for full struct KosContext */
struct KosContext {
  KosDynNode *dyn_top;
  KosDynFrame *frame_top;
  KosDynFrame *globals_frame;
  uint32_t globals_cache_version;
  KosEvaluatorToken *token;
  PyObject *domain;
  KosDynFrame *unwind_target_frame;
  PyObject *pending_return_value;
  int pending_return_to_parent;
  KosTransferPlan *plan;
  KosTransferPlan *plan_stack;
  uint32_t chain_version;
  KosDynNode *invocation_segment_top;
  KosDynNode *return_target_chain;
  uint32_t return_target_pc;
  KosDynFrame *delimited_return_frame;
  uint32_t delimited_return_pc;
  KosDynNode *delimited_return_node;
  PyObject *py_await_iter;
  PyObject *py_last_yielded;
  PyObject *py_resume_inject;
  uint8_t py_resume_kind;
  PyObject *pending_defer;
  int inject_mode;
  PyObject *inject_type;
  PyObject *inject_value;
  uint8_t suspension_event;
  void *pending_agen;
  PyObject *pending_agen_default;
  int pending_agen_n;
  KosDynGen *resume_gen;
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

### KosEvent — Boundary Event from koskari_eval

Driver-facing stop reasons are exactly these `KosEventKind` values (plus `KOS_EV_NONE` for cleared in-loop state). Internal staging of parent delivery after a callee **RETURN** does **not** add new public kinds: it uses `KosOpOutcome` with `KOS_LOP_POST` and tag `KOS_OP_POST_TAG_PARENT_RETURN` (see **Post-opcode staging** under the drive loop).

```c
typedef enum KosEventKind {
  KOS_EV_NONE = 0,  /* cleared / in-loop consume; not a driver boundary */
  KOS_EV_RETURN,    /* normal completion */
  KOS_EV_RAISE,     /* exception propagation */
  KOS_EV_YIELD,     /* generator yield */
  KOS_EV_AWAIT,     /* suspension on Python awaitable */
} KosEventKind;

typedef struct KosEvent {
  KosEventKind kind;
  PyObject *payload;   /* new reference where set; caller owns it */
  PyObject *exc_type;  /* for KOS_EV_RAISE: exception type (new ref) */
} KosEvent;
```

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

2. **RETURN (frame has parent, normal call return)**: When the callee’s dynamic parent is not a GEN, the RETURN opcode runs `KOS_TRANS_WALK_RETURN_TO_FRAME` via `kos_dyn_transition`, then `kos_dyn_pop_frame` so the **caller** becomes `frame_top`. It does **not** push onto the parent stack in the opcode handler. Instead it fills `out_ev` with `KOS_EV_RETURN` and the return value (one ref held by `out_ev`) and returns `KOS_OP_POST_PARENT_RETURN()`. The main loop then runs **`eval_parent_return_deliver`**: optional flattened Koskari async-gen iterator delivery (`kos_dyn_try_deliver_iterator_boundary_after_pop`), else native-frame handler or `KosStackBlock_PushRef` onto the parent stack, then **`kos_dynnode_on_control_returned`** (WIND/WITH). The outer `koskari_eval` caller still does not observe a completion event for this path unless the eval loop eventually stops for another reason.

3. **NEXT/ITER_NEXT (Koskari async gen)**: When the iterator is a Koskari async generator, the evaluator sets `pending_agen` and **attaches** the generator’s frame to the current context with `kos_dyn_attach_frame(ctx, gen_frame)`, then continues the loop. The gen’s frame runs until it YIELD or RETURN. On YIELD/RETURN, the **frame-has-parent** rule runs: the gen frame is **detached** (without freeing), the value (or exhausted/default) is delivered to the awaiting frame, and the loop continues with the caller as `frame_top`. No recursive `koskari_eval(agen->ctx, ...)`.

4. **YIELD (post-opcode path)**: When a **top-level** or otherwise surfaced YIELD produces `KOS_EV_YIELD` in `out_ev` and `eval_phase_post_opcode` runs, **`eval_event_yield`** performs stage A (`eval_yield_handle_walk_and_freeze`: `KOS_TRANS_WALK_HANDLE_YIELD`) and stage B (optional NEXT/FOREACH/YIELD_FROM delivery for flattened Koskari async gens via **`eval_event_yield_deliver_to_boundary`**), or tags suspension for the outer driver. In-loop child-frame YIELD (parent non-NULL) still avoids stopping `koskari_eval` with a public boundary in the same way as before; the exact unwind/freeze steps live in `kos_dyn_transition` / dynchain helpers.

5. **RETURN/YIELD (root / no in-loop parent delivery)**: When RETURN completes without a normal parent (or parent is GEN), or when the opcode path sets a stop reason, `koskari_eval` can return with `out_ev->kind` set (`KOS_EV_RETURN`, `KOS_EV_YIELD`, etc.).

6. **Attach/detach (dynchain)**: `kos_dyn_attach_frame(ctx, frame)` reparents an existing frame onto `ctx->dyn_top` and sets it as the new top (no allocation). `kos_dyn_detach_top_frame(ctx, &frame_out)` removes the top frame from the chain without freeing it and returns it; used when the gen yields or returns so the frame can remain stored in the async gen’s context.

## Pushing results and auto-await

The evaluator **auto-awaits** only in two places:

1. **CALL (Python path)**: When the callable is a Python object and the call returns a coroutine/awaitable, the VM awaits it (eager step or suspend/resume) and pushes the final value; otherwise the call result is pushed as-is.
2. **AWAIT opcode**: The value on TOS is explicitly awaited; the VM suspends or injects the result.

All other opcodes push their result with **no** automatic await. That includes: CONST, VAR, GLOBALS, LOCALS, FAST, getattr, subscript, binary ops, yield-from yielded or final value, and any value that was already awaited or is not an awaitable. The implementation uses a push-only path (e.g. `push_result`) for every opcode except CALL's Python result and AWAIT. This avoids re-awaiting delegate results from YIELD_FROM and keeps semantics predictable.

### AWAIT resume and pending WITH exit

When `koskari_eval` returns `KOS_EV_AWAIT`, the driver (e.g. KosCoroutine.send or
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

Chain operations dispatch on `KosDynKind` and `KOS_DYN_CAP_*` in `dynchain.c`
(switch-based helpers per kind). See
[dynamic-node-unified-api.md](../archive/dynamic-node-unified-api.md) for the capability model.

```c
/* Generic push/pop */
int kos_dyn_push(KosContext *ctx, KosDynNode *node);
int kos_dyn_pop(KosContext *ctx, KosDynNode *node, KosEvent *out_ev);
int kos_dyn_pop_top(KosContext *ctx, KosEvent *out_ev);  /* pops top; fails for BARRIER nodes */

/* Push/pop frames */
KosDynFrame *kos_dyn_push_frame(KosContext *ctx, KosCode *code, PyObject *ns);
int kos_dyn_pop_frame(KosContext *ctx);

/* Attach/detach existing frame (no allocation/free); for in-loop async gen stepping */
int kos_dyn_attach_frame(KosContext *ctx, KosDynFrame *frame);
int kos_dyn_detach_top_frame(KosContext *ctx, KosDynFrame **frame_out);

/* Push/pop prompts (also used during POP_SHIFT truncation and exception unwind) */
KosDynPrompt *kos_dyn_push_prompt(KosContext *ctx, uint32_t prompt_id);
void kos_dyn_pop_prompt(KosContext *ctx, KosDynPrompt *prompt);

/* Push/pop wind extents */
KosDynWind *kos_dyn_push_wind(KosContext *ctx,
        KosCode *before_code, uint32_t before_pc,
        KosCode *after_code, uint32_t after_pc);
int kos_dyn_pop_wind(KosContext *ctx, KosDynWind *wind);

/* Push/pop region kinds (TRY, FINALLY, LOOP, FOREACH) */
KosDynNode *kos_dyn_push_try(KosContext *ctx, uint32_t handler_pc);
KosDynNode *kos_dyn_push_finally(KosContext *ctx, uint32_t handler_pc);
KosDynNode *kos_dyn_push_loop(KosContext *ctx, uint32_t break_pc, uint32_t continue_pc);
KosDynNode *kos_dyn_push_foreach(KosContext *ctx, uint32_t break_pc, uint32_t continue_pc);
void kos_dyn_uninstall_handler(KosContext *ctx, KosDynNode *node);

/* Find topmost enabled TRY or FINALLY node (for exception handling) */
KosDynNode *kos_find_node_for_raise(KosContext *ctx);

/* Find topmost LOOP or FOREACH node (for break/continue) */
KosDynNode *kos_find_node_for_loop(KosContext *ctx);

/* Async WITH exit completion */
int kos_complete_pending_with_exit(KosContext *ctx);
int kos_complete_pending_with_exit_semantic(KosContext *ctx);

/* Unwind to specific chain position (transfer-plan paths) */
int kos_dyn_unwind_to_node(KosContext *ctx, KosDynNode *target, KosEvent *out_ev);
int kos_dyn_unwind_to_frame(KosContext *ctx, KosDynFrame *frame, KosEvent *out_ev);
```

### Continuation Capture and Invocation

```c
/* Find prompt by tag */
KosDynPrompt *kos_find_prompt(KosContext *ctx, uint32_t prompt_id);

/* Validate capture (prompt reachability in chain) */
int kos_validate_capture(KosContext *ctx, KosDynPrompt *prompt);

/* Capture continuation (shift) */
int kos_shift_capture(KosContext *ctx, uint32_t prompt_id,
        KosContinuation **out_k);

/* Invoke continuation (continuation_ref keeps k alive until plan is freed) */
int kos_invoke_continuation(KosContext *ctx,
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

/* Build transfer plan */
int kos_build_transfer_plan(KosContext *ctx,
        KosDynNode *target, PyObject *value, int is_throw,
        uint32_t graft_point_id, KosTransferPlan **out_plan);

/* Execute next step of transfer plan */
int kos_run_transfer_step(KosContext *ctx);

/* Free transfer plan */
void kos_free_transfer_plan(KosTransferPlan *plan);

/* Suspend/resume for nested transfers */
void kos_suspend_transfer_plan(KosContext *ctx);
int kos_resume_transfer_plan(KosContext *ctx);
```

### Context and Frame Lifecycle

```c
KosContext *kos_context_new(KosDynFrame *top);
void kos_context_free(KosContext *ctx);

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
int koskari_eval(KosContext *ctx, KosEvent *out_ev,
                 PyObject *send_val, PyObject *throw_typ, PyObject *throw_exc);
```

Main entry point: runs opcodes until boundary (RETURN/RAISE/YIELD/AWAIT).
Returns 0 on a normal boundary event (kind is set in `*out_ev`) or -1 on a
fatal internal error (PyErr set, `out_ev` may be inconsistent).

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
| 1 | `eval_phase_entry` | Entry/revive normalization; `entry_mode_dispatch` for EXCEPTION/RAISE outcomes |
| 2 | `eval_phase_transfer` | Transfer-plan drain; terminal RETURN(None) when no frame or hook remains |
| 3 | `eval_phase_pending_exception` | `PyErr_Occurred()` → **`eval_event_raise`** (→ **`eval_boundary_raise_dispatch`**) at each loop-top iteration |
| 4 | `KOS_EVAL_PREPARE_NEXT_INSN` | Fetch insn into `*insn`, set `acting_pc`, pre-advance `pc` (assumes valid frame/PC; no checks) |
| 5 | *(inline)* | Computed-goto / switch opcode dispatch; handlers return **`KosOpOutcome`** (`KosLoopOpMode` + optional post tag) |
| 6 | `eval_phase_post_opcode` | When mode is `KOS_LOP_POST`, map tag via **`kos_op_post_tag_to_res`** to legacy `op_res` and run post-opcode logic (see below) |

Each helper returns a `KosEvalStep` value:

```c
typedef enum KosEvalStep {
  KOS_STEP_CONTINUE,      /* phase completed; next prologue step or prepare */
  KOS_STEP_REPROLOGUE,    /* restart full prologue (xfer, pending, native) */
  KOS_STEP_STOP_EVENT,    /* out_ev is filled; return to caller (eval_ret = 0) */
  KOS_STEP_FATAL_ERROR,   /* PyErr set; return to caller (eval_ret = -1) */
} KosEvalStep;
```

Phase 4 sets a local `KosOp *insn` (and frame `acting_pc` / `pc`) for phase 5. Phase 6 (**`eval_phase_post_opcode`**) takes `ctx`, `out_ev`, and mapped `op_res`; it does not receive insn.

#### Post-opcode staging (`eval_phase_post_opcode`)

Opcode handlers signal the loop with **`KosOpOutcome`**: `KOS_LOP_RUN` (tail dispatch), `KOS_LOP_PROLOGUE` (full prologue next cycle), `KOS_LOP_POST` (run post-opcode with a **post tag**), or `KOS_LOP_FAIL`. For `KOS_LOP_POST`, **`kos_op_post_tag_to_res`** maps the tag to the integer passed to **`eval_phase_post_opcode`**:

| Tag / res | Meaning |
|-----------|---------|
| `1` | Suspend / async-style completion (`KOS_OP_POST_TAG_SUSPEND`); `out_ev` must carry the stop reason |
| `KOS_OP_RES_CONTROL_RETURNED` (2) | Run **`kos_dynnode_on_control_returned`** (WIND/WITH thunk completion, etc.) |
| `-1` | Error path (`KOS_OP_POST_TAG_ERR`); usually paired with `PyErr` |
| `KOS_OP_RES_PARENT_RETURN_DELIVER` (4) | **`eval_parent_return_deliver`**: after RETURN’s `kos_dyn_pop_frame`, deliver return value to parent (iterator boundary try, native handler or stack push, then **`kos_dynnode_on_control_returned`**) |

Order inside **`eval_phase_post_opcode`**: handle **parent return** (`op_res == 4`) first, then the **control-returned** hook (`op_res == 2`), then **`eval_phase_post_opcode_tail`** (PyErr → **`eval_event_raise`** → **`eval_boundary_raise_dispatch`**, `KOS_EV_NONE` check, AWAIT/YIELD/RETURN boundary branches such as **`eval_event_yield`** / **`eval_event_return_deliver_to_boundary`**).

**`PyErr_Occurred()`** in that tail path routes through **`eval_boundary_raise_dispatch`** (wraps **`process_raise`**), not a separate ad-hoc normalizer. **`eval_event_raise`** is the thin entry used from **`eval_phase_post_opcode`** and **`eval_phase_pending_exception`**.

See [boundary-control-reemit.md](boundary-control-reemit.md) for the design intent and [event-driven-eval-event-handlers.md](event-driven-eval-event-handlers.md) for handler-centric event flow.

#### Exception ownership note

When `try_local_exception_handler` returns 0 (no handler found), it has
already consumed the exception via `PyErr_Fetch`, stored the instance in
`out_ev->payload`, and cleared PyErr. Callers must not call `PyErr_Fetch`
again afterward.

### GC Helpers

```c
int kos_dynnode_traverse(KosDynNode *node, visitproc visit, void *arg);
int kos_dynchain_traverse(KosDynNode *node, KosDynNode *stop,
        visitproc visit, void *arg);
int kos_continuation_traverse(KosContinuation *k, visitproc visit, void *arg);
int kos_stackblock_traverse(KosStackBlock *block, visitproc visit, void *arg);
int kos_frame_traverse(KosFrame *frame, visitproc visit, void *arg);
int kos_context_traverse(KosContext *ctx, visitproc visit, void *arg);
```

## Ownership and Invariants

1. **Context ownership**: A `KosContext` is owned by exactly one Python
   wrapper (`KosCoroutine` or `KosPyAsyncGen`) during its lifetime.

2. **Chain ownership**: The dynamic chain is owned by the context. On
   continuation capture, chains are cloned for multi-shot semantics.

3. **Suspension**: When `koskari_eval` returns `KOS_EV_AWAIT`, the context's
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
- [event-driven-eval-event-handlers.md](event-driven-eval-event-handlers.md) — Event-driven eval handlers and `out_ev` contracts
- [../archive/evaluator-restructure.md](../archive/evaluator-restructure.md) — Design document for
  the DynNode chain restructure (implemented; archived)
- [introspection-trace.md](introspection-trace.md) — Read-only introspection
  and trace snapshot design (TraceView; proposed CAPTURE_FRAME in introspection-trace.md §5)
- [callcc.md](callcc.md) — Delimited continuation implementation details
- [execution-invariants.md](execution-invariants.md) — Execution model invariants
- [bytecode.md](bytecode.md) — Bytecode format and opcodes
- [../language/control-flow.md](../language/control-flow.md) — Language-level
  control flow including reset/shift
