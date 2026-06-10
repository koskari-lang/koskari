# Interrupted and failed transfer plans

**Status:** Nested continuation interrupt **done** (branch `interrupted_transfer`).
Failed/exception transfer abandonment **not started** — next work item.

**Audience:** Agents continuing the feature; maintainers reviewing continuation +
dyn-wind integration.

**Repository root:** `sibilant-ng/`

**Last updated:** 2026-06-10

**Related documents:**

| Document | Relationship |
|----------|----------------|
| [../TODO.md](../TODO.md) | Status rollup; follow-up items |
| [callcc.md](callcc.md) | Re-entrant transfers, plan structure, `original_target` |
| [../EVALUATOR.md](../EVALUATOR.md) | Eval loop, `revive_plan`, transfer draining |
| [../INVARIANTS-AND-CLARIFICATIONS.md](../INVARIANTS-AND-CLARIFICATIONS.md) | `plan` / `plan_stack`, transfer atomicity |
| [../kos_eval_state_api_migration.md](../kos_eval_state_api_migration.md) | `SuspendActivePlan` / `ResumeSuspendedPlan` |
| [../api/dynchain_ng/DYNCHAIN_USAGE_INVENTORY.md](../api/dynchain_ng/DYNCHAIN_USAGE_INVENTORY.md) | Symbol inventory |
| [extent-wind-unwind.md](extent-wind-unwind.md) | Wind/unwind thunk execution model |

---

## How to use this document (agent bootstrap)

| If you need to… | Read |
|-----------------|------|
| See what is already implemented | §2 Implementation status |
| Understand the in-place migration model (why failed transfers raise on the manifested chain) | §3 Architectural decision |
| Implement failed/exception transfer handling (your task) | §4 Failed transfer plans — implementation plan |
| Understand nested continuation interrupt (already done) | §5 Nested continuation interrupt (done) |
| Reference data structures and runtime flow | §6 Runtime reference |
| Run regression tests | §7 Test plan |
| Know when the next milestone is complete | §8 Acceptance criteria |

**Do not** abandon and rewrite the transfer machinery. Nested interrupt is landed;
failed-transfer work layers on top via abandon + normal exception dispatch.

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
dynamic chain to its `original_target` via `kos_build_direct_install_plan`.

### 1.2 Failed transfer (exception during plan) — **NOT DONE**

While a plan is stepping, any step may fail with an exception — notably a
before/after thunk raising during wind/unwind, or a synchronous failure in
`kos_run_transfer_step`. The plan must be **abandoned** (disarmed + `plan_stack`
freed). Exception dispatch must then proceed on the **manifested chain** as
normal eval (via `try_local_exception_handler`), not as a hard `KOS_EV_RAISE`
to an invoke site.

**Explicit non-goal for now:** transactional/scratch-space transfer (build full
target chain off to the side, then swap atomically). Transfer is **in-place
migration** step by step; see §3.

---

## 2. Implementation status

### 2.1 Done (landed on `interrupted_transfer`)

| Item | Location | Notes |
|------|----------|-------|
| Suspend on nested invoke | `kos_invoke_continuation` → `kos_suspend_transfer_plan` | `dynchain.c` |
| Resume + replan loop | `run_transfer_plan_to_completion` | `eval_chain.c` |
| UAF fix: clone `original_target` before disarm | replan block in `eval_chain.c` | `target_keep` via `kos_clone_chain_segment` |
| `graft_point_id` on `KosTransferPlan` | `dynchain.h`, plan builders, invoke | preserved on replan |
| `resume_frame = NULL` on replan | `eval_chain.c` | INSTALL finds frame from segment |
| `kos_free_transfer_plan_stack` | `dynchain.c`, `dynchain.h` | called on fatal transfer errors |
| `dyn_wind` nested-transfer guards | `dyn_wind.c` | `on_chain`, EXTENT_ACTION matching, safe stack pops |
| `PRE_AFTER` unwind guard | `dyn_wind.c` | no double after-thunk during nested unwind |
| `action_index++` on after-thunk return | `dyn_wind.c` | guarded to matching EXTENT_ACTION |
| Tests: `TestDynWindContinuations` (3) | `test_dyn_wind.py` | all pass |
| Full `test_dyn_wind.py` (13) | | all pass |
| Regression: `test_call_cc`, `test_continuations`, `test_reset_shift_nested` | | all pass |
| Design docs partial update | `TODO.md`, `callcc.md`, `DYNCHAIN_USAGE_INVENTORY.md` | nested interrupt marked done |

### 2.2 Not done (next agent work)

| Item | Priority | Notes |
|------|----------|-------|
| **Abandon plan + `try_local` on transfer failure** | **P0** | §4 — core deliverable |
| **`try_local` when `dyn_top == frame_top`** | **P0** | likely required for thunk failures; see §4.4 |
| Abandon plan when `PyErr` during suspended transfer (thunk in opcode loop) | P0 | same helper, different call site |
| `eval_phase_transfer` / `eval_step_drain_transfer_plan` continue on handler applied | P0 | return REPROLOGUE/CONTINUE, not STOP |
| Tests: `try/except` on installed segment during transfer wind | P0 | §7.2 |
| Delimited replan builder selection | P2 | §6.7 — `PUSH` vs `INSTALL` |
| Async nested await + `plan_stack` | P2 | §6.8 |
| Triple-nested `let/cc` tests | P3 | optional coverage |
| Transactional transfer (scratch build) | **deferred** | explicit non-goal |

### 2.3 Files touched (completed work)

| File | Changes |
|------|---------|
| `koskari/lib/eval/eval_chain.c` | replan loop, target clone, graft_id, resume_frame null, plan_stack cleanup |
| `koskari/lib/dynchain.c` | suspend in invoke, `kos_free_transfer_plan_stack`, graft_id in builders |
| `koskari/lib/dynchain.h` | `graft_point_id` field, helper decl |
| `koskari/lib/dynchain/dyn_wind.c` | nested-transfer CONTROL_RETURNED guards |
| `tests/compile/specials/test_dyn_wind.py` | `TestDynWindContinuations` |

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

| Shape | How it happens | Today |
|-------|----------------|-------|
| **Sync call failure** | `CallNoArgs` returns `-1` before thunk bytecode runs | `plan_res < 0` → disarm → `KOS_EV_RAISE` (no `try_local`) |
| **Thunk bytecode raises** | Plan suspended (`KOS_XFER_SUSPENDED_AWAIT`); opcode loop runs thunk; `PyErr` in post_opcode | `eval_event_raise` → `try_local`, but plan still armed; `try_local` gaps |

---

## 4. Failed transfer plans — implementation plan

This section is the **actionable bootstrap** for the next agent.

### 4.1 Goal (one sentence)

On **any** transfer failure while a plan is active or suspended, **abandon the
entire transfer nest**, then **dispatch the exception through normal local raise
handling** on the current manifested chain; if a handler applies, **continue the
eval loop**; if not, emit `KOS_EV_RAISE` as today.

### 4.2 Core helper: `kos_abandon_transfer_nest`

**Add** to `dynchain.c` (or `eval_chain.c` if eval-only), declare in `dynchain.h`
or `eval.h`:

```c
void kos_abandon_transfer_nest(KosEvalState *es);
```

**Behavior:**

1. `KosEvalState_DisarmTransferPlan(es)` — clears `PLAN`, `IN_TRANSFER`
2. `kos_free_transfer_plan_stack(es)` — frees all suspended outers
3. **Do not** clear `PyErr`
4. **Do not** mutate `dyn_top` / `frame_top` / stack (manifested chain stays)

Idempotent if called when no plan active (disarm no-op, stack walk empty).

### 4.3 Call site A — sync step failure in `run_transfer_plan_to_completion`

**File:** `koskari/lib/eval/eval_chain.c`

**Current code** (on `plan_res < 0` and replan errors):

```c
kos_eval_emit_raise_from_pyerr(ctx, out_ev, es);
KosEvalState_DisarmTransferPlan(es);
kos_free_transfer_plan_stack(es);
return -1;
```

**Replace with:**

```c
kos_abandon_transfer_nest(es);
int tl = try_local_exception_handler(es, out_ev);  /* or KosEvalState_Transition KOS_TRANS_TRY_LOCAL_RAISE */
if (tl > 0)
  return 1;   /* new return code: handler applied, continue eval */
if (tl < 0)
  return -1;  /* fatal PyErr */
/* tl == 0: out_ev already KOS_EV_RAISE, PyErr cleared */
return -1;
```

**Return code convention** for `run_transfer_plan_to_completion`:

| Return | Meaning |
|--------|---------|
| `0` | Plan drained successfully (or suspended on await — existing) |
| `1` | Plan abandoned; handler applied; caller must continue eval loop |
| `-1` | Unhandled raise or fatal error; caller stops with `out_ev` |

Apply the same abandon + `try_local` pattern on **replan clone failure** and
**`kos_build_direct_install_plan` failure** paths (after freeing `target_keep`
if allocated), not only `plan_res < 0`.

### 4.4 Call site B — exception during suspended transfer (thunk in opcode loop)

When a wind/unwind thunk runs under `KOS_XFER_SUSPENDED_AWAIT`, the plan remains
armed. If the thunk raises, `eval_event_raise` → `process_raise` →
`try_local_exception_handler` runs **with the plan still active** — inconsistent.

**Add** at the start of `process_raise` or `eval_event_raise` in `eval.c`:

```c
if (KosEvalState_PLAN(es) || KosEvalState_PLAN_STACK(es))
  kos_abandon_transfer_nest(es);
```

Then existing `try_local` runs on a clean post-transfer state.

**Careful:** do **not** call abandon on `KoskariContinuationApply` before
`handle_continuation_apply` — that path **arms** a new plan. Only abandon when
dispatching an ordinary exception, not when installing a continuation transfer.

Suggested guard:

```c
if (KosEvalState_PLAN(es) || KosEvalState_PLAN_STACK(es)) {
  if (! (KoskariContinuationApply && PyErr_ExceptionMatches(KoskariContinuationApply)))
    kos_abandon_transfer_nest(es);
}
```

### 4.5 Caller plumbing — continue eval on handler applied

Two callers invoke `run_transfer_plan_to_completion`:

| Caller | File | Change |
|--------|------|--------|
| `eval_phase_transfer` | `eval.c` (~1937) | if return `1` → `KOS_STEP_CONTINUE` (enter dispatch loop) |
| `eval_step_drain_transfer_plan` | `eval.c` (~2349) | if return `1` → `KOS_STEP_REPROLOGUE` or `KOS_STEP_CONTINUE` |

Today both map any `< 0` to `KOS_STEP_STOP_EVENT`. Teach them to distinguish
`1` (handler applied) from `-1` (unhandled raise).

Also verify `eval_phase_entry` path when `revive_plan` gets `throw_exc` with an
active plan — may need abandon before `KOS_MODE_EXCEPTION` → `try_local`.

### 4.6 `try_local` gap: `dyn_top == frame_top`

**File:** `kos_dyn_try_local_raise_body` in `dynchain.c`

The walk loop only runs while `dyn_top != frame_top`. When a **before-thunk
frame** raises, `dyn_top` is often **the thunk frame** — equal to `frame_top` —
so the walk never runs and handler search fails immediately.

**Minimum fix** (choose one):

**Option A — extent table on current frame first:**

At the top of `kos_dyn_try_local_raise_body`, if `dyn_top == frame_top` and
`PyErr_Occurred()` and `KosEvalState_FRAME(es)`:

```c
KosTryHandler th;
if (kos_try_handler_for((PyObject *) KosDynFrame_CODE(frame),
                        KosEvalState_ACTING_PC(es), &th) == 0
    && th.except_pc != KOS_EXTENT_NONE)
  /* jump to th.except_pc, same as CONSUMED path for TRY */
```

**Option B — walk from parent:**

If `dyn_top == frame_top`, set walk start to `KosDynNode_PARENT(frame)` and
continue the existing loop (still need TRY node on chain above frame).

**Reference:** `tests/lib/test_demand_sync.py` documents the same class of bug
outside transfers (`TestDemandSyncTryExceptAwaitInject`).

Without this fix, wiring abandon → `try_local` may be structurally correct but
**won't catch ancestor `try`** during transfer wind failures (the motivating
`do-foo` / `do-something` / dyn-wind scenario).

### 4.7 What we are **not** doing on failure

| Non-goal | Reason |
|----------|--------|
| Replan after exception | That's continuation-interrupt semantics, not fault handling |
| Suspend transfer intent for later resume | Abandon entirely |
| Raise at invoke site without undo | Incompatible with in-place migration without transactions |
| `kos_build_cc_unwind_plan` for every local failure | Only for cross-context CC apply; too heavy for general case |

Optional **phase 2:** if handler swallows but entered outer winds need after-thunks,
rely on normal `try`/`finally` unwind — do not add unless tests prove resource leaks.

### 4.8 Suggested implementation order

1. Read §3 and §5 (context).
2. `make build-ext`; confirm `TestDynWindContinuations` still passes (regression gate).
3. Implement `kos_abandon_transfer_nest`.
4. Wire call site A in `run_transfer_plan_to_completion`; update return codes.
5. Wire caller plumbing (§4.5).
6. Wire call site B in `process_raise` / `eval_event_raise` with continuation guard.
7. Fix `try_local` `dyn_top == frame_top` (§4.6).
8. Add tests §7.2 (failed transfer + `try/except`).
9. Full regression §7.1.
10. Update `design/TODO.md` when green.

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
            kos_build_direct_install_plan(es, target_keep, ...)
            RESUME_FRAME = NULL on new plan
            ArmTransferPlan(new_plan)
            continue drain loop
```

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

### 6.5 Follow-ups (not started)

**Delimited replan (§6.7):** outer delimited `PUSH` plan may need
`kos_build_delimited_push_plan` on replan, not always `direct_install`.

**Async nested await (§6.8):** outer suspended while inner thunk awaits; not
tested with `plan_stack` depth > 0.

---

## 7. Test plan

### 7.1 Regression gate (must pass before and after your changes)

```bash
make build-ext
tox -qe py -- tests/compile/specials/test_dyn_wind.py -v
tox -qe py -- tests/compile/specials/test_call_cc.py tests/lib/opcode/test_continuations.py -q
tox -qe py -- tests/reproducers/test_reset_shift_nested.py -q
```

### 7.2 New tests for failed transfer (add these)

| Test | Purpose |
|------|---------|
| `try/except` on installed segment catches wind before-thunk error during transfer | Core §4.9 scenario (simplified) |
| Sync Python error in before-thunk during transfer | Call site A path |
| Koskari before-thunk `raise` during transfer | Call site B + `try_local` fix |
| Nested interrupt still passes after failed-transfer wiring | No regression on §5 |

Place in `tests/compile/specials/test_dyn_wind.py` (new class e.g.
`TestDynWindTransferFailures`) or `tests/reproducers/`. No type annotations;
use `tox` only per [testing rules](../../.cursor/rules/testing.mdc).

### 7.3 Trace debugging

```bash
KOS_XFER_TRACE=1 KOS_STACK_TRACE=1 tox -qe gdb -- \
  tests/compile/specials/test_dyn_wind.py::TestDynWindContinuations -s
```

---

## 8. Acceptance criteria

### 8.1 Nested continuation interrupt — **COMPLETE**

1. All three `TestDynWindContinuations` pass.
2. Full `test_dyn_wind.py` passes.
3. No regressions in call_cc, continuations, reset_shift_nested.
4. §5.2 UAF fix, §5.3 dyn_wind guards, `graft_point_id`, `plan_stack` cleanup.

### 8.2 Failed transfer abandonment — **NEXT MILESTONE**

1. `kos_abandon_transfer_nest` implemented and used at all failure paths (§4.3–4.5).
2. Exception during transfer wind reaches `try/except` on manifested chain when
   handler exists (§4.9 scenario).
3. `try_local` handles `dyn_top == frame_top` for thunk-frame faults (§4.6).
4. Nested continuation tests still pass (§7.1).
5. `design/TODO.md` updated to split "nested interrupt" (done) from "failed
   transfer" (done when §8.2 complete).

### 8.3 Explicitly out of scope

- Transactional/scratch-space transfer.
- Replan after exception (distinct from nested continuation replan).
- Delimited replan builder selection (unless bundled intentionally).
- Exception-interrupted transfer that **resumes** the same plan after handler.

---

## 9. Regression checklist

- [ ] `make build-ext` clean.
- [ ] `TestDynWindContinuations` — 3/3.
- [ ] Full `test_dyn_wind.py` — 13/13.
- [ ] `test_call_cc`, `test_continuations`, `test_reset_shift_nested`.
- [ ] New failed-transfer tests (§7.2) green.
- [ ] Grep: no `RESERVED — NOT YET WIRED` on suspend/resume/direct_install.
- [ ] Trace nested interrupt once if touching `dyn_wind.c` or replan block.

---

## 10. Code touchpoints (quick reference)

| Symbol | File |
|--------|------|
| `run_transfer_plan_to_completion` | `eval/eval_chain.c` |
| `eval_phase_transfer`, `eval_step_drain_transfer_plan` | `eval.c` |
| `process_raise`, `eval_event_raise` | `eval.c` |
| `try_local_exception_handler` | `eval.c` → `kos_dyn_try_local_raise_body` |
| `kos_suspend_transfer_plan`, `kos_resume_transfer_plan` | `dynchain.c` |
| `kos_free_transfer_plan_stack` | `dynchain.c` |
| `kos_invoke_continuation` | `dynchain.c` |
| `kos_dyn_wind_on_control_returned` | `dynchain/dyn_wind.c` |
| `kos_build_direct_install_plan` | `dynchain.c` |

**Add:** `kos_abandon_transfer_nest` — suggested home `dynchain.c` alongside
`kos_free_transfer_plan_stack`.

---

# The end.
