# Generic DynNode Protocol

This document describes the design ideology of the Koskari dynamic chain interface and provides a reference for all generic calls versus type-specific bypasses. It complements [dynamic-node-unified-api.md](dynamic-node-unified-api.md) and [extent-wind-unwind.md](extent-wind-unwind.md).

---

## Design ideology

The dynamic chain is a linked structure of `KosDynNode` objects representing extents (try/finally, loops, with, wind, prompts, etc.). The design separates two layers:

1. **Generic interface**: Code that operates on the chain without knowing node types. It works with `KosDynNode *`, calls generic APIs, and never branches on `KosDynWith`, `KosDynWind`, etc. The evaluator core (eval.c), transfer plans, and most opcode logic use only the generic interface.

2. **Type-specific bypass**: Code that explicitly creates, installs, or acts on a particular node type because it is implementing a specific opcode or feature. Opcodes such as PUSH_WITH, PUSH_LOOP, POP_PROMPT are designed for a specific extent kind; they must call the type-specific push/pop APIs to install or tear down that extent correctly.

**Principle**: eval.c and generic callers must remain type-agnostic. They never call `kos_dyn_with_on_control_returned`, `kos_dyn_find_pending_exit_with`, or any other type-named function. Type dispatching lives entirely inside dynchain.c. The generic caller sees only `KosDynNode *` and operations like "find pending exit", "complete pending exit", "pop top", "unwind to frame".

---

## Generic API reference

### Chain topology

| Function | Purpose |
|----------|---------|
| `kos_dyn_push(ctx, node)` | Push a pre-allocated node onto the chain. Runs install handler. Used internally by per-type push helpers. |
| `kos_dyn_pop_top(ctx, out_ev)` | Pop the top node. Dispatches on kind: runs unwind if needed, unlinks, frees. Returns 0, -1, or `KOS_DYN_UNWIND_ASYNC_STARTED`. |
| `kos_dyn_push_clone(ctx, node)` | Clone node and push. Used by transfer plans when installing a captured segment. |

### Unwind and transfer

| Function | Purpose |
|----------|---------|
| `kos_dyn_unwind_to_frame(ctx, frame, out_ev)` | Unwind from dyn_top down to the given frame, running extent teardown (wind after, with exit, etc.). Used by RETURN, exception propagation, and transfer plans. |
| `kos_dyn_unwind_to_node(ctx, target, out_ev)` | Same as above but stop at an arbitrary node. Used by POP_PROMPT for delimited transfer. |
| `kos_dyn_install(ctx, node, out_ev)` | Run install-phase setup when a node is pushed. Dispatches per kind (e.g. DYN_WITH enter). |
| `kos_dyn_wind(ctx, node, out_ev)` | Run wind (enter) for extent nodes during transfer. Dispatches to `kos_dyn_wind_wind`, `kos_dyn_dyn_with_wind`, etc. |
| `kos_dyn_unwind(ctx, node, out_ev)` | Run unwind (exit) for extent nodes. May start async (returns `KOS_DYN_UNWIND_ASYNC_STARTED`). |
| `kos_dyn_exception_unwind(ctx, node, out_ev)` | Exception-specific unwind when propagation reaches a HANDLES_EXCEPTIONS node. |
| `kos_dyn_clone(node)` | Allocate a copy of the node for continuation capture. NULL if type forbids cloning. |

### Control-returned hook

| Function | Purpose |
|----------|---------|
| `kos_dynnode_on_control_returned(ctx, node, out_ev)` | Called when control returns to a frame (e.g. RETURN after wind thunk, or revive from AWAIT). Dispatches: for WIND in PRE_BEGIN/PRE_AFTER, advances phase; for WITH in PENDING_ENTER_AWAIT or PENDING_EXIT_AWAIT, runs enter/exit completion. Eval calls this with `KosContext_DYN_TOP(ctx)`; it never needs to know the node kind. |

### Pending-exit (generic)

| Function | Purpose |
|----------|---------|
| `kos_dyn_find_pending_exit(node)` | Walk chain from node toward root; return first node with a pending exit. Currently only WITH (PENDING_EXIT_AWAIT) has this; other types may add support. Returns `KosDynNode *` (caller stays type-agnostic). |
| `kos_dyn_complete_pending_exit(ctx, do_pop_node)` | Find pending exit from DYN_TOP, dispatch to complete it. `do_pop_node` 1 = full teardown (unlink), 0 = semantic-only. Used by revive_await and kos_complete_pending_with_exit*. |
| `kos_dyn_clear_pending_exit(ctx)` | Walk chain, clear pending-exit state (exc_*, phase) on any node that has it. Used when abandoning a context (e.g. before freeing). |

### Node lookup (generic)

| Function | Purpose |
|----------|---------|
| `kos_find_node_for_raise(ctx)` | Find the topmost enabled TRY or FINALLY region. Used by exception dispatch. Returns `KosDynNode *`. |
| `kos_find_node_for_loop(ctx)` | Find the innermost LOOP or FOREACH region. Used by BREAK, CONTINUE, ITER_NEXT. Returns `KosDynNode *`. |
| `kos_dyn_find_prompt(ctx, prompt_id)` | Find the nearest PROMPT with matching id. Used by reset/shift. |

### Eval-layer wrappers

These live in eval.c/eval.h and delegate to the generic dynchain APIs. They preserve a stable API for callers while keeping eval type-agnostic. Internal eval paths (e.g. revive_await) call `kos_dyn_complete_pending_exit` directly:

| Function | Delegates to | Purpose |
|----------|--------------|---------|
| `kos_complete_pending_with_exit(ctx)` | `kos_dyn_complete_pending_exit(ctx, 1)` | Public API: complete and unlink pending WITH exit. |
| `kos_complete_pending_with_exit_semantic(ctx)` | `kos_dyn_complete_pending_exit(ctx, 0)` | Public API: semantic-only completion (caller handles unlink). |
| `kos_clear_pending_with_exit(ctx)` | `kos_dyn_clear_pending_exit(ctx)` | Clear pending-exit state on teardown (e.g. before context free). |

---

## Type-specific bypass: when and why

Code bypasses the generic interface when it must **install** or **act on** a specific node type. That happens almost exclusively in **opcodes** and **transfer-plan machinery**, because those are the places that implement language features (with, loop, prompt, wind, etc.) and thus know which node type they are creating or manipulating.

### Push (install) bypass

Opcodes that push an extent call the type-specific push function. The opcode is designed for that extent; it supplies type-specific arguments (e.g. mgr, enter, exit for WITH) and receives the constructed node.

| Opcode | Type-specific call | Why |
|--------|--------------------|-----|
| PUSH_PROMPT | `kos_dyn_push_prompt(ctx, prompt_id)` | Prompt needs prompt_id; no generic "push extent" exists for prompts. |
| PUSH_WIND | `kos_dyn_push_wind(ctx, before, after)` | Wind needs before/after thunks. |
| PUSH_WITH | `kos_dyn_push_with_provisional` + `finalize_provisional_with_enter` | WITH has mgr/enter/exit/name; provisional path defers enter to allow sync/async. |
| PUSH_DYN_WITH | `kos_dyn_push_dyn_with_provisional` + `finalize_provisional_with_enter` | Same as WITH; DYN_WITH for transfer-plan participation. |
| PUSH_TRY, PUSH_FINALLY | `kos_dyn_push_try`, `kos_dyn_push_finally` | Region needs handler_pc. |
| PUSH_LOOP, PUSH_FOREACH | `kos_dyn_push_loop`, `kos_dyn_push_foreach` | Region needs break_pc, continue_pc. |
| PUSH_SHIFT | `kos_dyn_push_shift(ctx)` | Shift is a marker; no extra args. |
| PUSH_NS | `kos_dyn_push_ns(ctx, saved_ns)` | NS needs saved_ns to restore. |
| PUSH_COLLECT | `kos_dyn_push_collect(ctx, parent)` | Collect needs parent namespace. |
| PUSH_PARAM / parameterize | `kos_dyn_push_param` | Param needs bindings. |
| CALL_CC | `kos_dyn_push_cc(ctx, resume_pc)` | CC needs resume_pc. |

### Pop (teardown) bypass

Opcodes that pop a specific extent use the type-specific pop when they have already identified the node (e.g. via `kos_find_node_for_loop`) and need to tear it down. The generic `kos_dyn_pop_top` also runs teardown, but it is used when the caller does not care about the kind (e.g. POP_WIND pops whatever is on top).

| Opcode / path | Type-specific call | Why |
|---------------|--------------------|-----|
| POP_PROMPT | `kos_dyn_pop_prompt(ctx, prompt)` | PROMPT is a barrier; transfer plan and POP_RESET explicitly pop it. |
| POP_WIND | `kos_dyn_pop_top(ctx, out_ev)` | Generic: POP_WIND pops top, which must be WIND. |
| BREAK, CONTINUE, POP_LOOP, POP_FOREACH, etc. | `kos_dyn_pop_try`, `kos_dyn_pop_loop`, `kos_dyn_pop_foreach`, ... | After `kos_find_node_for_loop`, caller knows the kind; must pop the correct region type (TRY vs FINALLY vs LOOP vs FOREACH) to drop the right stack block. |
| POP_PARAM | `kos_dyn_pop_param(ctx, param)` | Param extent needs explicit restore. |
| POP_NS | `kos_dyn_ns_unwind` via generic path | NS unwind is triggered by generic pop. |

### Finalize / post-enter bypass

| Opcode | Type-specific call | Why |
|--------|--------------------|-----|
| PUSH_WITH, PUSH_DYN_WITH | `finalize_provisional_with_enter(ctx, result, with_node)` | After enter returns (sync or after await), the WITH opcode must store the result to the frame namespace and set entered=1. The opcode holds the with_node it created; it calls finalize to complete the install. |

### Exception unwind path

The exception unwind loop in eval.c uses `kos_dyn_pop_top` and `kos_find_node_for_raise`. It does not call type-specific functions. When it needs to read `KosDynWith_RESUME_PC` or similar fields, it does so only after identifying the node via `KosDynNode_IS_WITH_OR_DYN_WITH` — that is a capability check for structural data (resume_pc) needed to resume, not a call into WITH-specific logic.

---

## Summary table

| Caller | Uses generic? | Uses type-specific? |
|--------|---------------|---------------------|
| eval.c (revive, exception unwind, transfer driver) | Yes: kos_dyn_complete_pending_exit, kos_dyn_clear_pending_exit, kos_dynnode_on_control_returned, kos_dyn_pop_top, kos_dyn_unwind_to_*, kos_find_node_for_* | No (except reading resume_pc etc. after capability check) |
| RETURN, POP_WIND, utils, object.c | Yes: kos_dyn_unwind_to_frame, kos_dyn_pop_top, kos_find_node_for_loop | No |
| PUSH_* opcodes | No (they install) | Yes: kos_dyn_push_* for the extent they implement |
| POP_* / BREAK / CONTINUE | Yes: kos_find_node_for_loop, kos_dyn_pop_top | Yes: kos_dyn_pop_try/loop/foreach/finally to tear down the specific region |
| POP_PROMPT | Yes: kos_dyn_unwind_to_node, kos_dyn_unwind_to_frame | Yes: kos_dyn_pop_prompt for barrier pop |
| PUSH_WITH, PUSH_DYN_WITH | — | Yes: kos_dyn_push_*_provisional, finalize_provisional_with_enter |
| dynchain.c (internal) | — | Dispatches via switch on KosDynKind; type-specific helpers are static |

---

## Adding a new extent type

When adding a new dynamic node type:

1. Implement allocator, clone, and handlers in dynchain.c.
2. Add a type-specific `kos_dyn_push_foo` (and `kos_dyn_pop_foo` if needed) for the opcode that installs it.
3. Extend the generic dispatchers (`kos_dyn_install`, `kos_dyn_wind`, `kos_dyn_unwind`, `kos_dyn_pop_top` switch, etc.) so generic paths handle the new kind.
4. Do **not** add new type-named functions to eval.h or require eval.c to call them. Eval stays type-agnostic.
