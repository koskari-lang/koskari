# Interrupted and failed transfer plans

**Status:** Implemented (archived 2026-06-22). Nested continuation interrupt (§5),
failed-transfer abandonment (§4), delimited nested-interrupt forbid (§6.6), and
PUSH_SHIFT heritage capture are **landed**. Canonical summary:
[EVALUATOR.md](../EVALUATOR.md) §6. **Open follow-up:** async nested await
during transfer with non-empty `plan_stack` — see [TODO.md](../TODO.md)
(Continuations / transfer plans) and §6.7 below.

**Audience:** Maintainers and agents implementing §6.6–6.7; reviewers of
continuation + dyn-wind integration.

**Repository root:** `sibilant-ng/`

**Last updated:** 2026-06-10

**Related documents:**

| Document | Relationship |
|----------|----------------|
| [../TODO.md](../TODO.md) | Status rollup; follow-up items |
| [callcc.md](callcc.md) | Re-entrant transfers, plan structure, `original_target` |
| [delimited-invoke-model.md](delimited-invoke-model.md) | Target delimited invoke (lineage fork, double-wind fix) |
| [../EVALUATOR.md](../EVALUATOR.md) | Eval loop, `revive_plan`, transfer draining |
| [../INVARIANTS-AND-CLARIFICATIONS.md](../INVARIANTS-AND-CLARIFICATIONS.md) | `plan` / `plan_stack`, transfer atomicity |
| [kos_eval_state_api_migration.md](kos_eval_state_api_migration.md) | `SuspendActivePlan` / `ResumeSuspendedPlan` |
| [../api/dynchain_ng/DYNCHAIN_USAGE_INVENTORY.md](../api/dynchain_ng/DYNCHAIN_USAGE_INVENTORY.md) | Symbol inventory |
| [extent-wind-unwind.md](../interpreter/extent-wind-unwind.md) | Wind/unwind thunk execution model |

---

## How to use this document (agent bootstrap)

| If you need to… | Read |
|-----------------|------|
| See what is implemented | §2 Implementation status |
| Understand the in-place migration model (why failed transfers dispatch on the manifested chain) | §3 Architectural decision |
| Review failed/exception transfer behavior (landed) | §4 Failed transfer abandonment (done) |
| Understand nested continuation interrupt | §5 Nested continuation interrupt (done) |
| Reference data structures and runtime flow | §6 Runtime reference |
| Implement delimited nested-interrupt forbid | §6.6 — **done** |
| Implement async nested await coverage (later) | §6.7 |
| Run regression tests | §7 Test plan |
| Acceptance criteria | §8 Acceptance criteria |

**Do not** abandon and rewrite the transfer machinery. Failed-transfer handling
layers on nested interrupt via **`kos_abandon_transfer_nest`** + normal exception
dispatch, with a carve-out for foreign cc-unwind signal re-emit (§4.3).

**Do not commit** unless the user explicitly requests it (repository git rules).

---

## 1. Problem statement

A **transfer plan** (`KosTransferPlan`) orchestrates continuation application:
unwind hooks, `INSTALL` (or `PUSH` for delimited), wind hooks, `INJECT`.

Two distinct interruption kinds exist:

### 1.1 Continuation interrupt (nested invoke) — **DONE**

While a plan is active, a wind/unwind thunk (or bytecode) invokes **another**
continuation. The outer plan must be **suspended**, the inner transfer must run
to completion, then the outer plan must be **replanned** from the *current*
dynamic chain to its `original_target`.

**Undelimited (`let/cc`) plans** use `kos_build_direct_install_plan` (INSTALL +
wind + INJECT). **Delimited (`reset`/`shift`) PUSH plans** are **not** replanned
correctly today; §6.6 **forbids** nested invoke with an explicit `RuntimeError`
instead of silent wrong topology (**landed**).

### 1.2 Failed transfer (exception during plan) — **DONE**

While a plan is stepping, any step may fail with an exception — notably a
before/after thunk raising during wind/unwind, or a synchronous failure in
`kos_run_transfer_step`. The plan nest is **abandoned** (disarmed + `plan_stack`
freed). Exception dispatch proceeds on the **manifested chain** via
`try_local_exception_handler`; if a handler applies, the eval loop continues.
Unhandled exceptions still surface as **`KOS_EV_RAISE`**. Foreign cc-unwind
sentinel re-emit is **not** a failure — see §4.3 carve-out.

**Explicit non-goal for now:** transactional/scratch-space transfer (build full
target chain off to the side, then swap atomically). Transfer is **in-place
migration** step by step; see §3.

---

## 2. Implementation status

### 2.1 Done — nested continuation interrupt (§5)

| Item | Location | Notes |
|------|----------|-------|
| Suspend on nested invoke | `kos_invoke_continuation` → `kos_suspend_transfer_plan` | `dynchain/xfer_plan.c` |
| Resume + replan loop | `run_transfer_plan_to_completion` | `eval/eval_chain.c` |
| UAF fix: clone `original_target` before disarm | replan block in `eval_chain.c` | `target_keep` via `kos_clone_chain_segment` |
| `graft_point_id` on `KosTransferPlan` | `dynchain.h`, plan builders, invoke | preserved on replan |
| `resume_frame = NULL` on replan | `eval_chain.c` | INSTALL finds frame from segment |
| `kos_free_transfer_plan_stack` | `dynchain/xfer_plan.c`, `dynchain.h` | called on abandon / fatal paths |
| `dyn_wind` nested-transfer guards | `dyn_wind.c` | `on_chain`, EXTENT_ACTION matching, safe stack pops |
| `PRE_AFTER` unwind guard | `dyn_wind.c` | no double after-thunk during nested unwind |
| `action_index++` on after-thunk return | `dyn_wind.c` | guarded to matching EXTENT_ACTION |
| Tests: `TestDynWindContinuations` (3) | `test_dyn_wind.py` | all pass |

### 2.2 Done — failed transfer abandonment (§4)

| Item | Location | Notes |
|------|----------|-------|
| `kos_abandon_transfer_nest` | `dynchain/xfer_plan.c`, `dynchain.h` | disarm + `kos_free_transfer_plan_stack` |
| `abandon_xfer_and_try_local` | `eval_chain.c` | abandon then `try_local_exception_handler` |
| `plan_res < 0` → abandon + `try_local` | `run_transfer_plan_to_completion` | return `1` if handler applied |
| cc-unwind signal carve-out | `eval_chain.c` | `xfer_plan_step_is_cc_raise_reemit` → `KOS_EV_RAISE`, not `try_local` |
| Replan clone / `direct_install` failures | `eval_chain.c` | same abandon + `try_local` |
| Abandon on `PyErr` during suspended thunk | `process_raise` | skip when `KoskariContinuationApply` |
| Abandon on entry `KOS_MODE_EXCEPTION` | `eval_phase_entry` | before `try_local` |
| Prologue ordering | `cg_prologue` | `eval_phase_pending_exception` before plan drain |
| `try_local` when `dyn_top == frame_top` | `kos_dyn_try_local_raise_body` | `try_local_extent_on_current_frame` (extent table) |
| Caller plumbing return `1` | `eval_phase_transfer`, `eval_step_drain_transfer_plan` | CONTINUE / REPROLOGUE on handler applied |
| Tests: `TestDynWindTransferFailures` (2) | `test_dyn_wind.py` | Python + Koskari raise in before-thunk under `try` |
| Tests: `TestDynWindTransferWindReproducer` (1) | `test_dyn_wind.py` | before-thunk runs twice on transfer |
| Foreign apply + dyn-with | `test_continuations.py` | `__exit__` sees no exception during cc-unwind |

### 2.3 Done — delimited nested-interrupt fail-fast (§6.6)

| Item | Location | Notes |
|------|----------|-------|
| `kos_xfer_plan_is_delimited_push` | `dynchain/xfer_plan.c`, `dynchain.h` | detects PUSH in active plan |
| Invoke-time forbid + abandon | `kos_invoke_continuation` (`xfer_plan.c`) | inline `kos_xfer_plan_is_delimited_push` check |
| Replan catch-all | `eval/eval_chain.c` | inline check before `kos_build_direct_install_plan` |
| `handle_continuation_apply` abandon | `eval.c` | on invoke failure when plan/stack active |
| `kos_abandon_transfer_nest` | `dynchain/xfer_plan.c`, `dynchain.h` | exported; clears nest |
| Test: `TestDelimitedTransferNestedForbid` | `test_continuations.py` | green |

### 2.4 Done — PUSH_SHIFT heritage capture

| Item | Location | Notes |
|------|----------|-------|
| In-place capture only | `PUSH_SHIFT.c` | no peel-to-prompt transfer plan |
| Heritage gating | `kos_shift_capture` | `kos_validate_capture` + `handle(SHIFT)` walk |
| Segment includes dyn_top..prompt | `kos_shift_capture` | WIND and other heritage nodes in template |
| `POP_SHIFT` extent lookup | `POP_SHIFT.c` | walks ancestors to enclosing PROMPT extent |
| In-place shift capture | `dynchain.c`, `xfer_plan.c` | `kos_continuation_capture_segment`; no peel unwind plan |
| Regression | `test_reset_shift.py` | `test_dyn_wind_around_shift_captures_in_place`, `test_dyn_wind_around_shift_invoke_after_capture`, `test_dyn_wind_around_shift_invoke_inside_shift_body` (green) |

### 2.5 Next work (not started)

| Item | Priority | Notes |
|------|----------|-------|
| Delimited invoke two-path model | P1 | [delimited-invoke-model.md](delimited-invoke-model.md); in-shift vs remote invoke |
| Async nested await + `plan_stack` | P1 | §6.7 |
| Full delimited PUSH replan (real fix) | **deferred** | replace §6.6 forbid when implemented |

### 2.6 Known gap (why §6.6 exists)

The replan loop in `run_transfer_plan_to_completion` always calls
`kos_build_direct_install_plan` (INSTALL semantics). A suspended **delimited**
outer plan was built with `kos_build_delimited_push_plan` (PUSH semantics:
caller chain preserved, `KOS_DYN_DELIM` return metadata). Replanning with
`direct_install` can produce **unknown/wrong chain topology**. Until a correct
PUSH replan exists, **fail closed** with `RuntimeError` (§6.6).

### 2.7 Files touched (landed work)

| File | Role |
|------|------|
| `koskari/lib/eval/eval_chain.c` | `run_transfer_plan_to_completion`, replan loop, `abandon_xfer_and_try_local`, cc-unwind carve-out |
| `koskari/lib/eval.c` | `process_raise` abandon guard, prologue ordering, caller plumbing |
| `koskari/lib/dynchain.c` | shell, `kos_continuation_capture_segment`, `#include` of walk and xfer units |
| `koskari/lib/dynchain/dyn_walk.c` | pop/unwind walks, `kos_dyn_try_local_raise_body`, extent-on-frame helper |
| `koskari/lib/dynchain/xfer_plan.c` | plan build (`KosXferPlanOptions`), step execution, suspend/resume, invoke, abandon nest |
| `koskari/lib/dynchain.h` | `graft_point_id`, `kos_xfer_plan_is_delimited_push`, `kos_abandon_transfer_nest` |
| `koskari/lib/dynchain/dyn_wind.c` | nested-transfer CONTROL_RETURNED guards |
| `koskari/lib/opcode/PUSH_SHIFT.c` | in-place capture via `kos_shift_capture` |
| `koskari/lib/opcode/POP_SHIFT.c` | PROMPT extent ancestor walk |
| `tests/compile/specials/test_dyn_wind.py` | `TestDynWindContinuations`, `TestDynWindTransferFailures`, `TestDynWindTransferWindReproducer` |
| `tests/compile/specials/test_continuations.py` | `TestDelimitedTransferNestedForbid` |
| `tests/compile/specials/test_reset_shift.py` | dyn-wind-around-shift capture regression |

---

## 3. Architectural decision — in-place migration

**Chosen model:** Transfer plans **mutate live eval state** step by step. There is
no scratch dynchain that is swapped in atomically at the end.

Each plan step updates the real `dyn_top`, `frame_top`, stack, and PC:

1. **Unwind** `EXTENT_ACTION`s peel off the invoke-site side toward LCA.
2. **INSTALL** splices a cloned captured segment (one large step, still on the
   live chain).
3. **Wind** `EXTENT_ACTION`s enter extents and may run before-thunk frames.
4. **INJECT** pushes the resume value.

Once step 1 begins, the evaluator has **left** the pure invoke-site situation.
After INSTALL, the chain is materially the destination topology. This is
expected continuation semantics: a **migration**, not a function call that
returns to the invoke site.

### 3.1 Implications for failure

| Approach | Requires | Fits current code? |
|----------|----------|-------------------|
| Raise at invoke site as if transfer never happened | Transactional undo or scratch build | **No** — would need revamp |
| Raise on the **manifested chain** where the fault occurred | Abandon plan + normal `try_local` | **Yes** — minimal addition |

**Undelimited `let/cc`:** the invoke site does **not** expect a return value on
success (control transfers away). On failure, there is similarly no caller
waiting for a callback at the invoke frame — the honest failure context is the
chain you have already built.

**Not the same as `kos_build_cc_unwind_plan`:** that path (foreign /
cross-context `KoskariContinuationApply`) unwinds extents then re-raises. It is
a special case. General mid-transfer failure should use **abandon + local raise
dispatch**, not necessarily full unwind-to-root before every handler attempt.

### 3.2 Wind thunk execution (not separate eval loops)

Before/after thunks run in the **same** `koskari_eval` loop via flattened CALL
(`KosEvalState_CallNoArgs` pushes a child frame on the dyn chain). They are **not**
separate `koskari_eval` invocations.

Two failure shapes:

| Shape | How it happens | Behavior (landed) |
|-------|----------------|-------------------|
| **Sync call failure** | `CallNoArgs` returns `-1` before thunk bytecode runs | `plan_res < 0` → abandon + `try_local` (or `KOS_EV_RAISE` if unhandled) |
| **Thunk bytecode raises** | Plan suspended (`KOS_XFER_SUSPENDED_AWAIT`); opcode loop runs thunk; `PyErr` in post_opcode | prologue `eval_phase_pending_exception` → `process_raise` abandons plan → `try_local` |

**Known gap:** exception policy is still split across `process_raise`, `eval_phase_entry`,
and `run_transfer_plan_to_completion` rather than one event-driven handler. See
[archive/kosevent-elimination.md](../archive/kosevent-elimination.md).

---

## 4. Failed transfer abandonment (done)

This section documents the **landed** behavior. For the original bootstrap plan,
see git history on `interrupted_transfer`.

### 4.1 Goal (one sentence)

On **any** transfer failure while a plan is active or suspended, **abandon the
entire transfer nest**, then **dispatch the exception through normal local raise
handling** on the current manifested chain; if a handler applies, **continue the
eval loop**; if not, emit `KOS_EV_RAISE` as today.

### 4.2 Core helper: `kos_abandon_transfer_nest`

**Location:** `dynchain/xfer_plan.c`, declared in `dynchain.h`. Paired with
**`abandon_xfer_and_try_local`** (static in `eval/eval_chain.c`).

**Behavior:**

1. `KosEvalState_DisarmTransferPlan(es)` — clears `PLAN`, `IN_TRANSFER`
2. `kos_free_transfer_plan_stack(es)` — frees all suspended outers
3. **Do not** clear `PyErr`
4. **Do not** mutate `dyn_top` / `frame_top` / stack (manifested chain stays)

Idempotent if called when no plan active (disarm no-op, stack walk empty).

### 4.3 Call site A — sync step failure in `run_transfer_plan_to_completion`

**File:** `koskari/lib/eval/eval_chain.c`

On `plan_res < 0` (and replan allocation failures), **`abandon_xfer_and_try_local`**
runs unless the failing step is a **`KOS_DYN_CC_RAISE`** `EXTENT_ACTION` (foreign
cc-unwind sentinel re-emit). That carve-out calls **`xfer_emit_cc_unwind_signal`**
(`kos_eval_emit_raise_from_pyerr` + disarm) — **not** `try_local`. See
`test_continuation_passes_through_intermediate_context_with_dyn_with_unwind`.

**Return code convention** for `run_transfer_plan_to_completion`:

| Return | Meaning |
|--------|---------|
| `0` | Plan drained successfully (or suspended on await — existing) |
| `1` | Plan abandoned; handler applied; caller must continue eval loop |
| `-1` | Unhandled raise or fatal error; caller stops with `out_ev` |

Apply the same abandon + `try_local` pattern on **replan clone failure** and
**`kos_build_direct_install_plan` failure** paths (after freeing `target_keep`
if allocated), not only `plan_res < 0`.

**Exception:** when `plan_res < 0` on a `KOS_DYN_CC_RAISE` `EXTENT_ACTION`
(foreign cc-unwind sentinel re-emit), emit `KOS_EV_RAISE` via
`kos_eval_emit_raise_from_pyerr` and disarm the nest — do **not** call
`try_local`. See §3.1.

### 4.4 Call site B — exception during suspended transfer (thunk in opcode loop)

**Landed in two places:**

1. **`process_raise`** (`eval.c`): abandons plan nest when active, **except** when
   PyErr is **`KoskariContinuationApply`** (nested interrupt / foreign apply arms a
   new plan).
2. **`cg_prologue`** (`eval.c`): **`eval_phase_pending_exception`** runs **before**
   **`eval_step_drain_transfer_plan`**, so a thunk raise under
   **`KOS_XFER_SUSPENDED_AWAIT`** abandons via `process_raise` before the wind
   `EXTENT_ACTION` can complete with PyErr still set.

**`eval_phase_entry`** (`KOS_MODE_EXCEPTION`): also abandons plan nest before
`try_local` when reviving with `throw_exc`.

### 4.5 Caller plumbing — continue eval on handler applied

**Landed.** Callers distinguish return `1` (handler applied) from `-1` (unhandled):

| Caller | File | On return `1` |
|--------|------|---------------|
| `eval_phase_transfer` | `eval.c` | `KOS_STEP_CONTINUE` |
| `eval_step_drain_transfer_plan` | `eval.c` | `KOS_STEP_REPROLOGUE` |

### 4.6 `try_local` when `dyn_top == frame_top`

**Landed (Option A).** `kos_dyn_try_local_raise_body` calls
**`try_local_extent_on_current_frame`** when `dyn_top == frame_top` and PyErr is
set, using the bytecode extent table before the dyn-chain walk. Required for
before-thunk raises during transfer wind (`TestDynWindTransferFailures`).

### 4.7 What we are **not** doing on failure

| Non-goal | Reason |
|----------|--------|
| Replan after exception | That's continuation-interrupt semantics, not fault handling |
| Suspend transfer intent for later resume | Abandon entirely |
| Raise at invoke site without undo | Incompatible with in-place migration without transactions |
| `kos_build_cc_unwind_plan` for every local failure | Only for cross-context CC apply; too heavy for general case |

Optional **phase 2:** if handler swallows but entered outer winds need after-thunks,
rely on normal `try`/`finally` unwind — do not add unless tests prove resource leaks.

### 4.8 Implementation order (completed)

Steps 1–9 from the original bootstrap plan are done. Regression gate §7.1 and
new tests §7.2 pass.

### 4.9 Motivating scenario (test sketch)

```scheme
(compile/let/cc commit-call
  (compile/let []
    (compile/call do-foo)))

;; do-foo (conceptual):
;;   try:
;;     (do-something ...)   ;; dyn-wind before opens SQL; body captured commit-call
;;   except [Exception as: e]:
;;     (print e)
```

Later, elsewhere: `(commit-call 4 5 6)`. Transfer winds into `do-something`'s
before-thunk; SQL setup raises. **Expected after implementation:**

1. Transfer nest abandoned.
2. `try/except` in `do-foo` catches (on the manifested installed segment).
3. Handler runs `(print e)`; eval continues from handler — **not** a successful
   continuation landing with `4 5 6`.
4. Undelimited invoke site does not expect a return either way.

---

## 5. Nested continuation interrupt (done)

This section documents the **completed** work for agent context. Do not regress.

### 5.1 Sequence

```
outer plan active (EXTENT_ACTION running after-thunk)
  → thunk calls inner continuation
  → kos_invoke_continuation(inner)
       → kos_suspend_transfer_plan(es)   // outer → plan_stack
       → KosEvalState_ArmTransferPlan(es, inner_plan)
  → run_transfer_plan_to_completion
       → drain inner plan
       → DisarmTransferPlan(inner)
       → plan_stack non-empty:
            kos_resume_transfer_plan(es)
            clone original_target → target_keep  // BEFORE disarm
            extract inject, resume_pc, resume_stack, graft_id
            DisarmTransferPlan(outer)
            kos_build_direct_install_plan(es, target_keep, ...)  ; undelimited only
            RESUME_FRAME = NULL on new plan
            ArmTransferPlan(new_plan)
            continue drain loop
```

**Delimited outer plans:** replan must **not** reach `direct_install` (§6.6). Nested
invoke during an active PUSH plan is rejected at invoke time; replan has a
catch-all if a PUSH outer is resumed anyway.

### 5.2 Critical ownership rule (UAF fix)

`original_target` on the plan aliases the invoke clone's `captured_chain`.
`DisarmTransferPlan` frees that chain. **Always clone `original_target` to
`target_keep` before disarm** when replanning.

### 5.3 `dyn_wind` guards (do not break)

- **`kos_dyn_wind_is_on_chain`:** ignore stale CONTROL_RETURNED after INSTALL
  replaced the chain.
- **PRE_BEGIN / PRE_AFTER:** only complete cleanup when active plan's current
  EXTENT_ACTION matches this wind node.
- **Safe stack pop:** only pop if stack non-empty (tail-call continuation invoke
  leaves no return value).
- **Do not** add `if (!plan) return 0` at the start of PRE_AFTER — that breaks
  normal `POP_WIND` after-thunk cleanup when no plan is active.

### 5.4 Tests (all pass)

```scheme
;; Simple unwind — no nested interrupt
(compile/let/cc k
  (compile/dyn-wind nil (compile/call k 42) (compile/call record "after")))

;; Nested from after-thunk — inner 99, outer replans to 42
(compile/let/cc outer-k
  (compile/let/cc inner-k
    (compile/dyn-wind nil (compile/call outer-k 42) (compile/call inner-k 99))))

;; Escape from before-thunk during wind re-entry
(compile/let/cc escape
  (compile/let/cc k
    (compile/dyn-wind
      (compile/begin (compile/call record "before") (compile/call escape 99))
      (compile/begin (compile/call record "body") 42)
      nil)
    (compile/call k 0)))
```

---

## 6. Runtime reference

### 6.1 Data structures

```c
struct KosTransferPlan {
  KosTransferPlan *suspended;
  size_t action_count;
  size_t action_index;
  KosDynNode *original_target;  /* template segment; clone before replan disarm */
  uint32_t graft_point_id;      /* LCA fallback when chains disjoint */
  uint32_t resume_pc;
  KosDynFrame *resume_frame;    /* template only; INSTALL uses fresh frame */
  KosStackBlock *resume_stack;
  PyObject *continuation_ref;
  KosContinuation *invoke_clone;
  KosTransferAction actions[];
};
```

- **`KosEvalState_PLAN(es)`** — active plan.
- **`KosEvalState_PLAN_STACK(es)`** — suspended outers (`plan->suspended` list).

### 6.2 Suspend / resume

```c
void kos_suspend_transfer_plan(KosEvalState *es);
int kos_resume_transfer_plan(KosEvalState *es);
void kos_free_transfer_plan_stack(KosEvalState *es);
```

### 6.3 Multi-shot invoke ownership

On invoke, non-one-shot continuations are cloned; `INVOKE_CLONE` on the plan owns
the template `captured_chain` until `kos_free_transfer_plan`. `original_target`
points into that template — never use after disarm without cloning.

### 6.4 `kos_build_direct_install_plan`

Builds: `INSTALL` + wind `EXTENT_ACTION`s + `INJECT`. Skips unwind. Used for
replan after nested interrupt. Caller sets `RESUME_PC`, `RESUME_STACK`; prefer
`RESUME_FRAME = NULL`.

### 6.6 Delimited nested-interrupt fail-fast

#### 6.6.1 Policy

Until a correct **PUSH replan** is implemented, nested continuation invoke
during an active **delimited PUSH transfer** must **not** proceed. Prefer
**fail-fast** over silent wrong topology.

**Two layers (both required):**

| Layer | When | Purpose |
|-------|------|---------|
| **Primary — invoke time** | Before `kos_suspend_transfer_plan` in `kos_invoke_continuation` | Reject as early as possible; inner transfer never starts |
| **Catch-all — replan time** | After `kos_resume_transfer_plan`, before `kos_build_direct_install_plan` | Defense in depth if a PUSH outer reaches replan |

Both layers set the **same** `PyErr`, then route through **`abandon_xfer_and_try_local`**
(replan) or abandon + return `-1` from invoke (see §6.6.5).

**Explicit non-goal for this milestone:** implementing `kos_build_delimited_push_plan`
on replan. That remains a future replacement for the forbid.

#### 6.6.2 What is forbidden vs allowed

| Scenario | Active / resumed plan shape | Nested invoke | Policy |
|----------|----------------------------|---------------|--------|
| `let/cc` outer, `let/cc` inner | INSTALL (`kos_build_transfer_plan`) | Yes | **Allowed** — existing `TestDynWindContinuations` |
| Delimited PUSH outer, undelimited inner | PUSH | Yes | **Forbidden** at invoke (§6.6.4) |
| Delimited PUSH outer, delimited inner | PUSH | Yes | **Forbidden** at invoke |
| Undelimited outer on `plan_stack`, delimited inner active | INSTALL suspended, PUSH active | Inner only | **Allowed** — replan uses `direct_install` for INSTALL outer |
| PUSH outer resumes for replan | PUSH | N/A | **Catch-all** raises (§6.6.4) — must not call `direct_install` |

**Detection rule:** a plan is **delimited PUSH** if **any** action in the plan
(not only the current `action_index`) has step `KOS_XFER_PUSH`. Do **not** use
`prompt_id` alone — the plan object is authoritative. Undelimited plans use
`KOS_XFER_INSTALL`; `kos_build_return_plan` is INSTALL+INJECT only (no false
positive).

#### 6.6.3 Detection helper

**`kos_xfer_plan_is_delimited_push`** (`dynchain/xfer_plan.c`, `dynchain.h`):
scans `i in [0, action_count)`; returns 1 if any action step is `KOS_XFER_PUSH`,
else 0. NULL plan → 0.

#### 6.6.4 Call sites

**A — Invoke time (`kos_invoke_continuation` in `dynchain/xfer_plan.c`):** before
`kos_suspend_transfer_plan`, when `kos_xfer_plan_is_delimited_push` is true on the
active outer plan (`KosEvalState_PLAN(es)`). Frees the unarmed new plan, cleans up
invoke-side resources, calls `kos_abandon_transfer_nest`, sets `RuntimeError`
(`"nested continuation invoke during an active delimited transfer is not supported"`),
returns `-1`.

**B — Replan (`run_transfer_plan_to_completion` in `eval/eval_chain.c`):** after
`kos_resume_transfer_plan`, before cloning `original_target` or calling
`kos_build_direct_install_plan`. On delimited PUSH outer, releases inject/stack refs,
sets the same `RuntimeError`, returns `abandon_xfer_and_try_local`.

#### 6.6.5 Abandon nest on invoke failure

**`kos_abandon_transfer_nest`** (`dynchain/xfer_plan.c`): `KosEvalState_DisarmTransferPlan`
plus `kos_free_transfer_plan_stack`. §6.6.4 A calls it after freeing the unarmed
new plan.

**`handle_continuation_apply`** (`eval.c`): when `kos_invoke_continuation` returns
`-1` and PyErr is not `KoskariContinuationApply`, calls `kos_abandon_transfer_nest`
when `PLAN` or `plan_stack` is non-empty (idempotent when invoke already abandoned).

**Exception dispatch:** the `RuntimeError` follows the same paths as other
transfer failures (§4): `try_local` on the **manifested** chain if a handler
exists; otherwise `KOS_EV_RAISE`. No cc-unwind carve-out.

**Handler reachability note:** at invoke time the chain is wherever the PUSH plan
had stepped to (often mid-wind before-thunk). A `try/except` wrapped only around
the original `(k val)` call site may not catch the error; unhandled `RuntimeError`
is acceptable and still **explicit**.

#### 6.6.6 Wiring summary

| Component | Location |
|-----------|----------|
| `kos_xfer_plan_is_delimited_push` | `dynchain/xfer_plan.c`, `dynchain.h` |
| `kos_abandon_transfer_nest` | `dynchain/xfer_plan.c`, `dynchain.h` |
| Invoke-time forbid (§6.6.4 A) | `kos_invoke_continuation` in `xfer_plan.c` |
| Replan catch-all (§6.6.4 B) | `run_transfer_plan_to_completion` in `eval/eval_chain.c` |
| Invoke-failure abandon (§6.6.5) | `handle_continuation_apply` in `eval.c` |
| Tests | `TestDelimitedTransferNestedForbid` in `test_continuations.py` |

#### 6.6.7 Future replacement

When implementing real delimited replan, replace the catch-all with builder
selection: if outer plan has `KOS_XFER_PUSH`, call
`kos_build_delimited_push_plan` (or a dedicated replan variant) instead of
`kos_build_direct_install_plan`, and remove the invoke-time forbid. Until then,
§6.6 is the canonical behavior.

---

### 6.7 Async nested await + `plan_stack` (later)

Not started. When a wind/unwind thunk under an active or suspended transfer
returns `KOS_XFER_SUSPENDED_AWAIT`, `plan_stack` must remain coherent across
`KOS_EV_AWAIT` freeze/thaw, including when `plan_stack` depth > 0.

**Verify:** prologue ordering (`eval_phase_pending_exception` before plan drain),
abandon paths after resume, `dyn_wind` on_chain / EXTENT_ACTION guards.

**Tests to add:** async before/after thunk during undelimited nested interrupt
(`let/cc` + `dyn-wind` + `async def` or Koskari await); optional delimited case
after §6.6 forbid is in place.

---

## 7. Test plan

### 7.1 Regression gate (must pass before and after your changes)

```bash
tox -qe py -- tests/compile/specials/test_dyn_wind.py -v
tox -qe py -- tests/compile/specials/test_call_cc.py tests/lib/opcode/test_continuations.py -q
tox -qe py -- tests/reproducers/test_reset_shift_nested.py -q
```

### 7.2 Failed transfer tests (landed)

| Test class / method | Purpose |
|---------------------|---------|
| `TestDynWindTransferFailures::test_try_except_catches_python_error_in_before_thunk_during_transfer` | Python error in before-thunk; `try/except` on installed segment |
| `TestDynWindTransferFailures::test_try_except_catches_koskari_raise_in_before_thunk_during_transfer` | Koskari `compile/raise` under suspended thunk; prologue ordering + extent fix |
| `TestDynWindTransferWindReproducer::test_before_thunk_runs_on_transfer_into_dyn_wind_body` | Wind re-entry during transfer (no failure) |
| `TestDynWindContinuations` (3) | Nested interrupt regression after failed-transfer wiring |
| `test_continuation_passes_through_intermediate_context_with_dyn_with_unwind` | cc-unwind carve-out; dyn-with `__exit__` sees no exception |

All in `tests/compile/specials/test_dyn_wind.py` or `tests/lib/opcode/test_continuations.py`.
Use `tox` only per [testing rules](../../.claude/rules/testing.md).

### 7.3 Trace debugging

```bash
KOS_XFER_TRACE=1 KOS_STACK_TRACE=1 tox -qe gdb -- \
  tests/compile/specials/test_dyn_wind.py::TestDynWindContinuations -s
```

### 7.4 Delimited nested-interrupt forbid tests

Place in `tests/compile/specials/test_continuations.py` (class
`TestDelimitedTransferNestedForbid`) or `tests/compile/specials/test_dyn_wind.py`.
No type annotations; use `tox` only.

**Regression gate after implementation:**

```bash
tox -qe py -- tests/compile/specials/test_continuations.py -v -k DelimitedTransferNestedForbid
tox -qe py -- tests/compile/specials/test_dyn_wind.py::TestDynWindContinuations -v
tox -qe py -- tests/reproducers/test_reset_shift_nested.py -q
```

#### Test 1 — invoke-time forbid (primary, required)

**Name:** `test_nested_invoke_during_delimited_push_transfer_raises`

**Intent:** Delimited continuation invoke starts a PUSH plan; a before-thunk on
the captured segment calls the **same** continuation again while the PUSH plan
is still active → `RuntimeError` at invoke time; transfer nest abandoned.

**Source sketch:**

```scheme
(compile/begin
  (compile/define k
    (compile/reset
      (compile/shift k
        (compile/dyn-wind
          (compile/call k 99)
          42
          nil))))
  (compile/call k 0))
```

**Flow:**

1. `(define k (reset (shift k ...)))` captures delimited `k`.
2. `(k 0)` starts PUSH transfer into the segment (includes `dyn-wind`).
3. Wind `EXTENT_ACTION` runs the before-thunk `(k 99)` while the PUSH plan is
   still armed.
4. §6.6.4 A rejects before suspend; `RuntimeError` propagates.

**Assertions:**

```python
with self.assertRaises(RuntimeError) as ctx:
    await expr()
self.assertIn("delimited transfer", str(ctx.exception))
```

**Must not:** complete with value 42 or 99; must not leave `KOS_CTX_IN_TRANSFER`
set (no leaked plan — verify by clean subsequent eval if needed).

#### Test 2 — undelimited nested interrupt still passes (required)

Re-run existing `TestDynWindContinuations` (all 3). Ensures §6.6.4 A does not
false-positive on INSTALL plans.

#### Test 3 — helper unit test (recommended)

**File:** `tests/native/test_dynchain.c` (if a minimal plan-build fixture exists)
**or** a small Python-side test that builds/drains a trivial delimited invoke and
relies on Test 1 for integration.

**Name:** `test_xfer_plan_is_delimited_push_detects_push_action`

Assert `kos_xfer_plan_is_delimited_push` returns 1 on a plan from
`kos_build_delimited_push_plan` and 0 on one from `kos_build_transfer_plan`.

#### Test 4 — catch-all replan (optional)

Hard to trigger in integration once invoke-time forbid works. Acceptable coverage:
native test that constructs a `KosTransferPlan` with one `KOS_XFER_PUSH` action,
passes it to the helper, and documents that replan block checks the same helper.
Full replan-path test is optional defense-in-depth.

#### Test 5 — try/except on manifested segment (optional)

If the before-thunk raises inside a `compile/try` on the **captured segment**,
assert the handler runs after abandon (same pattern as `TestDynWindTransferFailures`).
Not required for §6.6 minimum if Test 1 asserts unhandled `RuntimeError`.

---

## 8. Acceptance criteria

### 8.1 Nested continuation interrupt — **COMPLETE**

1. All three `TestDynWindContinuations` pass.
2. Full `test_dyn_wind.py` passes.
3. No regressions in call_cc, continuations, reset_shift_nested.
4. §5.2 UAF fix, §5.3 dyn_wind guards, `graft_point_id`, `plan_stack` cleanup.

### 8.2 Failed transfer abandonment — **COMPLETE**

1. `kos_abandon_transfer_nest` and `abandon_xfer_and_try_local` at failure paths (§4.3–4.5).
2. Transfer wind failures reach `try/except` on manifested chain when handler exists.
3. `try_local` handles `dyn_top == frame_top` via extent table (§4.6).
4. cc-unwind sentinel carve-out preserves foreign-apply dyn-with semantics.
5. Nested continuation and full regression gate pass (§7.1).

### 8.3 Delimited nested-interrupt fail-fast — **COMPLETE**

1. `kos_xfer_plan_is_delimited_push` implemented and exported.
2. `kos_abandon_transfer_nest` in `dynchain/xfer_plan.c`, declared in `dynchain.h`.
3. Invoke-time forbid (§6.6.4 A) before `kos_suspend_transfer_plan`.
4. Replan catch-all (§6.6.4 B) before `kos_build_direct_install_plan`.
5. `handle_continuation_apply` abandons nest on invoke failure when plan/stack active.
6. Test 1 and Test 2 from §7.4 pass; §7.1 regression gate passes.
7. `design/TODO.md` follow-ups line updated.
8. PUSH_SHIFT in-place heritage capture; POP_SHIFT PROMPT ancestor walk.

### 8.4 Explicitly out of scope

- Transactional/scratch-space transfer.
- Replan after exception (distinct from nested continuation replan).
- Full delimited PUSH replan (replacement for §6.6 forbid).
- Exception-interrupted transfer that **resumes** the same plan after handler.

---

## 9. Regression checklist

- [x] `TestDynWindContinuations` — 3/3.
- [x] `TestDynWindTransferFailures` — 2/2.
- [x] `TestDynWindTransferWindReproducer` — 1/1.
- [x] `test_call_cc`, `test_continuations`, `test_reset_shift_nested`.
- [x] `test_continuation_passes_through_intermediate_context_with_dyn_with_unwind`.
- [x] §6.6: `TestDelimitedTransferNestedForbid` green (§7.4).
- [x] PUSH_SHIFT heritage capture; `test_dyn_wind_around_shift_captures_in_place`.
- [x] `test_dyn_wind_around_shift_invoke_after_capture`, `test_dyn_wind_around_shift_invoke_inside_shift_body`.

---

## 10. Code touchpoints (quick reference)

| Symbol | File |
|--------|------|
| `run_transfer_plan_to_completion` | `eval/eval_chain.c` |
| `eval_phase_transfer`, `eval_step_drain_transfer_plan` | `eval.c` |
| `handle_continuation_apply` | `eval.c` — abandon on invoke failure (§6.6.5) |
| `process_raise`, `eval_event_raise` | `eval.c` |
| `try_local_exception_handler` | `eval.c` → `kos_dyn_try_local_raise_body` |
| `kos_suspend_transfer_plan`, `kos_resume_transfer_plan` | `dynchain/xfer_plan.c` |
| `kos_free_transfer_plan_stack` | `dynchain/xfer_plan.c` |
| `kos_invoke_continuation` | `dynchain/xfer_plan.c` — §6.6.4 A |
| `kos_xfer_plan_is_delimited_push` | `dynchain/xfer_plan.c` — §6.6.3 |
| `kos_abandon_transfer_nest` | `dynchain/xfer_plan.c` — §6.6.5 |
| `kos_build_xfer_plan`, `KosXferPlanOptions` | `dynchain/xfer_plan.c` (static) |
| `kos_dyn_wind_on_control_returned` | `dynchain/dyn_wind.c` |
| `kos_build_direct_install_plan`, `kos_build_delimited_push_plan` | `dynchain/xfer_plan.c` |
| `abandon_xfer_and_try_local` | `eval/eval_chain.c` — replan catch-all §6.6.4 B |
| `xfer_plan_step_is_cc_raise_reemit`, `xfer_emit_cc_unwind_signal` | `eval/eval_chain.c` |
| `try_local_extent_on_current_frame` | `dynchain/dyn_walk.c` |
| `kos_continuation_capture_segment` | `dynchain.c` (static) |

---

# The end.
