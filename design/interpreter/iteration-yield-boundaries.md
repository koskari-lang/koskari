# Iteration, yield, and for-each: implementation map and consolidation

This document describes how four related mechanisms are implemented in the Koskari VM, which underlying C functions tie them together, and where consolidation is possible without blurring user-visible semantics.

**Scope**

- `KOS_OP_YIELD` — generator yield (control out and back; no iteration protocol)
- `KOS_OP_YIELD_FROM` — delegate to sub-iterator / sub-generator / await (expression-level conduit)
- `KOS_OP_PUSH_FOREACH` + `KOS_OP_ITER_NEXT` — structured for-each loop (region + stepping)

`KOS_OP_NEXT` appears where it shares machinery with `ITER_NEXT`; it is not a fourth “user story” in the same sense but is the stack-based iterator step used by some compiled forms.

**Related documentation**

- [EVALUATOR.md](../EVALUATOR.md) — eval loop, `KosEvent`, post-opcode yield handling, `KOS_DYN_NEXT` / `KOS_DYN_FOREACH` flattening
- [../EVALUATOR.md](../EVALUATOR.md) §5 — control transit and dynamic-node event model
- [../archive/yield-from-execution-model.md](../archive/yield-from-execution-model.md) — archived YIELD_FROM delegation narrative
- [../archive/dynnode-event-model.md](../archive/dynnode-event-model.md) — archived handle/unwind write-up

---

## 1. Operator responsibilities (high level)


| Mechanism        | Role                                                                                                                                                            | Iteration?               | Suspend/resume?                                              |
| ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------ | ------------------------------------------------------------ |
| **YIELD**        | Pop value(s), emit `KOS_EV_YIELD` at generator boundary, or detach to parent frame if not a boundary yield                                                      | No                       | Yes (`revive_yield` on re-entry)                             |
| **YIELD_FROM**   | Normalize TOS to a delegate, push `KOS_DYN_YIELD_FROM`, drive one step per dispatch until done; may emit `KOS_EV_YIELD`, `KOS_EV_AWAIT`, or complete with value | Yes (multiple protocols) | Yes (`revive_yield` / `revive_await`; node holds phase)      |
| **PUSH_FOREACH** | Pop iterable + loop result, build iterator (sync / async / Koskari async gen ref), push `KOS_DYN_FOREACH` region + stack block                                  | Yes (acquisition only)   | No by itself                                                 |
| **ITER_NEXT**    | Load iterator from innermost `FOREACH` region; one step; branch on exhaustion; Koskari async gen uses attach path                                               | Yes (step + exhaustion)  | Can (`KOS_EV_AWAIT` for Python async iter via shared helper) |


---

## 2. Implementation locations (source files)


| Opcode / construct       | Primary handler                                                                                                                                                                           |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `YIELD`                  | `koskari/lib/opcode/YIELD.c`                                                                                                                                                              |
| `YIELD_FROM`             | `koskari/lib/opcode/YIELD_FROM.c` (`yield_from_normalize`, `yield_from_emit_yield`, `yield_from_drive_*`, `KOS_OP_HANDLER(YIELD_FROM)`)                                                   |
| `PUSH_FOREACH`           | `koskari/lib/opcode/PUSH_FOREACH.c`                                                                                                                                                       |
| `ITER_NEXT`              | `koskari/lib/opcode/ITER_NEXT.c`                                                                                                                                                          |
| Shared “get next”        | `koskari/lib/eval.c` — `op_get_next_from_iter`, `next_from_python_async_iter`, `next_from_python_sync_iter`, `attach_koskari_gen_core` / `op_attach_koskari_agen_frame(es, …)`, `op_attach_koskari_agen_frame_for_iter_next(es, …)`, `op_attach_koskari_agen_frame_for_yield_from(es, …)` (all take live **`KosEvalState *es`**) |
| Yield boundary + flatten | `koskari/lib/eval.c` — `eval_phase_post_opcode` (`KOS_EV_YIELD`, …); `koskari/lib/dynchain.c` — `kos_dyn_walk_handle_yield`, `kos_dyn_gen_freeze_and_release`     |
| Entry revive             | `koskari/lib/eval.c` — `revive_yield`, `revive_await`                                                                                                                                     |


---

## 3. Flow breakdown by operator

### 3.1 YIELD

**What it does**

1. Pops yielded value(s) from the current frame stack (or uses `None` for arg 0).
2. If the frame’s dyn parent is **not** `KOS_DYN_GEN`, treats the yield as **in-loop delivery**: detaches frame, pushes value onto parent stack, returns 0 (no `out_ev` boundary).
3. Otherwise sets `out_ev.kind = KOS_EV_YIELD`, payload, marks context dead, returns 1 → `eval_phase_post_opcode` → `kos_dyn_walk_handle_yield` (and possibly pending-agen flattening per [EVALUATOR.md](../EVALUATOR.md)).

**Underlying calls (boundary path)**

- `KosStackBlock_PopRef` / `PopTail`
- `kos_dyn_detach_top_frame` (non-boundary path)
- `kos_dyn_walk_handle_yield` → `kos_dyn_gen_freeze_and_release` (typical)
- Entry: `revive_yield` pushes `send_val` for resume

**Not shared with** iterator stepping: no `PyIter_Next`, no `__anext__`, no `yield_from_normalize`.

---

### 3.2 YIELD_FROM

**What it does**

1. **First entry:** pop subexpression from stack, `**yield_from_normalize`** → `(delegate, kind)`; push `**KOS_DYN_YIELD_FROM**`; roll PC back to `YIELD_FROM`; return 0.
2. **Subsequent entries:** consume inject (send/throw/await result) per `KosDynYieldFrom` phase; dispatch to `**yield_from_drive_sync_iter`**, `**yield_from_drive_sync_gen`**, `**yield_from_drive_py_async**`, or `**yield_from_drive_kos_async_gen**` (Koskari async gen is **flattened** into the same eval via **`op_attach_koskari_agen_frame_for_yield_from(es, …)`**, not a nested **`koskari_eval(agen->ctx, …)`**).
3. On each yielded value: typically `**yield_from_emit_yield**` → `out_ev = KOS_EV_YIELD`, PC rollback, phase `EXPECT_SEND`.
4. On await: `out_ev = KOS_EV_AWAIT`, `EXPECT_AWAIT` phase.
5. On completion: pop yield-from node, push final value or return 0 per kind.

**Underlying calls (representative)**

- `yield_from_normalize` — classification, `PyObject_GetIter`, callable one-shot, coroutine rejection
- `yield_from_emit_yield`, `yield_from_rollback_pc`
- Per-kind drivers: `PyIter_Next`, Python gen `send`/`throw`, `__anext__` / async gen helpers; **`KOS_YF_KOS_ASYNC_GEN`** uses **`op_attach_koskari_agen_frame_for_yield_from`** + **`kos_dyn_gen_install`** on the current **`es`** (Python-driven **`__anext__`** still uses **`koskari_eval(agen->ctx, …)`** outside this opcode path)
- `kos_dyn_pop_yield_from`, `kos_yieldfrom_close_delegate` (unwind)
- Same eval boundary stack as plain YIELD: `**kos_dyn_walk_handle_yield`**, `**revive_yield**`, `**revive_await**`

---

### 3.3 PUSH_FOREACH (for-each setup)

**What it does**

1. Pops **iterable** and **loop_result** from stack.
2. Resolves **break** PC from extent table (`KOS_EXT_FOREACH`).
3. Builds **iterator**:
  - `KosPyAsyncGen_Check` → hold reference to async gen (no `GetIter` yet in the same way as sync)
  - else if `tp_as_async->am_aiter` → `am_aiter(iterable)`
  - else `PyObject_GetIter(iterable)`
4. Pushes `**kos_dyn_push_foreach**` with `KosDynForeach_ITER`, `KosDynForeach_RESULT`, `KosDynForeach_EXHAUSTED = 0`, and a new stack block.

**Underlying calls**

- `kos_extent_find`, `kos_extent_desc`
- `PyObject_GetIter`, async `am_aiter`
- `kos_dyn_push_foreach`, `KosStackBlock_PushBlock`

**Overlap note:** this three-way acquisition is **similar in spirit** to the **fallback** branch of `yield_from_normalize` (Koskari agen / async / sync), but **does not** perform yield-from’s full taxonomy (sync `PyGen`, callable, explicit `__anext_`_ classifier, coroutine rejection message).

---

### 3.4 ITER_NEXT (for-each step)

**What it does**

1. Finds innermost `**KOS_DYN_FOREACH`** via `kos_find_node_for_loop`.
2. Reads `**KosDynForeach_ITER**`.
3. **If Koskari async gen:** records else PC and step phase on the **`KOS_DYN_FOREACH`** region, **`op_attach_koskari_agen_frame_for_iter_next(es, …)`**, returns 0 (flatten into same eval loop).
4. **Else:** `**op_get_next_from_iter`** (same as `NEXT` for non–async-gen cases):
  - `GET_NEXT_VALUE` → push value, return 0
  - `GET_NEXT_EXHAUSTED` → set exhausted, **`KosEvalState_SetFramePc(es, else_pc)`**
  - `GET_NEXT_SUSPEND` → return 1, `out_ev` await path

**Underlying calls**

- `op_get_next_from_iter` → `get_await_next` / `PyIter_Next` / etc. (see `eval.c`)
- `op_attach_koskari_agen_frame_for_iter_next(es, …)` → `attach_koskari_gen_core` → `kos_dyn_gen_install(es, gen)`
- Post-opcode yield/return integration with **`KOS_DYN_NEXT`** / attach paths (same family as `NEXT`) — see [EVALUATOR.md](../EVALUATOR.md) and `eval_phase_post_opcode` in `eval.c`

---

## 4. Shared infrastructure (already consolidated)


| Layer                                                                         | Shared by                                           | Notes                                                                                                |
| ----------------------------------------------------------------------------- | --------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `op_get_next_from_iter`                                                       | `NEXT`, `ITER_NEXT`                                 | Single entry for “iterator object → one value / suspend / exhausted” except Koskari async gen attach |
| `op_attach_koskari_agen_frame(es, …)` / `op_attach_koskari_agen_frame_for_iter_next(es, …)` / `op_attach_koskari_agen_frame_for_yield_from(es, …)` | `NEXT`, `ITER_NEXT`, **YIELD_FROM** (kos async gen) | Shared **`attach_koskari_gen_core`**; **`kos_dyn_gen_install(es, gen)`** on the opcode’s live **`es`** |
| `KOS_DYN_NEXT` / `KOS_DYN_FOREACH` attach paths + `eval_phase_post_opcode`     | `NEXT`, `ITER_NEXT`, yield paths when flattened     | Delivers inner yield/return without escaping boundary                                                |
| `kos_dyn_walk_handle_yield`, `revive_yield`, `revive_await`                   | `YIELD`, `YIELD_FROM`                               | Same boundary kinds; no `KOS_SUSP_YIELD_FROM`                                                        |


---

## 5. Duplication and drift risk

1. **Iterable → iterator / delegate**
  - `**PUSH_FOREACH`** and `**yield_from_normalize**` both encode “what kind of thing is this?” with overlapping branches (Koskari async gen, async protocol, sync iter).  
  - **Risk:** fixing a bug in one path (e.g. async iterator edge case) may not fix the other.
2. **Sync iterator step**
  - `**yield_from_drive_sync_iter`** uses `PyIter_Next` + yield-from completion rules.  
  - `**op_get_next_from_iter**` uses sync path via `next_from_python_sync_iter`.  
  - **Risk:** subtle differences in StopIteration / error handling unless kept in sync intentionally.
3. **Semantics**
  - **YIELD_FROM** is a **pinned-PC state machine** with send/throw/close and multiple delegate kinds.  
  - **ITER_NEXT** is a **loop opcode** with else-branch exhaustion and **no** yield-from send protocol.  
   **These must not be merged into one user-visible opcode**; any shared code must stay below the opcode level.

---

## 6. Consolidation proposals

### 6.1 Recommended (low risk)

- **Extract `kos_iterable_to_step_target`** (name illustrative) implementing only the **common** branch shared by `**PUSH_FOREACH`** and the `**PyObject_GetIter` / `am_aiter` / Koskari async gen ref** part of `**yield_from_normalize`**, returning something like `(PyObject *delegate, enum iter_kind)` or reusing a small subset of existing `KOS_YF_*` kinds.  
- Keep `**yield_from_normalize**`’s extra rules **in the same file** as today: callable one-shot, `PyGen` vs async gen detection, coroutine rejection, and mapping to full `KOS_YF_*` set.  
- **Add regression tests** that run the same iterable through **for-each** and **yield-from** where semantics should match (e.g. list, sync gen, async iterator).

### 6.2 Optional (medium effort)

- Factor **sync iterator one-step** into a single helper used by both `**yield_from_drive_sync_iter`** and the sync branch inside `**op_get_next_from_iter**`, with explicit documentation of **StopIteration** vs **yield-from final value** differences.

### 6.3 Not recommended

- Merging `**YIELD`** into `**YIELD_FROM**` or `**ITER_NEXT**` into `**YIELD_FROM**`: different bytecode, extent model, and compiler contracts.  
- Replacing `**PUSH_FOREACH` / `ITER_NEXT**` with a single mega-opcode: hurts validation, readability, and stack layout clarity.

---

## 7. Summary

- **YIELD** is **pure boundary control flow**; iteration lives elsewhere.  
- **YIELD_FROM** is a **full delegation** implementation with its own dyn node and drivers.  
- **PUSH_FOREACH** + **ITER_NEXT** are **structured loop** + **shared `op_get_next_from_iter`** stepping.  
- **Consolidation** should target **duplicate iterable acquisition** and **duplicate sync-step** logic, not opcode surface area. The eval loop and `**KosEvent`** handling for yields are already shared; see [EVALUATOR.md](../EVALUATOR.md).

