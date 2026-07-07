# Generic DynNode Protocol

This document describes the design ideology of the Koskari dynamic chain interface and provides a reference for all generic calls versus type-specific bypasses. It complements the archived migration spec [dynamic-node-unified-api.md](../archive/dynamic-node-unified-api.md) and [extent-wind-unwind.md](extent-wind-unwind.md).

---

## Design ideology

The dynamic chain is a linked structure of `KosDynNode` objects representing extents (try/finally, loops, with, wind, prompts, etc.). The design separates two layers:

1. **Generic interface**: Code that operates on the chain without knowing node types. It works with `KosDynNode *`, calls generic APIs, and never branches on `KosDynWith`, `KosDynWind`, etc. The evaluator core (eval.c), transfer plans, and most opcode logic use only the generic interface.

2. **Type-specific bypass**: Code that explicitly creates, installs, or acts on a particular node type because it is implementing a specific opcode or feature. Opcodes such as PUSH_WITH, PUSH_LOOP, POP_PROMPT are designed for a specific extent kind; they must call the type-specific push/pop APIs to install or tear down that extent correctly.

**Principle**: eval.c and generic callers must remain type-agnostic. They never call `kos_dyn_with_unwind`, `kos_dyn_param_wind`, or any other type-named function. Type dispatching lives entirely inside the dynchain translation unit as switch/case on `KosDynKind`; the per-kind implementations are `static` functions in the `dynchain/dyn_*.c` fragments included by `dynchain.c`. The generic caller sees only `KosDynNode *` and operations like "pop top", "unwind to frame", "offer this event".

All generic entry points take the live `KosEvalState *es` (not a raw context) and, where control transfer is involved, a `KosXferEvent` describing why the operation is running (RETURN, RAISE, BREAK, ...). Operations that can suspend return `KosOpResult`: `EVAL_NORMAL`, `EVAL_RAISE`, `EVAL_AWAIT` (async extent action started; carries the awaitable), or `EVAL_REPROLOGUE` (a thunk frame was pushed; re-enter the dispatch prologue).

---

## Generic API reference

### Chain topology

| Function | Purpose |
|----------|---------|
| `kos_dyn_push(es, node)` | Push a pre-allocated node onto the chain. Runs `kos_dyn_install`. Used internally by per-type push helpers. |
| `kos_dyn_push_noinstall(es, node)` | Push without the install phase (region nodes, CC markers). |
| `kos_dyn_push_clone(es, node)` | Clone node and push. Used by transfer plans when installing a captured segment. |
| `kos_dyn_pop_nonbarrier_top(es, event)` | Pop the top node; error if top is a BARRIER (PROMPT). The default pop. |
| `kos_dyn_pop_top_allow_barrier(es, event)` | Pop the top node, barriers included. Prompt-crossing unwind/transfer paths only. |
| `kos_dyn_pop_top(es, event)` | Non-barrier pop that maps AWAIT to REPROLOGUE (drops the awaitable). Used by POP_WIND. |

All three pop entry points funnel into the static `kos_dyn_uninstall_core`: run `kos_dyn_uninstall_handler` (kind-dispatched teardown), then unlink via `kos_dyn_commit_dyn_top` and free. When teardown suspends (AWAIT/REPROLOGUE), the node stays linked so the suspended action can resume against it. Walk loops that speak the int unwind protocol (`0` / `-1` / `KOS_DYN_UNWIND_ASYNC_STARTED`) convert pop results with `kos_map_pop_result`.

### Unwind and transfer

| Function | Purpose |
|----------|---------|
| `kos_dyn_unwind_to_frame(es, frame, event)` | Unwind from dyn_top down to the given frame, running extent teardown (wind after, with exit, etc.). Used by return/exception propagation and transfer plans. |
| `kos_dyn_unwind_to_node(es, target, event)` | Same but stop at an arbitrary node; pops barriers (deliberately crosses prompt boundaries). |
| `kos_dyn_install(es, node)` | Install-phase setup when a node is pushed. Dispatches per kind. |
| `kos_dyn_wind(es, node)` | Run wind (enter) for extent nodes during install or continuation transfer. AWAIT for async WITH enter, REPROLOGUE when a WIND before-thunk frame was started. |
| `kos_dyn_unwind(es, node, event)` | Run unwind (exit) for extent nodes. AWAIT when WITH `__exit__` returned an awaitable, REPROLOGUE when a WIND after-thunk frame was started. |
| `kos_dyn_exception_unwind(es, node)` | Exception-specific unwind when propagation reaches a node with special raise behavior (PROMPT unlink, DELIM pop). |
| `kos_dyn_handle(es, node, event)` | Offer a control transit to a node without unlinking: PASS, CONSUMED, DEFERRED, or ERROR. The central transit-rules table (TRY/FINALLY consumption, GEN boundary, PROMPT shift). |
| `kos_dyn_clone(node)` | Allocate a copy of the node for continuation capture. NULL if the kind forbids cloning. |

### Control-returned hook

| Function | Purpose |
|----------|---------|
| `kos_dynnode_on_control_returned(es, node)` | Called when control returns to a frame (e.g. after a wind thunk, or revive from AWAIT). Dispatches per kind: WIND advances phase, WITH completes a pending enter/exit. Eval calls this with `KosEvalState_CurrentNode(es)`; it never needs to know the node kind. |

### Pending-exit (generic)

| Function | Purpose |
|----------|---------|
| `kos_dyn_find_pending_exit(node)` | Walk chain from node toward root; return first node with a pending exit. Currently only WITH (PENDING_EXIT_AWAIT) has this. Returns `KosDynNode *` (caller stays type-agnostic). |
| `kos_dyn_complete_pending_exit(es, do_pop_node)` | Find pending exit from DYN_TOP and dispatch to complete it. `do_pop_node` 1 = full teardown (unlink), 0 = semantic-only. Called directly by eval's revive path; the former `kos_complete_pending_with_exit*` eval wrappers are gone. |

### Node lookup (generic)

| Function | Purpose |
|----------|---------|
| `kos_find_node_for_loop(es)` | Innermost LOOP or FOREACH region. Used by ITER_NEXT and by the transfer-to-loop walk that serves BREAK/CONTINUE. |
| `kos_find_prompt(es, prompt_id)` | Nearest PROMPT with matching id. Used by reset/shift. |
| `kos_find_nearest_delim(es, mode)` | Nearest DELIM with the given mode (e.g. K_RETURN). Used by RETURN. |

### Transitions

BREAK, CONTINUE, and RETURN do not walk the chain themselves. They build a `KosTransitionReq` (e.g. `KOS_TRANS_TRANSFER_TO_LOOP` with `KOS_XFER_EVENT_BREAK`) and submit it via `KosEvalState_TransitionOp`. The transition machinery in `dyn_walk.c` performs the lookup, offers `kos_dyn_handle` along the way, and pops through the generic entry points. Opcode code stays entirely type-agnostic on these paths.

---

## Type-specific bypass: when and why

Code bypasses the generic interface when it must **install** or **act on** a specific node type. That happens almost exclusively in **opcodes** and **transfer-plan machinery**, because those are the places that implement language features (with, loop, prompt, wind, etc.) and thus know which node type they are creating or manipulating.

### Push (install) bypass

Opcodes that push an extent call the type-specific push function. The opcode is designed for that extent; it supplies type-specific arguments (e.g. mgr, enter, exit for WITH) and receives the constructed node.

| Opcode | Type-specific call | Why |
|--------|--------------------|-----|
| PUSH_PROMPT | `kos_dyn_push_prompt(es, prompt_id)` | Prompt needs prompt_id; no generic "push extent" exists for prompts. |
| PUSH_WIND | `kos_dyn_push_wind(es, before, after)` | Wind needs before/after thunks. |
| PUSH_WITH | `kos_dyn_push_with_provisional` then generic `kos_dyn_wind` | WITH has mgr/enter/exit/name; the provisional push links the node, then wind runs enter (sync or async). On enter failure the opcode tears down with `kos_dyn_pop_with`. |
| PUSH_DYN_WITH | `kos_dyn_push_dyn_with_provisional` then generic `kos_dyn_wind` | Same as WITH; DYN_WITH for transfer-plan participation. |
| PUSH_TRY, PUSH_FINALLY | `kos_dyn_push_try`, `kos_dyn_push_finally` | Region needs handler_pc. |
| PUSH_LOOP, PUSH_FOREACH | `kos_dyn_push_loop`, `kos_dyn_push_foreach` | Region needs break_pc, continue_pc. |
| PUSH_SHIFT | `kos_dyn_push_shift(es)` | Shift is a marker; no extra args. |
| PUSH_NS | `kos_dyn_push_ns(es, saved_ns)` | NS needs saved_ns to restore. |
| PUSH_COLLECT | `kos_dyn_push_collect(es, parent)` | Collect needs parent namespace. |
| PUSH_PARAM / parameterize | `kos_dyn_push_param` | Param needs bindings. |
| PUSH_CC | `kos_dyn_push_cc(es, resume_pc)` | CC needs resume_pc. |

### Pop (teardown) bypass

Specialized POP opcodes act on `KosEvalState_CurrentNode(es)`, which their bytecode contract guarantees is the matching region kind. They call the type-specific pop to drop the right stack block and payload.

| Opcode / path | Type-specific call | Why |
|---------------|--------------------|-----|
| POP_TRY, POP_FINALLY, POP_LOOP, POP_FOREACH | `kos_dyn_pop_try`, `kos_dyn_pop_finally`, `kos_dyn_pop_loop`, `kos_dyn_pop_foreach` | Top node kind is guaranteed by compilation; the pop must drop the matching region block. |
| POP_WITH, POP_DYN_WITH | `kos_dyn_with_unwind` then `kos_dyn_pop_with` | The opcode runs exit explicitly (it owns the result delivery), then unlinks. |
| POP_WIND | generic `kos_dyn_pop_top(es, KOS_XFER_EVENT_RETURN)` | Pops whatever is on top, which must be WIND; teardown runs the after-thunk. |
| POP_PROMPT | `kos_dyn_pop_prompt(es, prompt)` | PROMPT is a barrier; transfer plan and POP_PROMPT explicitly pop it. |
| POP_PARAM | `kos_dyn_pop_param(es, param)` | Param extent needs explicit restore. |
| POP_NS | `kos_dyn_ns_unwind` via generic path | NS unwind is triggered by generic pop. |

### Exception unwind path

The exception unwind walk (`kos_dyn_try_local_raise_body` in `dyn_walk.c`) offers `kos_dyn_handle(es, node, RAISE)` per node and pops via `kos_dyn_pop_nonbarrier_top`. It does not call type-specific functions. Handler and resume PCs are read through capability-gated accessors (`kos_dynnode_raise_handler_pc_for_try_finally`, `kos_dynnode_catch_resume_pc`) — structural data needed to resume, not calls into kind-specific logic.

---

## Summary table

| Caller | Uses generic? | Uses type-specific? |
|--------|---------------|---------------------|
| eval.c (revive, transfer driver) | Yes: kos_dyn_complete_pending_exit, kos_dynnode_on_control_returned, apply/transition paths | No |
| dyn_walk.c walks (raise, return, yield, shift, loop transfer) | Yes: kos_dyn_handle, kos_dyn_pop_nonbarrier_top, kos_dyn_pop_top_allow_barrier, kos_find_node_for_* | No (capability-gated PC reads only) |
| BREAK, CONTINUE, RETURN | Yes: KosEvalState_TransitionOp, kos_find_nearest_delim | No |
| PUSH_* opcodes | PUSH_WITH/PUSH_DYN_WITH also call generic kos_dyn_wind | Yes: kos_dyn_push_* for the extent they implement |
| POP_* opcodes | POP_WIND: kos_dyn_pop_top | Yes: kos_dyn_pop_<kind> for the region they own |
| xfer_plan.c | Yes: kos_dyn_wind, kos_dyn_unwind, kos_dyn_push_clone, pop entry points | PROMPT barrier handling via kos_dyn_pop_prompt |
| dynchain.c (internal) | — | Dispatches via switch on KosDynKind; per-kind helpers are static in dyn_*.c fragments |

---

## Adding a new extent type

When adding a new dynamic node type:

1. Implement allocator, clone, traverse, free, and behavior handlers as `static` functions in a new `dynchain/dyn_*.c` fragment; add its `#include` to `dynchain.c`.
2. Add a type-specific `kos_dyn_push_foo` (and `kos_dyn_pop_foo` if needed) for the opcode that installs it.
3. Extend the generic dispatch switches (`kos_dyn_install`, `kos_dyn_wind`, `kos_dyn_unwind`, `kos_dyn_exception_unwind`, `kos_dyn_handle`, `kos_dyn_uninstall_handler`, `kos_dyn_clone`, `kos_dynnode_free`, GC traverse) so generic paths handle the new kind. Kinds with no behavior for an operation rely on that switch's documented default arm.
4. Do **not** add new type-named functions to eval.h or require eval.c to call them. Eval stays type-agnostic.
