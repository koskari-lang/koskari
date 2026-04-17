# Event-driven eval handlers: RETURN escape, RAISE, YIELD, and node dispatch

This document specifies the **next** refactor after the completed NEXT boundary milestone (delivered: `KOS_DYN_NEXT`, FOREACH phases, `dispatch_boundary_event`, removal of `KosContext.pending_agen*`). Background: [event-driven-node-delivery.md](../archive/event-driven-node-delivery.md). It focuses on **escaping the overloaded `RETURN` opcode**, unifying **RAISE**, **RETURN**, and **YIELD** behind **eval-owned** routing, and introducing **`eval_event_*`** helpers that encapsulate non-trivial handling (transitions, transfer plans, dyn walks).

Companion documents: [evaluator.md](evaluator.md), [generic-dynnode-protocol.md](generic-dynnode-protocol.md).

---

## 1. Motivation

### 1.1 What is still wrong after NEXT delivery

The standalone **`NEXT`** / **`ITER_NEXT`** boundary is largely **event-driven**: **`YIELD`** and (when applicable) **`RETURN`** surface as **`KosEvent`**, then **`eval_phase_post_opcode`** runs **`kos_dyn_transition`** for yield walks and **`dispatch_boundary_event`** for flattened Koskari async-generator delivery to **`KOS_DYN_NEXT`** or **`KOS_DYN_FOREACH`**.

**`RETURN`** remains **overloaded**. In [`koskari/lib/opcode/RETURN.c`](koskari/lib/opcode/RETURN.c) the handler:

- Pops stack operands and builds the return value (appropriate for an opcode).
- Runs **delimited** return (`kos_return_delimited`) when prompts are active.
- For a frame with a **non-GEN** parent, runs a full **`KOS_TRANS_WALK_RETURN_TO_FRAME`**, **`kos_dyn_pop_frame`**, then contains **inline** policy for **flattened async gen exhaustion** (unlink **`KOS_DYN_NEXT`**, **`StopIteration`** vs default value, **`KOS_DYN_FOREACH`** else-PC and phase) that **duplicates** the semantics already implemented in **`kos_dyn_next_deliver_return`** / **`kos_dyn_foreach_deliver_return_kos_agen`** (used from **`dispatch_boundary_event`**).
- Delivers to **native** parent handlers or pushes on the parent stack, or emits **`KOS_OP_RES_CONTROL_RETURNED`** for **WIND** / thunk completion.
- Only the **root / GEN-parent** path emits **`KOS_EV_RETURN`** and clears **`KOS_CTX_RUNNING`**.

That split means **one logical story** (“return crossed a consumer boundary”) is implemented in **two places** (opcode synchronous path vs eval **`dispatch_boundary_event`**), which blocks a clean **handler-based** model.

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

**Invariant:** Opcodes **do not** call **`kos_dyn_next_deliver_return`** directly. **Eval** (or a **dynchain** function invoked **only** from eval) does.

---

## 3. `eval_event_*` helpers (proposed responsibilities)

Names are **proposed**; final names should match **`koskari/lib/eval.c`** static conventions.

### 3.1 `eval_event_yield`

**Inputs:** `ctx`, `out_ev` with **`KosEvent_KIND(out_ev) == KOS_EV_YIELD`**, payload owned per existing yield rules.

**Responsibilities:**

1. Clear or preserve **`KosContext_PY_RESUME_KIND`** per existing **`eval_phase_post_opcode`** contract.
2. Run **`kos_dyn_transition`** with **`KOS_TRANS_WALK_HANDLE_YIELD`** (current behavior).
3. If **`kos_dyn_need_kos_agen_boundary_deliver(ctx)`**, call **`dispatch_boundary_event(ctx, out_ev)`** (or inline equivalent once unified).
4. Set **`KosContext_SUSPENSION_EVENT`** to **`KOS_SUSP_YIELD`** when execution will **stop** with yield; if **`dispatch_boundary_event`** **consumes** the event in-loop (**`out_ev`** cleared to **`KOS_EV_NONE`** inside deliver helpers), match **current** **`STOP_EVENT`** vs **continue** behavior exactly.

**Ownership:** Before **`dispatch_boundary_event`**, payload is owned by **`out_ev`**. Deliver helpers in **`dyn_next.c`** / **`dyn_foreach.c`** must **either** steal into stack/node, **or** **`Py_DECREF`** and clear **`KosEvent_PAYLOAD`**, matching **current** **`kos_dyn_next_deliver_yield`** behavior. **No** double free: **`eval_event_yield`** must not **`DECREF`** after a successful steal.

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

6. **Boundary delivery** (flattened Koskari async gen): if **NEXT** or **awaiting FOREACH** is active, call **`dispatch_boundary_event`** with **`KOS_EV_RETURN`** — **replacing** the **inline** block in **`RETURN.c`** (lines that touch **`pending_next`**, **`pending_foreach`**, **`agen`**, default **`StopIteration`**).

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

Nodes expose **`KosDynNode_CAPS`** (and possibly finer flags per [dynamic-node-unified-api.md](dynamic-node-unified-api.md)). For this refactor:

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

**Pitfall:** Calling **`dispatch_boundary_event`** while a **transfer plan** is **partially** applied can leave **`ctx`** in **`KOS_CTX_IN_TRANSFER`** with **stale** **`dyn_top`**. **Rule:** document **allowed** orderings: e.g. **drain transfer** before **boundary delivery**, or **boundary delivery** only when **plan** is **NULL** — **match** existing **`eval`** prologue order.

**Show-stopper:** If **`eval_event_return`** pops a frame **before** transfer state is updated, **continuation capture** tests break. Any move of **`pop_frame`** must preserve **`freeze-thaw`** and **segment** invariants (see [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md) if present).

---

## 6. RAISE vs PyErr: single contract

**Problem:** Opcodes sometimes set **`PyErr`** and return **error post tag**; sometimes **`KOS_EV_RAISE`** with structured fields; **`try_local`** constructs **`KosEvent`**.

**Requirement:** **`eval_phase_post_opcode`** entry should **normalize** to **one** of:

- **`PyErr` set** → **`eval_event_raise`** (or **`eval_step_after_process_raise`**) **without** a competing filled **`out_ev`**, or  
- **`KosEvent_KIND == KOS_EV_RAISE`** with **parallel** **`PyErr`** cleared per existing contract.

**Show-stopper:** **`eval_event_raise`** and **`dispatch_boundary_event`** both **clearing** or **replacing** **`PyErr`** causes **lost** exceptions or **double** **`PyErr_Dict`**.

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

2. **Boundary delivery without walk** — **`dispatch_boundary_event`** expects **GEN** freeze / **caller** stack layout already established by **`walk_handle_yield`** for **YIELD**; **RETURN** path must not call it with **wrong** **`CURRENT_FRAME`**.

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

4. **Unify** **`dispatch_boundary_event`** invocation: only from **`eval_event_yield`** / **`eval_event_return`**.

5. **Extract** **`eval_event_raise`** from **`eval_step_after_process_raise`** + **`eval_phase_post_opcode`** **PyErr** branch; align **`RAISE`** opcode to **prefer** this path.

6. **Full** **`tox`** and **`make test-valgrind`** (per project rules).

---

## 11. Success criteria

- **`RETURN.c`** contains **operand decoding**, **delimited** shortcut, and **minimal** tagging — **no** **`kos_dyn_find_next_above_frame`** in opcode.
- **`eval_phase_post_opcode`** delegates **YIELD/RETURN/RAISE** complexity to **`eval_event_*`** helpers.
- **Single** implementation of **flattened** async-gen **return** delivery (**`dyn_next` / `dyn_foreach`** only).
- **No** new **`KosContext`** mirrors of **NEXT/FOREACH** state.
- **Green** regression suite for **NEXT**, **ITER_NEXT**, **FOREACH**, **YIELD_FROM**, **try/finally**, **native** returns.

---

## Document history

- **2026-04-16** — Initial draft: eval event handlers, RETURN escape, RAISE/YIELD unification, ownership and pitfalls.
