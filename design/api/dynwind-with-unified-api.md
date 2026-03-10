# Design Proposal: Unified Dynwind/With/Dyn-With API

**Status:** Partially implemented (dyn-with enter call path unified 2025-03-10)
**Date:** 2025-03-10

## Implementation status (2025-03-10)

- **kos_dyn_dyn_with_wind** now uses `kos_callable_vcall(ctx, out_ev, enter_fn, NULL, 0, NULL)`.
  `out_ev` is threaded through `kos_dyn_wind`, `kos_dyn_install`, `xfer_step_install`, `xfer_step_push`, and `kos_run_transfer_step`.
  Async enter during transfer (INSTALL or PUSH wind) propagates `KOS_DYN_UNWIND_ASYNC_STARTED` to `KOS_XFER_SUSPENDED_AWAIT`.
- **PUSH_WITH / PUSH_DYN_WITH** already use `kos_callable_vcall` and provisional-node + `kos_dyn_with_on_enter_control_returned`.
- **kos_dyn_with_unwind** uses `kos_callable_call`; exit pending state lives on the node.

## Summary

Unify the implementation of dynamic extent handlers (dynwind, with, dyn-with) so that all three use the same call path (kos_callable_*) and a common "control returned to node" completion model. State for pending enter/exit moves from the context onto the node where possible. Runtime behavior remains unchanged: binding from __enter__ result, exception swallowing during __exit__, auto-async, expression value always None.

---

## Motivation

**Current asymmetry:**

- **Dynwind** uses `kos_callable_vcall` / `kos_callable_call` for before/after thunks. State lives on the node (`KosDynWind_PHASE`). Completion is signaled by RETURN → `KOS_OP_RES_CONTROL_RETURNED` → `kos_dynnode_on_control_returned`. Both sync and async paths converge on this hook.
- **With / dyn-with** use `PyObject_CallNoArgs` / `PyObject_CallFunctionObjArgs` for enter/exit. Pending state lives on the context (`pending_with_enter`, `pending_with_exit`). Completion is driven by opcode re-dispatch (enter) or revive (exit).

**Problems:**

1. KosCallable enter/exit (e.g. KosFunction) are bypassed and run via Python call machinery, losing VM-native execution and the control-returned convergence.
2. Context holds duplicate "pending" structures for with; the dynwind model keeps state on the node.
3. Multiple completion paths (opcode re-dispatch vs revive vs post_opcode) make the system harder to reason about and extend.

---

## Runtime Features (Preserved)

These behaviors must remain exactly as they are:

| Feature | Description |
|---------|-------------|
| Enter binding | Optional `name` binds the enter result into the current namespace. |
| Exception swallowing | `exit(exc_type, exc_val, tb)`; truthy return means swallow. |
| Normal exit | `exit(None, None, None)`; result ignored except for swallow. |
| Expression value | The with-expression value is always `None`; exit return is never the with value. |
| Auto-async | Enter/exit may return awaitables; the VM flattens them (await) transparently. |
| Stack block | Structural push/pop; no height-mark arithmetic. |
| Single-shot vs multi-shot | `with` is single-shot; `dyn-with` participates in transfer plans. |

---

## Current Architecture (Detailed)

### Dynwind

| Component | Location | Behavior |
|-----------|----------|----------|
| Wind before | `kos_dyn_wind_wind` | Sets `KosDynWind_PHASE = PRE_BEGIN`; calls `kos_callable_vcall(ctx, NULL, before_thunk, NULL, 0, NULL)`. Returns `KOS_DYN_UNWIND_ASYNC_STARTED`. |
| Wind after | `kos_dyn_wind_unwind` | Sets `KosDynWind_PHASE = PRE_AFTER`; calls `kos_callable_call(ctx, NULL, after_thunk, args, NULL)`. Returns `KOS_DYN_UNWIND_ASYNC_STARTED`. |
| Completion | `kos_dyn_wind_on_control_returned` | Called from `kos_dynnode_on_control_returned`. Checks phase (PRE_BEGIN → POST_BEGIN, PRE_AFTER → pop node). |
| State | Node | `KosDynWind_PHASE`, thunks, `entered`. No context stash. |

Thunk runs as a normal frame. When it returns, RETURN opcode returns `KOS_OP_RES_CONTROL_RETURNED`; `eval_phase_post_opcode` calls `kos_dynnode_on_control_returned`. If the thunk awaited, the same frame resumes; when it eventually returns, the same path.

### With enter (current)

| Component | Location | Behavior |
|-----------|----------|----------|
| PUSH_WITH / PUSH_DYN_WITH | `opcode/PUSH_WITH.c`, `PUSH_DYN_WITH.c` | `kos_callable_vcall(ctx, out_ev, enter, NULL, 0, NULL)`. Provisional node in PENDING_ENTER_AWAIT; completion via `kos_dyn_with_on_enter_control_returned`. |
| kos_dyn_dyn_with_wind | `eval.c` | `kos_callable_vcall(ctx, out_ev, enter_fn, NULL, 0, NULL)` for transfer-plan wind. Returns KOS_DYN_UNWIND_ASYNC_STARTED on suspend. out_ev threaded via kos_dyn_wind, kos_dyn_install, xfer_step_install, xfer_step_push. |
| Completion | `kos_dyn_with_on_enter_control_returned` | Called from `kos_dynnode_on_control_returned` when frame parent is WITH in PENDING_ENTER_AWAIT. Pops TOS (enter result), stores to name, sets entered=1, phase=IDLE. |
| State | Node | `KOS_WITH_PHASE_PENDING_ENTER_AWAIT` on provisional node; no context stash. |

### With exit (current)

| Component | Location | Behavior |
|-----------|----------|----------|
| kos_dyn_with_unwind | `dynchain.c` | `PyObject_CallFunctionObjArgs(exit_fn, typ, val, tb, NULL)`. If awaitable, stashes `pending_with_exit` on context, returns awaitable. |
| kos_with_exit_start_await | `eval.c` | Calls `push_or_await(ctx, out_ev, awaitable)`. |
| Completion | `kos_complete_pending_with_exit` | Called from `revive_await` when `KosContext_PENDING_WITH_EXIT_NODE(ctx)` set. Pops TOS (exit result), applies swallow, drops block, pops node. |
| State | Context | `pending_with_exit`: with_node, exc_typ, exc_val, exc_tb, normal_exit. |

---

## Proposed Changes

### 1. Use flattening call for enter and exit

Replace all `PyObject_CallNoArgs` / `PyObject_CallFunctionObjArgs` with the kos_callable entry points:

| Call site | Current | Proposed |
|-----------|---------|----------|
| PUSH_WITH / PUSH_DYN_WITH | `PyObject_CallNoArgs(enter)` | `kos_callable_vcall(ctx, out_ev, enter, NULL, 0, NULL)` |
| kos_dyn_dyn_with_wind | `PyObject_CallNoArgs(enter_fn)` | `kos_callable_vcall(ctx, out_ev, enter_fn, NULL, 0, NULL)` |
| kos_dyn_with_unwind | `PyObject_CallFunctionObjArgs(exit_fn, typ, val, tb, NULL)` | `kos_callable_call(ctx, out_ev, exit_fn, args_tuple, NULL)` where `args_tuple = (typ, val, tb)` |

**Return handling:** `kos_callable_vcall` and `kos_callable_call` push the result (or run await via `push_or_await` internally). Return 0 = continue (result on stack); 1 = suspend; -1 = error.

### 2. Extend kos_dynnode_on_control_returned for WITH

Currently `kos_dynnode_on_control_returned` dispatches only for `KOS_DYN_WIND`. Extend to handle `KOS_DYN_WITH` and `KOS_DYN_DYN_WITH` when the node is in a "pending exit" phase. For exit, the semantics are the same as `kos_complete_pending_with_exit_impl`: pop TOS (exit result), apply swallow, drop block, optionally pop node.

For enter, the node does not exist until enter succeeds. Pending enter must remain on context or use a different mechanism (see §6).

### 3. Move pending exit state onto the node

Add to `KosDynWith` (or a phase enum) fields for "awaiting exit result":

- `phase` (or equivalent): `IDLE`, `PENDING_EXIT_AWAIT`
- `exc_typ`, `exc_val`, `exc_tb` (owned refs while pending)
- `normal_exit` (1 = exit(None,None,None), 0 = exception)

The context `pending_with_exit` becomes unnecessary. On suspend, the node holds the state. On revive or RETURN, `kos_dynnode_on_control_returned` is invoked with the with node as DYN_TOP (or the node is reachable via DYN_TOP's parent chain).

**Edge case:** After exit returns an awaitable and we suspend, the with node is still DYN_TOP's parent (we have not popped it). On resume, `revive_await` pushes the value. We need to recognize "control returned to the with node" and call the completion logic. That can be done by: (a) having revive check whether DYN_TOP's parent is a with node in PENDING_EXIT_AWAIT and, if so, call `kos_dynnode_on_control_returned` with that node; or (b) keeping a minimal context pointer `pending_with_exit_node` (just the node, no exc_* stash) so revive knows which node to complete. The exc_* refs live on the node.

### 4. Pending enter: context vs node

Enter runs *before* the WITH node exists. We cannot store pending enter on the node. Options:

- **A. Keep pending_with_enter on context.** Minimal change; enter completion stays as opcode re-dispatch. Only exit moves to the node-based model.
- **B. Provisional WITH node.** Push a WITH node in state `PENDING_ENTER_AWAIT` (mgr, enter, exit, name, resume_pc stored) before calling enter. If enter suspends, the node exists and holds the pending state. On control return (resume or RETURN), `kos_dynnode_on_control_returned` handles it. This fully unifies enter and exit.

Recommendation: Start with **A** to limit scope; **B** can be a follow-up.

### 5. Call flow summary

**Enter (PUSH_WITH / PUSH_DYN_WITH):**

1. Resolve enter/exit from mgr.
2. Call `kos_callable_vcall(ctx, out_ev, enter, NULL, 0, NULL)`.
3. If return 1 (suspend): stash `pending_with_enter` on context as now; return 1. On resume, opcode re-dispatches, `kos_complete_pending_with_enter` runs.
4. If return 0: result is on stack. Pop it, pass to `finalize_with_enter_result` (or equivalent). Push block, push node.

**Exit (kos_dyn_with_unwind):**

1. Fetch exception info (typ, val, tb) or use (None, None, None).
2. Build `args = PyTuple_Pack(3, typ, val, tb)`.
3. Call `kos_callable_call(ctx, out_ev, exit_fn, args, NULL)`. Pass `out_ev` so suspend can be signaled.
4. If return 1: Set node phase to PENDING_EXIT_AWAIT; store typ, val, tb, normal_exit on node. Set `KosContext_PENDING_WITH_EXIT_NODE(ctx) = node` (or equivalent) so revive knows which node to complete. Return `KOS_DYN_UNWIND_ASYNC_STARTED` with the awaitable.
5. If return 0: Result on stack. Pop it, apply swallow logic, drop block, return 0 (caller pops node) or perform full completion.

**Completion (control returned):**

- **Via RETURN (KosCallable exit ran as frame):** post_opcode sees `KOS_OP_RES_CONTROL_RETURNED`, calls `kos_dynnode_on_control_returned(ctx, KosContext_DYN_TOP(ctx), out_ev)`. For WITH in PENDING_EXIT_AWAIT, the with node is DYN_TOP's parent (the exit frame was a child). The dispatcher must walk or check: when the top frame's parent is a with node in PENDING_EXIT_AWAIT, invoke the WITH completion logic with that node. The return value is on the frame stack below the returning frame.
- **Via revive (Plain Python exit returned awaitable):** `revive_await` pushes send_val. If `KosContext_PENDING_WITH_EXIT_NODE(ctx)` is set, call `kos_dynnode_on_control_returned(ctx, node, out_ev)` or a WITH-specific completion that pops TOS and applies swallow. This reuses the same completion logic.

---

## API Changes (Detailed)

### kos_callable_* (no change)

Existing entry points are used as-is: `kos_callable_vcall`, `kos_callable_call`. They already handle KosCallable (install) vs Plain Python (PyObject_Call + push_or_await).

### kos_dyn_with_unwind

**Current signature:**
```c
int kos_dyn_with_unwind(KosContext *ctx, KosDynWith *node, PyObject **out_awaitable);
```

**Proposed behavior:** Use `kos_callable_call` instead of `PyObject_CallFunctionObjArgs`. When exit returns an awaitable, set node phase to PENDING_EXIT_AWAIT and store exc_typ, exc_val, exc_tb, normal_exit on the node. Set context `pending_with_exit_node` (or equivalent) so revive can find the node. Return `KOS_DYN_UNWIND_ASYNC_STARTED` and set `*out_awaitable`.

**New/updated node fields** (KosDynWith or equivalent):

| Field | Type | Purpose |
|-------|------|---------|
| phase | uint8_t | IDLE, PENDING_EXIT_AWAIT |
| exc_typ | PyObject * | Exception type (owned) while pending |
| exc_val | PyObject * | Exception value (owned) |
| exc_tb | PyObject * | Traceback (owned) |
| normal_exit | int | 1 = exit(None,None,None) |

These can live in unused space or extend the `u.with` struct. `phase` may alias or replace use of context `pending_with_exit` discriminator.

### kos_dynnode_on_control_returned

**Current:** Dispatches by `KosDynNode_KIND`; handles only `KOS_DYN_WIND`.

**Proposed:** Add cases for `KOS_DYN_WITH` and `KOS_DYN_DYN_WITH`. When the node has phase PENDING_EXIT_AWAIT, pop the return value from the appropriate stack (frame below the returning frame, or current stack if invoked from revive), apply swallow logic, drop block, pop node. Return 0 (continue) or propagate error.

**Signature unchanged:**
```c
int kos_dynnode_on_control_returned(KosContext *ctx, KosDynNode *node, KosEvent *out_ev);
```

### kos_dyn_dyn_with_wind

**Current:** `PyObject_CallNoArgs(enter_fn)`; no suspend support (transfer plan may handle await at a higher level).

**Proposed:** `kos_callable_vcall(ctx, out_ev, enter_fn, NULL, 0, NULL)`. If return 1, the transfer plan must handle suspend (existing `KOS_XFER_SUSPENDED_AWAIT` mechanism). If return 0, pop result and apply binding. The transfer plan's enter step may need to be updated to expect the result on the stack and handle suspend.

### PUSH_WITH / PUSH_DYN_WITH opcodes

**Current:** `PyObject_CallNoArgs(enter)`; then `kos_push_with_after_enter` with result.

**Proposed:** Replace the call with `kos_callable_vcall(ctx, out_ev, enter, NULL, 0, NULL)`. If return 1, keep current `pending_with_enter` stashing and return 1. If return 0, pop result from stack and pass to `kos_push_with_after_enter`. No change to `kos_push_with_after_enter` or `finalize_with_enter_result` semantics.

### revive_await

**Current:** If `KosContext_PENDING_WITH_EXIT_NODE(ctx)`, calls `kos_complete_pending_with_exit(ctx)`.

**Proposed:** Same check. Call `kos_dynnode_on_control_returned(ctx, KosContext_PENDING_WITH_EXIT_NODE(ctx), out_ev)` or keep `kos_complete_pending_with_exit` as a thin wrapper that delegates to the node-based logic. The node holds exc_typ, exc_val, exc_tb; the value to pop is TOS (the revived send value).

### Context fields

**pending_with_enter:** Retained (per §4). No change to structure.

**pending_with_exit:** Reduced or removed. We need either (a) only `pending_with_exit_node` to know which node to complete on revive, or (b) the node holds all state and we discover the pending node by inspecting DYN_TOP's parent. Option (a) is simpler: keep `KosContext_PENDING_WITH_EXIT_NODE(ctx)`; exc_* move to the node.

---

## Node Payload Extension (KosDynWith)

The `u.with` struct currently has: mgr, enter, exit, entered, name, resume_pc.

**Add:**

```c
uint8_t phase;           /* KOS_WITH_PHASE_IDLE, KOS_WITH_PHASE_PENDING_EXIT_AWAIT */
PyObject *exc_typ;       /* owned; only when phase == PENDING_EXIT_AWAIT */
PyObject *exc_val;
PyObject *exc_tb;
int normal_exit;         /* 1 = exit(None,None,None) */
```

Cloning (dyn-with): When phase is PENDING_EXIT_AWAIT, we should not be cloning; the node is in the middle of an async exit. Document as invariant: do not capture/transfer while with exit is pending.

---

## File-by-File Change Scope

| File | Changes |
|------|---------|
| `koskari/lib/dynchain.h` | Add phase, exc_typ, exc_val, exc_tb, normal_exit to u.with or document extension. Add `KOS_WITH_PHASE_*` enum if needed. |
| `koskari/lib/dynchain.c` | `kos_dyn_with_unwind`: use `kos_callable_call`; set node phase and exc_* on suspend. Update clone/traverse for new fields. |
| `koskari/lib/eval.c` | PUSH_WITH path: use `kos_callable_vcall` for enter. `kos_dyn_dyn_with_wind`: use `kos_callable_vcall`. `revive_await`: call node-based completion. `kos_complete_pending_with_exit`: delegate to node logic or inline. |
| `koskari/lib/eval.h` | Possibly simplify `pending_with_exit` to only `with_node`; document that exc_* live on node. |
| `koskari/lib/opcode/PUSH_WITH.c` | Replace `PyObject_CallNoArgs(enter)` with `kos_callable_vcall(ctx, out_ev, enter, NULL, 0, NULL)`. Pop result and pass to `kos_push_with_after_enter` on success. |
| `koskari/lib/opcode/PUSH_DYN_WITH.c` | Same as PUSH_WITH. |
| `koskari/lib/opcode/POP_WITH.c` | No change to control flow; `kos_dyn_with_unwind` and `kos_with_exit_start_await` behave differently internally. |
| `koskari/lib/opcode/POP_DYN_WITH.c` | Same as POP_WITH. |

---

## Migration and Testing

1. **Unit tests:** Existing with tests (sync and async) in `tests/lib/opcode/test_with.py` and `tests/compile/specials/test_with.py` must pass unchanged.
2. **KosCallable enter/exit:** Add tests where enter/exit are KosFunction to verify they run as VM frames and complete correctly.
3. **Incremental implementation:**
   - Phase 1: Switch enter/exit calls to kos_callable_* only. Keep context pending state. Verify all tests pass.
   - Phase 2: Move pending exit state onto the node; extend `kos_dynnode_on_control_returned`. Simplify context.
   - Phase 3 (optional): Provisional node for pending enter to fully unify.

---

## References

- [special-call-protocol.md](special-call-protocol.md) — kos_callable_call, kos_callable_vcall, install slots, push_or_await
- [extent-wind-unwind.md](../interpreter/extent-wind-unwind.md) — unified wind/unwind model, KOS_DYN_UNWIND_ASYNC_STARTED
- [with-dyn-with.md](../interpreter/with-dyn-with.md) — context manager semantics, exit convention, transfer rules
- [EVALUATOR.md](../EVALUATOR.md) — post_opcode, KOS_OP_RES_CONTROL_RETURNED, kos_dynnode_on_control_returned
