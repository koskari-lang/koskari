# Dynamic Node Unified API: Design and Migration Specification

This document specifies the redesign of the Koskari dynamic node system into a clean, unified API. It is intended as an **implementation prompt**: a fresh implementer should be able to execute this specification without additional context.

**Existing reference material:** `dynchain.c`, `dynchain.h`, `eval.c`, [callcc.md](callcc.md), [with-dyn-with.md](with-dyn-with.md), [extent-wind-unwind.md](extent-wind-unwind.md).

## Implementation Status

This specification was partially adopted. The following notes describe what was implemented and what was not.

**Adopted:**
- §2.3 Capability bits (`KOS_DYN_CAP_*`): all capability constants are implemented in `dynchain.h`.
  `KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION` exists as an alias for `KOS_DYN_CAP_CATCH` (§2.3 rename
  partially done; both names are present).
- §3 Per-type semantics: TRY, FINALLY, LOOP, FOREACH as separate kinds (from the flatten proposal,
  which this doc informed). REGION row in the per-type table below no longer maps to a real kind;
  TRY/FINALLY/LOOP/FOREACH supersede it.
- §5 Transfer plan step types: `KOS_XFER_TRUNCATE_AND_INJECT` and `KOS_XFER_POP_ONE` are
  implemented. `KOS_XFER_DYN_WITH_ENTER` was removed as proposed.
- §7 POP_SHIFT via transfer plan: implemented.
- §9 Migration item 7 (WITH clone): WITH participates in capture. See §9.7 note below.

**Not adopted:**
- §2.2 `KosDynTypeOps` function-pointer dispatch table and `kos_dyn_type_ops[]` array: not
  implemented. Dispatch uses inline `switch` on `KosDynKind` throughout `dynchain.c` (as
  described in the flatten proposal §7, which this document also allowed as an option).
- §2.1 `teardown_after_unwind` handler slot and `KOS_DYN_CAP_NEEDS_TRANSFER_TEARDOWN` capability:
  not implemented. Teardown is folded into the unwind path rather than being a separate phase.
- §4 `kos_dyn_push` / `kos_dyn_pop` as the sole unified entry points: the existing
  `kos_dyn_push_*` / `kos_dyn_pop_*` per-type entry points are still present alongside the
  generic helpers.
- §8 "Adding a New Dynamic Node Type" via function table: since the table was not adopted, new
  kinds are added by extending the `switch` statements in `dynchain.c`.

**Open work:** See `design/interpreter/dynchain-refactor-notes.md` for remaining kind-check
violations that would be resolved by fuller adoption of the capability-based dispatch model.

---

## 1. Goals

1. **Unified API** — All dynamic node operations go through a single, consistent interface. No ad-hoc per-type code paths scattered across opcodes and eval.
2. **Extensibility** — Adding a new dynamic node type requires only: (a) the node structure and allocator, (b) binding into the push/pop/wind/unwind/clone dispatchers. No new API surface.
3. **Multi-shot continuations** — Both delimited (reset/shift) and undelimited (call/cc) continuations are multi-shot. Single-shot is an optimization flag, not a structural constraint.
4. **Clarity** — Code that operates on the dynamic stack should be trivial to read. The flow of push, pop, wind, unwind, and transfer plans must be obvious.
5. **Async-uniform** — Transfer plans and unwind handlers share the same async suspension/resumption mechanism. No special-case paths.

---

## 2. Handler Model

Each dynamic node type defines handlers via a type operations table. Generic code dispatches by node kind.

### 2.1 Handler Slots

| Slot | When invoked | Purpose |
|------|--------------|---------|
| **install** | After push | Establish extent (e.g. enter for WITH, bind for PARAM, push stack block for PROMPT) |
| **pop** | Before unlink | Tear down extent (e.g. exit for WITH, restore for PARAM, drop stack block for PROMPT) |
| **wind** | During transfer only | Re-enter extent when winding toward target (e.g. before for WIND, enter for PARAM/DYN_WITH) |
| **unwind** | During transfer only | Tear down when unwinding from source (e.g. after for WIND, exit for WITH/DYN_WITH) |
| **teardown_after_unwind** | After transfer unwind completes | Structural cleanup: drop stack block, unlink node (DYN_WITH and PROMPT). |
| **unwind_on_exception** | During exception unwinding, when normal pop is refused (e.g. BARRIER) | Capability-based exception dispatch. Handler performs cleanup, unlinks the node, and returns: 0 to continue unwinding, 1 if the exception was swallowed and execution should resume, `KOS_DYN_UNWIND_ASYNC_STARTED` if an async operation was started, or -1 on error. Only invoked when node has `KOS_DYN_CAP_HANDLES_EXCEPTIONS`. |
| **clone** | For continuation capture | Clone node for inclusion in captured chain. NULL = cannot clone (e.g. CC_RAISE sentinel). |

Nodes may have NULL for handlers they do not need. Install and wind may be the same function when semantics allow (e.g. PARAM, DYN_WITH).

### 2.2 Type Operations Structure

The implementer may use a function table for dispatch. Actual shape:

```c
typedef struct KosDynTypeOps {
  int (*install)(KosContext *ctx, KosDynNode *node);
  int (*pop)(KosContext *ctx, KosDynNode *node, KosEvent *out_ev);
  int (*wind)(KosContext *ctx, KosDynNode *node);
  int (*unwind)(KosContext *ctx, KosDynNode *node, PyObject **out_awaitable);
  int (*teardown_after_unwind)(KosContext *ctx, KosDynNode *node);
  int (*unwind_on_exception)(KosContext *ctx, KosDynNode *node, struct KosEvent *out_ev);
  KosDynNode *(*clone)(KosDynNode *node);
  uint32_t caps;
} KosDynTypeOps;

/* Indexed by KosDynKind. NULL slot = no-op or not applicable. */
extern const KosDynTypeOps kos_dyn_type_ops[];
```

Dispatch: `kos_dyn_type_ops[kind].install(ctx, node)`, etc. The generic push/pop/wind/unwind/exception-unwind paths call these.

### 2.3 Capability Bits

| Cap | Meaning |
|-----|---------|
| `KOS_DYN_CAP_CATCH` | Node may clear an exception during unwind. After handler returns, check `PyErr_Occurred()`; if clear, stop unwinding and resume at node's `resume_pc`. |
| `KOS_DYN_CAP_BARRIER` | Node blocks normal pop. Generic `kos_dyn_pop` returns error. Used by PROMPT. |
| `KOS_DYN_CAP_NEEDS_UNWIND_ACTION` | Node has unwind handler for transfer (WIND after, WITH/DYN_WITH exit, PARAM restore). |
| `KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION` | Node has wind handler for transfer (WIND before only). |
| `KOS_DYN_CAP_NEEDS_INSTALL_WIND` | Node needs wind run when a segment is installed or pushed (PARAM enter, DYN_WITH enter). |
| `KOS_DYN_CAP_NEEDS_TRANSFER_TEARDOWN` | Node needs teardown_after_unwind after transfer unwind (DYN_WITH and PROMPT). |
| `KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP` | Preserve exception state across pop for outer handlers. |
| `KOS_DYN_CAP_HANDLES_EXCEPTIONS` | Node has an `unwind_on_exception` handler. When normal `kos_dyn_pop_top` is refused (e.g. BARRIER), the exception unwind loop invokes this handler instead of treating the refusal as a fatal error. Used by PROMPT. |

Use `KOS_DYN_CAP_CATCH`. Replace the existing `KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION` with `KOS_DYN_CAP_CATCH` everywhere; they are the same concept.

---

## 3. Per-Type Semantics

Each type binds handlers and caps. This table is authoritative.

| Type | Install | Pop | Wind | Unwind | Teardown | Unwind-on-exc | Clone | Caps |
|------|---------|-----|------|--------|----------|---------------|-------|------|
| FRAME | — | unlink | — | — | — | — | yes | — |
| PROMPT | push stack block | drop stack block + unlink | — | — | drop stack block | unlink + drop stack block | yes | BARRIER, NEEDS_TRANSFER_TEARDOWN, HANDLES_EXCEPTIONS |
| WIND | — | after | before | after | — | — | yes | NEEDS_UNWIND, NEEDS_WIND_BEFORE |
| REGION | — | — | — | — | — | — | yes | — |
| NS | — | restore ns | — | — | — | — | yes | — |
| CC | — | unlink | — | — | — | — | yes | — |
| YIELD_FROM | — | close delegate | — | — | — | — | yes | PRESERVE_ERR_ACROSS_POP |
| PARAM | enter | restore | enter | restore | — | — | yes | NEEDS_UNWIND, NEEDS_INSTALL_WIND |
| WITH | enter | exit + teardown | — | exit + teardown | — | — | yes | CATCH, NEEDS_UNWIND |
| DYN_WITH | enter | exit + teardown | enter | exit + teardown | yes | — | yes | CATCH, NEEDS_UNWIND, NEEDS_INSTALL_WIND, NEEDS_TRANSFER_TEARDOWN |
| SHIFT | — | — | — | — | — | — | yes | — |

**Notes:**

- **WIND before:** Run by the opcode/compiler *before* calling push. The generic push does not run it. The install slot for WIND is NULL.
- **WITH vs DYN_WITH:** Both can be cloned. Single-shot for WITH is enforced by push/pop: enter runs once at push, exit runs once at pop. DYN_WITH additionally runs enter during transfer install when landing a cloned segment.
- **PROMPT and stack blocks:** PROMPT owns the COW stack block for the delimited continuation extent. `install` pushes a new stack block; `pop` and `unwind_on_exception` drop it. SHIFT is a simple marker with no stack block of its own.
- **PROMPT barrier and exception unwinding:** BARRIER means generic `kos_dyn_pop` refuses to pop it. During exception unwinding, `try_local_exception_handler` checks `HANDLES_EXCEPTIONS` and calls `unwind_on_exception`, which drops the stack block and unlinks the PROMPT, then returns 0 to continue unwinding past it. Transfer plans handle PROMPT explicitly (store return continuation, truncate, etc.).
- **SHIFT:** Marker node only; no handlers needed. Created by PUSH_SHIFT, popped as part of POP_SHIFT's transfer plan (TRUNCATE_AND_INJECT step).

---

## 4. Unified API

All opcodes and eval code must use these entry points exclusively. No direct manipulation of the dynamic chain outside this API.

### 4.1 Push and Pop

```c
/* Push node onto context. Calls install handler after linking. Returns 0 on success, -1 on error. */
int kos_dyn_push(KosContext *ctx, KosDynNode *node);

/* Pop node from context. Calls pop handler before unlinking. Returns 0, -1, or KOS_DYN_UNWIND_ASYNC_STARTED. For BARRIER nodes (PROMPT) returns -1. Use kos_dyn_pop_prompt (or equivalent) to abandon a prompt explicitly (POP_RESET path). */
int kos_dyn_pop(KosContext *ctx, KosDynNode *node, KosEvent *out_ev);

/* Clone node and push. Fails if node type cannot clone (e.g. returns error for types that must not cross capture). */
int kos_dyn_push_clone(KosContext *ctx, KosDynNode *node);
```

### 4.2 Segment Operations

```c
/* Copy chain segment from ctx's dyn_top down to node with given node_id. Walk from dyn_top toward root to find the node; use it as stop for clone. Pass 0 for full stack copy (stop = NULL). Returns cloned segment or NULL. */
KosDynNode *kos_dyn_copy_to(KosContext *ctx, uint32_t to_node_id);

/* Compute LCA of a and b. Uses identity first; if disjoint, uses graft_point_id for ID-based fallback. Returns LCA or NULL. */
KosDynNode *kos_dyn_lca(KosDynNode *a, KosDynNode *b, uint32_t graft_point_id);
```

### 4.3 Transfer Plans

```c
/* Build plan: unwind from current to LCA, install/push target, wind to target, inject. Returns 0 on success, -1 on error. */
int kos_dyn_build_transfer_plan(KosContext *ctx, KosDynNode *target, PyObject *value, int is_throw,
                                uint32_t graft_point_id, KosTransferPlan **out_plan);

/* Build plan for POP_SHIFT: unwind from top to prompt (exclusive), truncate at prompt (free through prompt), inject value. No wind phase. */
int kos_dyn_build_pop_shift_plan(KosContext *ctx, KosDynPrompt *prompt, PyObject *value,
                                 KosTransferPlan **out_plan);

/* Build plan for PUSH_SHIFT unwind: POP_ONE for each node from top to prompt (exclusive). Arrives at prompt. */
int kos_dyn_build_push_shift_unwind_plan(KosContext *ctx, KosDynPrompt *prompt,
                                         KosTransferPlan **out_plan);

/* Build plan for delimited invoke: wind nodes in segment, PUSH segment, inject. */
int kos_dyn_build_delimited_push_plan(KosContext *ctx, KosDynNode *segment, PyObject *value,
                                      int is_throw, KosTransferPlan **out_plan);

/* Build plan for direct install (cross barrier): INSTALL, wind, INJECT. No unwind. */
int kos_dyn_build_direct_install_plan(KosContext *ctx, KosDynNode *target, PyObject *value,
                                      int is_throw, uint32_t graft_point_id,
                                      KosTransferPlan **out_plan);

/* Execute next step of plan. Returns 1 when complete, 0 when more steps, KOS_XFER_SUSPENDED_AWAIT when async, -1 on error. */
int kos_run_transfer_step(KosContext *ctx);

/* Free plan and release resources. */
void kos_free_transfer_plan(KosTransferPlan *plan);
```

For plans that use INSTALL or PUSH, the caller must set `KosTransferPlan_RESUME_PC`, `KosTransferPlan_RESUME_FRAME`, and `KosTransferPlan_RESUME_STACK` on the plan after building and before running. The step handler uses these when splicing the chain and restoring execution state.

### 4.4 Plan Execution

The evaluator calls `kos_run_transfer_step` in a loop. When it returns `KOS_XFER_SUSPENDED_AWAIT`:

1. Plan has stored the awaitable in `KosTransferPlan_STEP_AWAITABLE` (or equivalent).
2. Driver schedules/awaits it and returns control.
3. On resume, driver feeds the result (or exception) into the plan and calls `kos_run_transfer_step` again.
4. The same step re-enters; extent runner resumes with the awaited value.

Transfer plans and standalone unwind (e.g. exception propagation) use the same extent runner. When an extent handler (WITH exit, WIND after) returns an awaitable, the runner stores it and returns `KOS_DYN_UNWIND_ASYNC_STARTED`. The driver treats this identically: suspend, await, resume, continue.

---

## 5. Transfer Plan Step Types

Extend the existing `KosTransferStep` enum:

| Step | Purpose |
|------|---------|
| `KOS_XFER_EXTENT_ACTION` | Run wind or unwind for a node. Data: node, phase (WIND/UNWIND). Used in transfer; does not unlink (INSTALL replaces chain). |
| `KOS_XFER_POP_ONE` | Run unwind for current top, then unlink and free. Used for sequential pop (e.g. PUSH_SHIFT unwind to prompt). |
| `KOS_XFER_INSTALL` | Replace current chain with clone of target segment. Run install wind for NEEDS_INSTALL_WIND nodes in segment. |
| `KOS_XFER_PUSH` | Append clone of segment to current chain. Run install wind for NEEDS_INSTALL_WIND nodes in segment. |
| `KOS_XFER_TRUNCATE_AND_INJECT` | Free from current top down to and including the given node. Set dyn_top to node's parent. Push value. Used for POP_SHIFT. |
| `KOS_XFER_INJECT` | Push value onto stack (or set throw mode). |

Remove or repurpose `KOS_XFER_DYN_WITH_ENTER`; DYN_WITH wind is folded into INSTALL/PUSH via NEEDS_INSTALL_WIND.

### 5.1 Plan Build Order

**Undelimited / general transfer:**  
Unwind (EXTENT_ACTION UNWIND from current to LCA) → INSTALL → Wind (EXTENT_ACTION WIND from LCA to target) → INJECT

**POP_SHIFT:**  
EXTENT_ACTION UNWIND for each node from current to prompt (DYN_WITH/WITH teardown pops; WIND runs after but stays) → TRUNCATE_AND_INJECT (free from current top through prompt, set dyn_top to prompt's parent, push value)

**PUSH_SHIFT (unwind to prompt):**  
POP_ONE for each node from current top down to (but not including) the prompt. Each POP_ONE runs unwind and unlink. After N steps, dyn_top equals the prompt.

**Delimited invoke:**  
Wind (EXTENT_ACTION WIND for NEEDS_WIND_BEFORE nodes in segment) → PUSH → INJECT

**Direct install (cross barrier):**  
INSTALL → Wind → INJECT. Use when the path from current to target crosses a barrier (e.g. PROMPT) that cannot be popped. We replace the chain without unwinding.

---

## 6. Exception Handling: CATCH and HANDLES_EXCEPTIONS

Two separate capability-based mechanisms govern exception handling during unwind.

### 6.1 CATCH — Exception Swallow via Unwind Handler

Nodes with `KOS_DYN_CAP_CATCH` (WITH, DYN_WITH) can clear an exception during normal unwind.

**Unwind loop convention:**

1. Before calling the unwind handler: ensure the current exception is restored (so the handler sees it).
2. Call the unwind handler. It may be async; wait for completion.
3. When the handler returns: check `PyErr_Occurred()`.
4. If **no** exception: stop unwinding. Resume at the node's `resume_pc`. The block has absorbed the exception.
5. If **exception** still set: continue unwinding to the next node.

The unwind loop does not know the handler's logic. It only checks the outcome.

**Swallow semantics:** When a CATCH node's unwind handler clears the exception (e.g. `__exit__` / `__aexit__` returns truthy):

- Exception is cleared.
- Unwinding **stops**.
- Execution resumes past the extent at the node's `resume_pc`, as if the block evaluated normally.

### 6.2 HANDLES_EXCEPTIONS — Barrier-Compatible Exception Dispatch

Nodes with `KOS_DYN_CAP_HANDLES_EXCEPTIONS` (currently PROMPT) provide an `unwind_on_exception`
handler that is invoked during exception unwinding when `kos_dyn_pop_top` refuses to pop the
node (because it is a BARRIER).

**Unwind loop convention when pop is refused:**

1. `try_local_exception_handler` calls `kos_dyn_pop_top`; for a BARRIER node this returns non-zero.
2. The original Python exception is preserved across this call (fetched before, restored after).
3. If the node has `KOS_DYN_CAP_HANDLES_EXCEPTIONS` and `ops->unwind_on_exception` is non-NULL:
   - Call `ops->unwind_on_exception(ctx, node, out_ev)`.
   - Return 0: node cleaned up and unlinked itself; continue unwinding.
   - Return 1: exception swallowed; stop unwinding and resume execution.
   - Return `KOS_DYN_UNWIND_ASYNC_STARTED`: async operation started; suspend.
   - Return -1: error; fall through to RAISE event.
4. If no handler or handler failed: convert the active exception to a `KOS_EV_RAISE` event and mark context dead.

**PROMPT's unwind_on_exception:** Drops the stack block (via `kos_dyn_pop_prompt`, which invokes
`type_ops_prompt_pop`) and unlinks the PROMPT node. Returns 0 so unwinding continues past the
prompt boundary. The exception propagates out of the `reset` region.

---

## 7. Reset and Shift

Reset and shift are extent blocks. No special-case logic except during delimited transfer install.

### 7.1 PUSH_RESET

Creates the prompt node. Push prompt onto the dynamic chain.

### 7.2 PUSH_SHIFT

1. Capture delimited continuation from resume_pc (POP_SHIFT+1) to top of stack.
2. Build `kos_dyn_build_push_shift_unwind_plan` and run it until complete. This unwinds to the prompt via POP_ONE steps (awaitable).
3. Push handler frame, bind continuation, enter shift body.

### 7.3 k(value) — Delimited Continuation Invoke

1. Capture the call-site continuation (from current top to appropriate anchor).
2. Build a PUSH plan: wind, PUSH captured segment, INJECT value.
3. Before or during PUSH step: store the call-site continuation in the cloned prompt node. The prompt structure must have a field (e.g. `return_continuation`) for this. This is the only special case: the delimited transfer install explicitly stores the return continuation into the prompt so POP_RESET can invoke it.
4. Run the plan.

### 7.4 POP_SHIFT

1. Take TOS as result.
2. Build a POP_SHIFT plan: (a) EXTENT_ACTION UNWIND for each node from current top down to (but not including) the prompt; (b) TRUNCATE_AND_INJECT: free from current top through the prompt, set dyn_top to prompt's parent, push result.
3. Run the plan. Use transfer plan machinery so async extent actions (e.g. WITH exit) suspend and resume correctly. The stack block at the destination may be different from the shift body's stack.
4. Result is on TOS of the reset body's frame.

### 7.5 POP_RESET

1. If the prompt has a stored call-site continuation: take the result (e.g. TOS), invoke that continuation with the result. The invocation builds and runs a transfer plan (unwind and wind back to the call site). The prompt is effectively left as part of that transfer.
2. Otherwise: pop the prompt (abandon the extent).

---

## 8. Adding a New Dynamic Node Type

1. **Define the structure** — Add a union member to `KosDynNode` (or a dedicated struct) and `KosDynKind` enum value.
2. **Implement allocator** — `kos_dyn_foo_new(parent, ...)`.
3. **Implement clone** — `kos_dyn_foo_clone(node)` if the type participates in capture.
4. **Bind handlers** — Add cases to the push/pop/wind/unwind dispatchers (switch on kind or function table). Implement install, pop, wind, unwind, teardown_after_unwind as needed.
5. **Set capability bits** — In the allocator, set the appropriate `KOS_DYN_CAP_*` flags.

No new API functions. No changes to transfer plan building or execution beyond what the handlers and caps already support.

---

## 9. Migration Checklist

1. **Introduce `kos_dyn_push` and `kos_dyn_pop`** — Implement as wrappers that dispatch to existing per-type logic. Replace all direct `kos_dyn_push_*` / `kos_dyn_pop_*` call sites with these.
2. **Unify extent runner** — Single path for `kos_extent_run_wind` and `kos_extent_run_unwind`. Ensure transfer plan and exception unwind use it.
3. **Add TRUNCATE_AND_INJECT and POP_ONE steps** — Implement in `kos_run_transfer_step`. POP_ONE runs unwind for current top and unlinks. TRUNCATE_AND_INJECT frees from top through given node and pushes value.
4. **POP_SHIFT via plan** — Replace pop loop with `kos_dyn_build_pop_shift_plan` and `kos_run_transfer_step` loop.
5. **CATCH handling** — In the unwind path, after each CATCH node's handler returns (including async completion), check exception state and resume at `resume_pc` if clear.
6. **Rename/consolidate caps** — `KOS_DYN_CAP_MAY_SWALLOW_EXCEPTION` → `KOS_DYN_CAP_CATCH`. Update all nodes.
7. **WITH clone** — Allow WITH to be cloned. Single-shot is enforced by enter/exit running once per push/pop. The existing with-dyn-with.md states WITH is a transfer barrier; that is superseded by this spec. WITH participates in capture and transfer; single-shot means one enter/exit per extent instance, not that transfer is blocked.
8. **Remove redundant functions** — Once `kos_dyn_push` and `kos_dyn_pop` are used everywhere, deprecate or remove the old `kos_dyn_push_*` / `kos_dyn_pop_*` per-type entry points, or keep them as thin wrappers that call the unified API.
9. **Opcodes** — Ensure PUSH_*, POP_* opcodes call only the unified API. No direct chain manipulation.

---

## 10. Invariants

- **All dyn chain mutations** go through `kos_dyn_push`, `kos_dyn_pop`, or transfer plan execution.
- **All extent actions** (wind, unwind) go through the unified extent runner. No inline extent logic in opcodes.
- **Async suspension** is handled uniformly: store awaitable, return suspended code, re-enter on resume.
- **CATCH nodes** determine swallow by clearing the exception. The unwind loop only observes the outcome.
- **HANDLES_EXCEPTIONS nodes** are invoked via `unwind_on_exception` when pop is refused (BARRIER). No kind-specific checks in the generic exception unwind loop.
- **PROMPT owns the stack block.** PUSH_PROMPT pushes a COW stack block; POP_PROMPT drops it. SHIFT is a simple marker with no stack block.
- **Reset/shift** store the call-site continuation in the cloned prompt during delimited invoke. No other special cases.
