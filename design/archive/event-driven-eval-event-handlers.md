# Event-driven eval handlers: RETURN escape, RAISE, YIELD, and node dispatch

**Status:** Implemented (archived 2026-06-22). Canonical summary:
[EVALUATOR.md](../EVALUATOR.md) (phases, `apply_kos_op_result`, `eval_event_*`);
[archive/kosevent-elimination-merged.md](kosevent-elimination-merged.md).

---

This document specifies the **next** refactor after the completed NEXT boundary milestone (delivered: `KOS_DYN_NEXT`, FOREACH phases, removal of `KosContext.pending_agen*`). Background: [event-driven-node-delivery.md](event-driven-node-delivery.md). It focuses on **escaping the overloaded `RETURN` opcode**, unifying **RAISE**, **RETURN**, and **YIELD** behind **eval-owned** routing, and introducing **`eval_event_*`** helpers that encapsulate non-trivial handling (transitions, transfer plans, dyn walks).

**Implementation status (historical):** At archive time this design was **only partially realized** in the tree. Post-opcode **YIELD** and **RETURN** boundary delivery to **`KOS_DYN_NEXT` / `KOS_DYN_FOREACH`** shared helpers with the opcode path for the **iterator exhaustion** slice of **`RETURN`**. The bulk of **`RETURN`** Phase **2** still lived in the opcode. See §12 for migration proposals that have since been completed.

Companion documents: [evaluator.md](../interpreter/evaluator.md), [generic-dynnode-protocol.md](../interpreter/generic-dynnode-protocol.md).

---

## 1. Motivation

### 1.1 What is still wrong after NEXT delivery

The standalone **`NEXT`** / **`ITER_NEXT`** boundary is largely **event-driven**: **`YIELD`** and (when applicable) **`RETURN`** surface as **`KosEvent`**, then **`eval_phase_post_opcode`** runs **`kos_dyn_transition`** for yield walks and **`eval_event_yield`** / **`eval_event_return_deliver_to_boundary`** for flattened Koskari async-generator delivery to **`KOS_DYN_NEXT`** or **`KOS_DYN_FOREACH`**.

**`RETURN`** remains **overloaded** outside the iterator-delivery slice. In [`koskari/lib/opcode/RETURN.c`](../../koskari/lib/opcode/RETURN.c) the handler still:

- **Phase 1:** Pops stack operands and builds the return value (appropriate for an opcode).
- **Phase 2a:** Runs **delimited** return (`kos_return_delimited`) when prompts are active — **still opcode-local**.
- **Phase 2b:** For a frame with a **non-GEN** parent, runs **`KOS_TRANS_WALK_RETURN_TO_FRAME`**, handles async unwind / **finally** taking return, **`kos_dyn_pop_frame`**, then **`kos_dyn_try_deliver_iterator_boundary_after_pop`** (shared with eval), then **native** handler or stack push or **`KOS_OP_RES_CONTROL_RETURNED`** — **still almost entirely in the opcode**.
- **Phase 2c:** Top-level / **GEN-parent** path runs the same **walk_return** family, then emits **`KOS_EV_RETURN`**, **`KOS_CTX_DEAD`** — **still in the opcode**.

The **flattened async-gen exhaustion** policy for **NEXT/FOREACH** after **`pop_frame`** is **no longer duplicated** inline; it uses **`kos_dyn_deliver_iterator_boundary_return_event`** from both **`RETURN.c`** and **`eval.c`**. Everything else in Phase **2** above is **not** yet **`eval_event_return`**.

That remaining split means **unwind, parent delivery, and terminal completion** are still **opcode-driven** for **`RETURN`**, while **post-opcode YIELD/RETURN boundary** paths are **eval-driven** — a **partial** handler model until **`eval_event_return`** owns Phase **2** end-to-end (§12).

### 1.2 Goal statement

1. **`RETURN`** becomes **thin**: pop operands, optionally run **delimited** return (still opcode-local if it does not participate in the dyn event model), otherwise record **return value** and signal **`KOS_EV_RETURN`** (or an internal **post tag** that **always** runs **`eval_phase_post_opcode`**) so that **eval** applies **unwind-to-frame**, **pop_frame**, and **boundary delivery** in **one** place.

2. **`YIELD`** and **`KOS_EV_YIELD`** remain opcode-thin; **eval** continues to own **`eval_event_yield`**-class logic: **walk_handle_yield** / freeze, then **boundary delivery** when the flattened-gen predicate holds.

3. **`RAISE`** / **`KOS_EV_RAISE`** and the **exception** path are **handler-based** in the same sense: the opcode or **`try_local`** machinery **tags** an exception boundary outcome; **eval** runs **`eval_event_raise`** (or extends **`eval_step_after_process_raise`**) so that **transfer plans**, **dyn unwind**, and **optional** node caps share **one** ordering contract.

4. **Distinct `eval_event_*` helpers** encapsulate **multi-step** work (transition requests, **`KosEvalStep`** outcomes, **`out_ev`** ownership rules) so **`eval_phase_post_opcode`** and the main loop stay **readable** and **testable**.

5. **Capacity-driven** **`KosDynNode`** dispatch: nodes that **accept** iterator-boundary or control-return events advertise **caps**; **eval** does not hard-code **`KOS_DYN_NEXT`** vs **`KOS_DYN_FOREACH`** in scattered **if** ladders beyond a **small** router.

---

## 2. Target architecture (conceptual)

```text
Opcode RETURN / YIELD / RAISE (thin)
    |
    v
post_tag -> eval_phase_post_opcode (unchanged entry)
    |
    +-- eval_event_yield(ctx, out_ev)     # if KOS_EV_YIELD
    +-- eval_event_return(ctx, out_ev)    # if KOS_EV_RETURN (non-delimited)
    +-- eval_event_raise(ctx, out_ev)     # if KOS_EV_RAISE or PyErr + agreed tag
    |
    v
Optional: kos_dyn_transition / walk / transfer plan drain (inside helpers)
    |
    v
Router: find boundary consumer(s) by cap + chain position (NEXT, FOREACH, GEN, ...)
    |
    v
Node handler OR default eval policy (push stack, STOP_EVENT, REPROLOGUE)
```

**Invariant (target):** **`RETURN`** should not embed iterator-boundary policy; **eval** should drive **`KOS_EV_RETURN`** handling. **Interim:** [`kos_dyn_try_deliver_iterator_boundary_after_pop`](../../koskari/lib/dynchain/dyn_next.c) may be called from **`RETURN.c`** after **`pop_frame`** until **`eval_event_return`** subsumes Phase **2**; it shares **`kos_dyn_deliver_iterator_boundary_return_event`** with the eval path.

---

## 3. `eval_event_*` helpers (proposed responsibilities)

Names are **proposed**; final names should match **`koskari/lib/eval.c`** static conventions.

### 3.1 `eval_event_yield`

**Inputs:** `ctx`, `out_ev` with **`KosEvent_KIND(out_ev) == KOS_EV_YIELD`**, payload owned per existing yield rules.

**Responsibilities:**

1. Clear or preserve **`KosContext_PY_RESUME_KIND`** per existing **`eval_phase_post_opcode`** contract.
2. Run **`kos_dyn_transition`** with **`KOS_TRANS_WALK_HANDLE_YIELD`** (current behavior).
3. If **`kos_dyn_need_kos_agen_boundary_deliver(ctx)`**, call **`eval_event_yield_deliver_to_boundary`** (or equivalent yield-only delivery).
4. Set **`KosContext_SUSPENSION_EVENT`** to **`KOS_SUSP_YIELD`** when execution will **stop** with yield; if **yield boundary delivery** **consumes** the event in-loop (**`out_ev`** cleared to **`KOS_EV_NONE`** inside deliver helpers), match **current** **`STOP_EVENT`** vs **continue** behavior exactly.

**Ownership:** Before **yield boundary delivery**, payload is owned by **`out_ev`**. Deliver helpers in **`dyn_next.c`** / **`dyn_foreach.c`** must **either** steal into stack/node, **or** **`Py_DECREF`** and clear **`KosEvent_PAYLOAD`**, matching **current** **`kos_dyn_next_deliver_yield`** behavior. **No** double free: **`eval_event_yield`** must not **`DECREF`** after a successful steal.

### 3.2 `eval_event_return`

**Inputs:** `ctx`, `out_ev` with **`KOS_EV_RETURN`**, payload = return value (new reference held by **`out_ev`** until consumed).

This helper is the **heart** of escaping overloaded **`RETURN`**. It is **not** used for the **top-level “eval finished”** case if that case must set **`KOS_CTX_DEAD`** without a parent frame; that can remain a **small** branch in **`eval_phase_post_opcode`** that **calls** the same **payload finalize** logic.

**Responsibilities (conceptual phases):**

1. **Classify** return: delimited vs dyn vs root. *If* delimited return is **fully** handled inside **`RETURN`** opcode (current **`kos_return_delimited`**), **`eval_event_return`** may **not** run for that path (opcode returns **`KOS_OP_PROLOGUE`** / plan). Document this exception explicitly in code comments.

2. **Unwind to frame** for the **returning** activation: **`KOS_TRANS_WALK_RETURN_TO_FRAME`** with **`target_frame`** and **`payload`** — **today** this starts inside **`RETURN.c`**. After refactor, either:
   - **A)** **`RETURN`** opcode sets **`out_ev`** only with payload and **`eval_event_return`** runs **`kos_dyn_transition`**; or  
   - **B)** **`RETURN`** opcode runs **one** transition to sync **finally** / **async unwind** state, then emits event — **avoid duplicating** transition **if** (A) is chosen; **single** transition owner must be **eval** or **opcode**, not both.

   **Recommendation:** **Eval** owns **`KOS_TRANS_WALK_RETURN_TO_FRAME`** for the **normal** (non-delimited) path so **opcode** does not embed **hundreds of lines** of unwind result handling.

3. **Async unwind started** (`KOS_DYN_UNWIND_ASYNC_STARTED`): stash **pending return value** and pending “return to parent” flag (**current** fields on **`KosContext`**). Same as today; **`eval_event_return`** centralizes the **if** ladder.

4. **Finally taking return:** continue eval at finally; **do not** pop frame yet.

5. **Pop frame** when unwind completes and callee frame detaches.

6. **Boundary delivery** (flattened Koskari async gen): if **NEXT** or **awaiting FOREACH** is active, deliver via **`kos_dyn_deliver_iterator_boundary_return_event`** / **`eval_event_return_deliver_to_boundary`** — **done** for the post-**`pop_frame`** slice; **parent** unwind and **push** still originate in **`RETURN.c`** until §12.

7. **Non-boundary parent:** native handler vs **`KosStackBlock_PushRef`** vs **`KOS_OP_RES_CONTROL_RETURNED`** path — **same** as today but **inside eval** after pop.

8. **Root / GEN-parent path:** emit **terminal** **`KOS_EV_RETURN`**, set **`KOS_CTX_DEAD`**, **`STOP_EVENT`**.

**Ownership:** **`KosEvent_PAYLOAD(out_ev)`** for **`KOS_EV_RETURN`** must follow **one** of:

- **Transferred** to parent stack (**push** steals reference),
- **Passed** to **native** handler (handler contract: steals or borrows — **document**),
- **Consumed** by **`kos_dyn_next_deliver_return`** (clears payload),
- **Discarded** when **`StopIteration`** path does not push ( **`Py_DECREF`** once).

**Failure mode:** If **two** code paths **`DECREF`** the same payload, tests may pass until a **rare** branch runs.

### 3.3 `eval_event_raise`

**Today**, **`PyErr_Occurred()`** in **`eval_phase_post_opcode`** routes to **`eval_step_after_process_raise`**. **`RAISE`** opcode and **`try_local`** / **`dynchain`** also produce **`KOS_EV_RAISE`** or set **`PyErr`**.

**Responsibilities:**

1. Package or normalize **exception** into **`out_ev`** when **`try_local`** / transfer plan expects **`KosEvent`** (**current** `eval.c` / **`dynchain.c`** behavior).
2. Run **unwind** / **transfer plan** steps in **one** order: **never** run **boundary node** delivery **and** full **sys.exc_info** propagation twice for the same **raise**.
3. Intersect with **capacity** nodes that **observe** errors (e.g. **preserve error across pop** caps) **without** replacing **PEP** semantics unless explicitly intended.

**Pitfall:** **RAISE** as **`KosEventKind`** vs **implicit** **`PyErr` only** — eval must treat **“event present”** and **“PyErr set”** as **one** coherent protocol (see §6).

---

## 4. Capacity-driven node dispatch

### 4.1 What “capacity” means here

Nodes expose **`KosDynNode_CAPS`** (and possibly finer flags per [dynamic-node-unified-api.md](../archive/dynamic-node-unified-api.md)). For this refactor:

| Capability (conceptual) | Participates in |
|-------------------------|-----------------|
| **Iterator boundary consumer** | **`KOS_EV_YIELD`** / **`KOS_EV_RETURN`** delivery from **flattened** Koskari async gen (**`KOS_DYN_NEXT`**, gated **`KOS_DYN_FOREACH`**) |
| **Control returned** | **`KOS_OP_RES_CONTROL_RETURNED`** / thunk completion (**WIND**, **WITH**, …) |
| **Error preservation** | Ordering when popping with **`PyErr`** set |

**Eval** should **discover** the target node via **existing** helpers (**`kos_dyn_find_next_above_frame`**, **`kos_dyn_find_awaiting_foreach`**) **or** a **generic** “walk until cap matches” if the chain gains more listener kinds (**`KOS_DYN_GEN`** completion).

### 4.2 Avoiding opcode-named branches in eval

**Bad:** `if (kos_dyn_find_next_above_frame(ctx)) { ... } else if (kos_dyn_find_awaiting_foreach(ctx)) { ... }` duplicated in **`RETURN.c`** and **`eval.c`**.

**Good:** **One** function **`kos_dispatch_iterator_boundary_event(ctx, kind, out_ev)`** (name in **`dynchain`**) invoked from **`eval_event_return`** and **`eval_event_yield`** after walks, using **caps** internally to select **NEXT** vs **FOREACH** implementation.

The **archived** plan already moved **delivery** into **`dyn_next.c`** / **`dyn_foreach.c`**; this step **removes the second caller** from **`RETURN.c`**.

---

## 5. Transfer plans and `KOS_CTX_IN_TRANSFER`

**Invariant:** **`eval_event_*`** helpers that may **start** or **complete** transfers must cooperate with **`eval_step_drain_transfer_plan`** and **`run_transfer_plan_to_completion`**.

**Pitfall:** Calling **iterator boundary delivery** while a **transfer plan** is **partially** applied can leave **`ctx`** in **`KOS_CTX_IN_TRANSFER`** with **stale** **`dyn_top`**. **Rule:** document **allowed** orderings: e.g. **drain transfer** before **boundary delivery**, or **boundary delivery** only when **plan** is **NULL** — **match** existing **`eval`** prologue order.

**Show-stopper:** If **`eval_event_return`** pops a frame **before** transfer state is updated, **continuation capture** tests break. Any move of **`pop_frame`** must preserve **`freeze-thaw`** and **segment** invariants (see [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md) if present).

---

## 6. RAISE vs PyErr: single contract

**Problem:** Opcodes sometimes set **`PyErr`** and return **error post tag**; sometimes **`KOS_EV_RAISE`** with structured fields; **`try_local`** constructs **`KosEvent`**.

**Requirement:** **`eval_phase_post_opcode`** entry should **normalize** to **one** of:

- **`PyErr` set** → **`eval_event_raise`** (or **`eval_step_after_process_raise`**) **without** a competing filled **`out_ev`**, or  
- **`KosEvent_KIND == KOS_EV_RAISE`** with **parallel** **`PyErr`** cleared per existing contract.

**Show-stopper:** **`eval_event_raise`** and **boundary delivery** both **clearing** or **replacing** **`PyErr`** causes **lost** exceptions or **double** **`PyErr_Dict`**.

---

## 7. Object and value ownership (summary table)

| Object | Typical owner before handler | After successful `eval_event_yield` (boundary path) | After `eval_event_return` (boundary path) | After terminal `STOP_EVENT` |
|--------|------------------------------|------------------------------------------------------|---------------------------------------------|------------------------------|
| **`KosEvent_PAYLOAD` (PyObject*)** | **`out_ev`** (one ref) | Stolen by stack push or **`DECREF`** in deliver | Stolen or cleared per **`dyn_next`/`foreach`** | Caller of **`koskari_eval`** owns **`out_ev`** payload |
| **Detached frame** | **`ctx`** chain | N/A | **Freed** or reparented per **`pop_frame`** | N/A |
| **`KOS_DYN_NEXT`** node | Chain | **Unlinked** after accept | **Unlinked** after accept | N/A |

**Rule:** Every **`eval_event_*`** branch must end in **exactly one** of: **stolen**, **DECREF**, **left in `out_ev`** for **caller**, or **borrowed** (document **borrow** explicitly; avoid if possible).

---

## 8. YIELD_FROM and nested `koskari_eval`

**[`YIELD_FROM.c`](koskari/lib/opcode/YIELD_FROM.c)** runs a **nested** **`koskari_eval`** and **switches** on **`KosEvent`**. That is **not** the same as the **main** loop’s **`eval_event_*`**, but the **nested** eval **must** produce **events** consistent with **main** eval so **behavior** matches.

**Pitfall:** Refactoring **main** **`eval_event_return`** without auditing **YIELD_FROM** can cause **double** pop or **wrong** **`in_progress`** flags on **`KosPyAsyncGen`**.

**Mitigation:** Treat **YIELD_FROM** as a **client** of the **same** **`KosEvent`** contracts; optionally route nested completion through **shared** static helpers in **`eval.c`** **exported** to **`YIELD_FROM.c`** only if **linking** / layering allows (or duplicate **thin** wrappers with **shared** comments).

---

## 9. Pitfalls and failure modes (explicit)

1. **Two transitions for one RETURN** — Opcode and **`eval_event_return`** both run **`KOS_TRANS_WALK_RETURN_TO_FRAME`**: **undefined** stack/`finally` state.

2. **Boundary delivery without walk** — **Yield** boundary delivery expects **GEN** freeze / **caller** stack layout already established by **`walk_handle_yield`**; **RETURN** path must not call **return** delivery with **wrong** **`CURRENT_FRAME`** relative to **NEXT/FOREACH**.

3. **`out_ev` left `KOS_EV_NONE` with non-zero `op_res`** — **RuntimeError** today; **`eval_event_*`** must set kind **before** any early return.

4. **`KOS_OP_RES_CONTROL_RETURNED` vs `KOS_EV_RETURN`** — **WIND** completion uses **integer** op result; do **not** conflate with **iterator boundary** without ordering: **`kos_dynnode_on_control_returned`** may need to run **before** or **after** **`eval_event_return`** depending on **dyn_top**; **document** chosen order.

5. **Native parent handler** — Must still receive **return value** with **correct** refcount **after** **`eval_event_return`**; regression in **embedding** / **native** tests.

6. **Delimited continuation** — **`kos_return_delimited`** may **install** plans; **`eval_event_return`** must **not** assume **pop_frame** always runs.

7. **FOREACH phase gating** — **`KOS_DYN_FOREACH`** must **ignore** spurious **RETURN** events when **not** in **awaiting iter step** phase (same as **YIELD** gating).

8. **`clone` / continuation** — Any new **state** moved from **`RETURN`** into **eval** must participate in **dyn chain** **clone** if **continuations** capture **mid-return** (rare; verify test coverage).

---

## 10. Implementation phases (suggested)

1. **Extract** **`eval_event_yield`** from **`eval_phase_post_opcode`** verbatim (no behavior change).

2. **Extract** **`eval_event_return`** body **empty** for paths **not** yet moved; **call** from **`eval_phase_post_opcode`** only for **new** opcode tagging if needed — **or** first **change** **`RETURN`** to emit **`KOS_EV_RETURN`** + **`POST`** for **inner** returns and **route** through **`eval_event_return`** incrementally.

3. **Move** **`KOS_TRANS_WALK_RETURN_TO_FRAME`** + **pop_frame** + **inline async-gen block** from **`RETURN.c`** into **`eval_event_return`**, **deleting** duplicate **NEXT/FOREACH** logic from opcode.

4. **Centralize** iterator boundary delivery: only from **`eval_event_yield`** (yield half) / **`eval_event_return_deliver_to_boundary`** and **`kos_dyn_try_deliver_iterator_boundary_after_pop`** (opcode after **pop**), sharing **`kos_dyn_deliver_iterator_boundary_return_event`**.

5. **Extract** **`eval_event_raise`** from **`eval_step_after_process_raise`** + **`eval_phase_post_opcode`** **PyErr** branch; align **`RAISE`** opcode to **prefer** this path.

6. **Full** **`tox`** and **`make test-valgrind`** (per project rules).

---

## 11. Success criteria

These criteria describe the **finished** refactor. The tree is **partially** there (see the **Implementation status** section below); **`RETURN`** Phase **2** migration is **not** complete until §12 proposals are executed.

- **`RETURN.c`** contains **operand decoding**, optional **delimited** shortcut only if that remains opcode-local by policy, and **minimal** machine tagging — **no** **`kos_dyn_transition`** / **`kos_dyn_pop_frame`** for the normal path once **`eval_event_return`** owns Phase **2b/2c**.
- **`eval_phase_post_opcode`** delegates **YIELD/RETURN/RAISE** complexity to **`eval_event_*`** helpers for **all** opcode-emitted boundary outcomes.
- **Single** implementation of **flattened** async-gen **return** delivery (**`dyn_next` / `dyn_foreach`** only) — **met** for delivery **after** **`pop_frame`**; unwind still in opcode.
- **No** new **`KosContext`** mirrors of **NEXT/FOREACH** state.
- **Green** regression suite for **NEXT**, **ITER_NEXT**, **FOREACH**, **YIELD_FROM**, **try/finally**, **native** returns.

---

## Implementation status (partial, 2026-04)

**Summary:** Event-driven **post-opcode** paths and **shared dynchain** helpers are in place; **`RETURN`** is still a **fat opcode** for everything except the **iterator boundary return** slice. Do **not** read older sections of this doc as “fully implemented” without cross-checking this section.

**Implemented**

- **Iterator RETURN delivery unified:** [`kos_dyn_deliver_iterator_boundary_return_event`](../../koskari/lib/dynchain/dyn_next.c), [`kos_dyn_try_deliver_iterator_boundary_after_pop`](../../koskari/lib/dynchain/dyn_next.c), and [`eval_event_return_deliver_to_boundary`](../../koskari/lib/eval.c) — same semantics whether **`RETURN`** runs delivery after **`pop_frame`** or **`eval_phase_post_opcode`** handles **`KOS_EV_RETURN`** with **`kos_dyn_need_kos_agen_boundary_deliver`**.
- **`eval_event_yield`** — post-opcode **`KOS_EV_YIELD`** ([`eval.c`](../../koskari/lib/eval.c)).
- **`eval_event_raise`** — thin wrapper used from **`eval_phase_post_opcode`** and **`eval_phase_pending_exception`** ([`eval.c`](../../koskari/lib/eval.c)).
- Comment on **`kos_dynnode_on_control_returned`**: iterator boundaries use eval/dynchain helpers, not that hook ([`dynchain.c`](../../koskari/lib/dynchain.c)).

**Not implemented (still in [`RETURN.c`](../../koskari/lib/opcode/RETURN.c) Phase 2)**

| Phase in `RETURN.c` | What still runs in the opcode |
|---------------------|-------------------------------|
| **2a** | **`kos_return_delimited`** (reset/prompt **k(value)** path). |
| **2b** | **`KOS_TRANS_WALK_RETURN_TO_FRAME`**, async-unwind / **finally**-taking-return branches, **`kos_dyn_pop_frame`**, **native** handler delivery, stack **push**, **`KOS_OP_RES_CONTROL_RETURNED`**. Only the **flattened agen** tail after **`pop_frame`** is delegated to **`kos_dyn_try_deliver_iterator_boundary_after_pop`**. |
| **2c** | Second **`KOS_TRANS_WALK_RETURN_TO_FRAME`** block and terminal **`KOS_EV_RETURN`** / **`KOS_CTX_DEAD`** for root / **GEN**-parent completion. |

**Verification (snapshot):** `tox -qe test` green; native **`make test-valgrind`**: tests pass; known valgrind leak baseline unchanged for this tree.

---

## 12. Migration proposals: shifting `RETURN` Phase 2 into eval handlers

The following are **design options** for completing the move so **`eval_event_return`** (or a small family of **`eval_event_return_*`** statics) owns the same ordering contract as §3.2. They can be combined; order is a suggested dependency order.

### Proposal A — Opcode emits **`KOS_EV_RETURN` + POST**, eval owns unwind

**Idea:** After Phase **1** (operands on **`out_ev`** or in a **single** scratch **`PyObject *`** owned by **`out_ev`**), **`RETURN`** returns **`KOS_OP_POST_SUSPEND`** (or a dedicated **post tag**) with **`KosEvent_KIND` = **`KOS_EV_RETURN`** and payload = value, **without** calling **`kos_dyn_transition`**.

**`eval_phase_post_opcode`** dispatches to **`eval_event_return`**, which runs **2a/2b/2c** classification (delimited vs nested vs root), **`KOS_TRANS_WALK_RETURN_TO_FRAME`**, **`pop_frame`**, iterator boundary (**existing** helpers), parent delivery, terminal **STOP_EVENT**.

**Pros:** Single owner for **`KOS_TRANS_WALK_RETURN_TO_FRAME`** (§9.1). **Cons:** Requires **`CURRENT_FRAME`** to still be the **callee** at **post_opcode** entry (true today); careful with **PC advance** and **acting_pc** if the opcode does **not** mutate chain before POST.

### Proposal B — Two-step return: “begin return” vs “finish after unwind”

**Idea:** Split **`eval_event_return`** into **`eval_event_return_begin`** (run **walk_return** only; may **resume** at **finally** without **pop**) and **`eval_event_return_complete`** (after frame is detached, run **iterator** + **parent** delivery), wired through **existing** **`KosEvalStep`** / **REPROLOGUE** flow.

**Pros:** Matches the **natural** **`KOS_DYN_UNWIND_*`** state machine. **Cons:** More **surface area** in the eval loop; must not **double**-**pop** or **double**-**deliver**.

### Proposal C — Delimited **2a** stays opcode-local or moves to **`eval_event_return`**

**Idea:** Either **keep** **`kos_return_delimited`** in **`RETURN`** (documented **exception**: no **`KOS_EV_RETURN`** until plan completes) **or** treat delimited like **A** with a **synthetic** internal kind **`KOS_EV_RETURN_DELIMITED`** consumed only by **`eval_event_return`**.

**Pros:** Avoids **prompt** / **transfer-plan** ordering bugs. **Cons:** Extra **event** kind or **branch** complexity.

### Proposal D — Terminal **2c** only from eval

**Idea:** Narrow migration: move **only** Phase **2c** (root **`KOS_EV_RETURN`**, **`KOS_CTX_DEAD`**) into **`eval_event_return`** or **`eval_phase_post_opcode`**, leaving **2b** until **A** or **B**.

**Pros:** Smaller **diff**, proves **payload** / **ownership** plumbing. **Cons:** **Two** owners of **walk_return** temporarily — must document and **test** heavily.

### Proposal E — **`eval_event_return_parent_delivery`**

**Idea:** Extract **native** handler + **`KosStackBlock_PushRef`** + **`KOS_OP_RES_CONTROL_RETURNED`** (current tail of **2b**) into **`eval_event_return_parent_delivery(ctx, val, out_ev)`** called from **`eval_event_return`** after **pop** and optional **iterator** delivery — even if **walk** stays in opcode one milestone longer.

**Pros:** Shrinks **`RETURN.c`** and centralizes **refcount** rules for **§7**. **Cons:** Still need **opcode** or **A** to reach this function without **split** ownership of **val**.

### Ordering and invariants (all proposals)

- Preserve **§5** transfer-plan ordering relative to **pop** and **boundary** delivery.
- **Exactly one** **`KOS_TRANS_WALK_RETURN_TO_FRAME`** per **logical** return (§9.1).
- Run **full** **`tox -qe test`** and **native** tests after each milestone; **YIELD_FROM** and **nested** **`koskari_eval`** (§8) must be re-audited when **`RETURN`** stops setting **terminal** events from the opcode path.

---

## Document history

- **2026-04-16** — Initial draft: eval event handlers, RETURN escape, RAISE/YIELD unification, ownership and pitfalls.
- **2026-04-16** — Implementation status: shared iterator RETURN delivery, `eval_event_*` extractions.
- **2026-04-16** — Partial implementation called out; `RETURN` Phase 2 mapping; §12 migration proposals A–E.
