# KosEvalState as the primary evaluator API

**Status:** Implemented (archived 2026-06-22). Opcode handlers use
`KosEvalState_*` only (no `KosContext_*` in `opcode/`). Live authority and
choke-point funnel: [eval-state-live-authority.md](../interpreter/eval-state-live-authority.md);
rollup: [EVALUATOR.md](../EVALUATOR.md) §1.

---

## How to use this document

| If you want to… | Read |
|-----------------|------|
| Centralize live `KosEvalState` updates (performance; remove scatter sync) | [../interpreter/eval-state-live-authority.md](../interpreter/eval-state-live-authority.md) |
| Understand the end-state rule (what may touch `KosContext`) | §1, §2 |
| See what lives on `KosContext` vs `KosEvalState` today | §3, Glossary |
| Know which opcodes still use `ctx` directly | §5 |
| Map `KosContext` fields to writers/readers | §9 |
| Implement the migration | §10 (API), §11 (phases), §12 (verification) |
| See how transfer + prologue fit together | §3.3, Appendix B |
| Find source files | §13 |

**Survey baseline:** `koskari/lib` as of 2026-04-27. Re-run the §12 greps after
large refactors.

---

## Table of contents

1. [Executive summary](#1-executive-summary)  
2. [Design decisions](#2-design-decisions-non-negotiable-for-the-migration)  
3. [Current architecture](#3-current-architecture)  
4. [`KosTransitionKind`](#4-kostransitionkind-todays-transition-api)  
5. [Opcode inventory](#5-opcode-inventory)  
6. [`eval.c` behavior buckets](#6-evalc--behavior-buckets)  
7. [`dynchain`](#7-dynchainc-and-dynchainc)  
8. [Other modules](#8-other-koskarilib-modules)  
9. [Appendix A: field reference](#9-appendix-a--koscontext-field-reference-writers-and-readers)  
10. [Target `KosEvalState` API](#10-target-kosevalstate-api--concrete-functions-to-add)  
11. [Phased migration](#11-phased-migration-ordered-with-exit-criteria)  
12. [Verification](#12-verification)  
13. [File reference](#13-file-reference)  
14. [Appendix B: transfer plan and prologue](#appendix-b-transfer-plan-and-prologue-control-flow)  
15. [Glossary](#glossary)  
16. [Revision history](#revision-history)

---

## Glossary

| Term | Meaning |
|------|---------|
| **`KosContext` (`ctx`)** | Persistent struct holding `dyn_top`, cached `frame_top` / `block` / `namespace`, transfer plans, flags, Python bridge, etc. One per running evaluator / coroutine body. |
| **`KosEvalState` (`es`)** | Short-lived C snapshot: borrowed `ctx`, mirrors (`frame`, `code`, `block`, `namespace`), **live** `pc` and `acting_pc`. Created on stack for each `koskari_eval` entry and wherever helpers need the hot view. |
| **`dyn_top`** | Head of the parent-linked dynamic chain (`KosDynNode *`). Opcode **reads** use `KosEvalState_CurrentNode(es)` today. **Writes** go through `dynchain.c` (`kos_dyn_commit_dyn_top`, `kos_dyn_push`, …). |
| **Cold vs hot** | **Cold:** `KosContext` fields updated only through agreed APIs. **Hot:** `KosEvalState` fields (`pc`, `acting_pc`) authoritative until flushed to the heap frame. |
| **Transfer plan** | `KosTransferPlan *`: ordered steps (unwind, install segment, inject value, …). Stored in `ctx->plan` while executing. |
| **`KOS_CTX_IN_TRANSFER`** | Flag: evaluator prologue is driving a transfer plan. Set when a plan is **armed**; cleared when the driver finishes or aborts (see Appendix B). |
| **`KOS_OP_PROLOGUE()`** | Opcode return tag: “run eval prologue before next instruction” — used to **start or continue** transfer plan execution. |
| **`kos_dyn_transition`** | Central dispatcher for structured walks (return to frame, unwind to node, try-local raise, …). Becomes **`KosEvalState_Transition`** at the public boundary. |

---

## 1. Executive summary

### 1.1 Goal

**During bytecode execution, `KosEvalState_*` is the only supported API** for
driving the evaluator: operand stack, frame/code/PC mirrors, dynamic chain
operations (via existing `kos_dyn_*` that take `es`), **arming** transfer
plans, flag and inject fields, and the Python await/yield bridge.

**`KosContext` remains the backing struct** (token, domain, plan pointers,
chain version, bridge object fields). **Application code and opcode handlers do
not use `KosContext_*` macros** except inside the implementation of
`KosEvalState_*` and in **context lifecycle** (`KosContext_New`,
`KosContext_Free`, GC traverse) and **constructors** that run before any
`KosEvalState` exists (`KosPyAsyncGen_New`, `KosCoroutine_New`, …).

### 1.2 Compile-to-native

Generated native glue calls **`KosEvalState_*` with `KosEvalState *es` as the
first parameter** and branches on the same outcome family opcodes use today.
Generated code **does not** reference `KosContext_PLAN`, `KosContext_FLAGS`, or
inject fields by name.

### 1.3 Out of scope

| Topic | Reason |
|-------|--------|
| New `KosTransferPlan` IR | This migration moves **call sites** only; plan **semantics** stay. |
| Language semantics | Refactor-only; same observable behavior. |
| **Incremental** pop/push retarget at every `dyn_top` publish | **Phase 1 done** for Tier 1 (`kos_dyn_retarget_*` in `dynchain.c`); see [eval-state-live-authority.md](../interpreter/eval-state-live-authority.md). Tier 2 xfer / tail koscall still use full `kos_dyn_rebuild_live_from_chain`. |

---

## 2. Design decisions (non-negotiable for the migration)

| ID | Decision |
|----|----------|
| **D1** | `KosEvalState` holds a **borrowed** `ctx`; the full context is **not** inlined into `KosEvalState`. |
| **D2** | Plan execution stays **driven by the eval loop**: arm plan → return **`KOS_OP_PROLOGUE()`** → prologue runs steps; suspend/resume unchanged. No new requirement to run a full plan synchronously in one C call. |
| **D3** | **`KosEvalState_Transition`** is the **public** name for `kos_dyn_transition`. Implementation stays in `dynchain.c` until renamed; **on `-1`**, call **`KosEvalState_SyncFromContext(es)`** before returning (same class of repair as `kos_dyn_uninstall` error paths). After all call sites switch, **`kos_dyn_transition` may become `static`** in `dynchain.c` and drop from `dynchain.h`. |
| **D4** | **No new** `KosContext_DYN_TOP(ctx) = …` outside **`dynchain.c`** and **`eval.c`** (tail-koscall / agen paths). Existing stragglers are listed in Phase **F** for folding into **`kos_dyn_commit_dyn_top`** (or a single `eval.c` helper). |
| **D5** | **Pure** chain-editing helpers (clone segment, splice under a given node) take **explicit** `KosDynNode *` arguments — they do **not** read `KosContext_DYN_TOP` internally. **Publishing** the new live head remains **`kos_dyn_commit_dyn_top(es, new_top)`** (or push/install). |
| **D6** | Module globals: **`KosEvalState_GlobalsNamespace(es)`** walks **`KosNamespace_PARENT`** from the current frame's namespace (no **`KosContext`** cache). |

---

## 3. Current architecture

### 3.1 `KosContext` (see `eval.h` ~338–416)

| Region | Fields |
|--------|--------|
| Chain | `dyn_top` |
| Derived from walking chain | `frame_top`, `block`, `namespace` |
| Module globals | (none on context; parent walk at opcode time) |
| Identity | `token`, `domain` |
| Unwind / return staging | `unwind_target_frame`, `pending_return_value`, `pending_return_to_parent` |
| Transfer | `plan`, `plan_stack` |
| Python bridge | `py_resume_inject`, `py_resume_kind`, `py_await_iter` |
| Exception inject | `inject_mode`, `inject_type`, `inject_value` |
| Revive hint | `suspension_event` |
| Flags | `flags` (`RUNNING`, `FROZEN`, `DEAD`, `IN_TRANSFER`) |
| Allocator | `context_freelist_next` |

**Intent (from `eval.h`):** while `koskari_eval` runs, treat **`KosContext` as
cold storage** updated through functions that take **`KosEvalState *`**;
**`KosEvalState`** carries the **active** execution slice.

### 3.2 `KosEvalState`

| Field | Role |
|-------|------|
| `ctx` | Borrowed; must match the evaluator in use |
| `frame`, `code`, `block`, `namespace` | Mirrors of the context-derived view |
| `pc`, `acting_pc` | **Authoritative** until `FlushLiveRegsToFrame` |

**Sync contract (order matters for correctness):**

1. **`KosEvalState_Init(es, ctx)`** — initialize from context (entry).
2. **`KosEvalState_FlushLiveRegsToFrame(es)`** — before **any** change to
   `dyn_top` or which frame is “current.”
3. **`KosEvalState_RefreshDynCaches(es)`** — recompute **`frame_top` / `block` /
   `namespace` on `ctx`** from **`dyn_top`**, then **`ApplyDynCaches`** into
   `es`.
4. **`KosEvalState_SyncFromContext(es)`** — broader resync; preserves live PCs
   when frame+code still match (see inline in `eval.h`).

### 3.3 Transfer plans and the eval loop

**Invariant (preserve exactly):**

1. **Arm:** `ctx->plan` points to a heap `KosTransferPlan`, and
   **`KOS_CTX_IN_TRANSFER`** is set. Any previous plan is freed first (same as
   `POP_SHIFT.c` / `eval.c` delim paths).
2. **Enter driver:** Handler returns **`KOS_OP_PROLOGUE()`** so the evaluator
   **prologue** runs **`run_transfer_plan_to_completion`** (which may suspend;
   nested plans use **`plan_stack`**).
3. **No synchronous completion requirement:** a single C function is **not**
   required to run all plan steps; suspension and resume keep working as today.

**Mapping to new API:**

- **`KosEvalState_ArmTransferPlan(es, plan)`** performs the arm sequence:
  `kos_free_transfer_plan` on old `plan` if non-NULL; assign **`plan`**; set
  **`KOS_CTX_IN_TRANSFER`**. Argument **`plan`** must be non-NULL.
- **`KosEvalState_HasActiveTransferPlan(es)`** replaces the opcode idiom
  **`KosContext_PLAN(ctx) != NULL`** for branching:
  **`KOS_OP_PROLOGUE()`** vs **`KOS_OP_RUN()`** (see `RETURN.c`).

**Arming pattern:** Opcode paths that today do **`PLAN` assign + `|=` `IN_TRANSFER`**
in one block become **`KosEvalState_ArmTransferPlan`**. The same “replace active
plan” sequence appears in **`eval.c`** (`kos_return_delimited`, foreign-token
path in continuation apply) and **`dynchain.c`** (continuation invoke arm, ~4015
ff.): those should call **`ArmTransferPlan`** instead of open-coding.

**Clearing `IN_TRANSFER`:** Today the flag is cleared **together** with freeing
the plan in **`clear_and_free_transfer_plan`** (`eval.c`). There is **no**
standalone “clear `IN_TRANSFER` only” call site. **`DisarmTransferPlan`** is the
public name for that teardown (see §10.1).

---

## 4. `KosTransitionKind` (today’s transition API)

Source: **`dynchain.h`**. Comment there: **continuation install/resume** is
**not** a `KosTransitionKind`; **`eval.c`** still orchestrates that separately.

| Kind | Role |
|------|------|
| `KOS_TRANS_WALK_RETURN_TO_FRAME` | Deliver return toward a target frame |
| `KOS_TRANS_UNWIND_TO_FRAME` | Unwind chain to a frame |
| `KOS_TRANS_UNWIND_TO_NODE` | Unwind to a specific node |
| `KOS_TRANS_WALK_HANDLE_YIELD` | Yield boundary walk |
| `KOS_TRANS_TRANSFER_TO_LOOP` | Loop transfer |
| `KOS_TRANS_TRY_LOCAL_RAISE` | TRY/FINALLY local raise |

**Call sites today:** `kos_dyn_transition(es, …)` from `POP_PROMPT.c`,
`RETURN.c`, `BREAK.c`, `CONTINUE.c`, `eval.c`, `utils.c`.

**Public wrapper:** **`KosEvalState_Transition`** (§10.7) — thin call +
**`SyncFromContext` on `-1`**.

---

## 5. Opcode inventory

### 5.1 Scale

- **`koskari/lib/opcode/*.c`:** **207** files.
- **Files containing `KosEvalState_CONTEXT(es)`:** **18** (table below).

### 5.2 All 18 files: what they touch on `KosContext`

| File | `KosContext` usage |
|------|---------------------|
| `AWAIT.c` | **`FLAGS`:** clears **`KOS_CTX_RUNNING`** on **arg == 0** bare suspend; arg == 1 uses **`kos_await_dispatch`** (no direct `ctx` in that branch). |
| `RETURN.c` | **`PLAN`** (prologue vs run), **`PENDING_RETURN_*`**, **`FLAGS`** RUNNING/DEAD; **`kos_dyn_transition`**. |
| `YIELD.c` | **`FLAGS`:** clear RUNNING, set DEAD. |
| `YIELD_FROM.c` | **`FLAGS`** RUNNING/DEAD in yield emit paths; **`INJECT_*`** read/clear. |
| `POP_PROMPT.c` | **`PENDING_RETURN_*`**, **`FLAGS`** (clear RUNNING on suspend branch); **`kos_dyn_transition`**. |
| `POP_SHIFT.c` | **`PLAN`** replace, **`KOS_CTX_IN_TRANSFER`**. |
| `PUSH_SHIFT.c` | Same as `POP_SHIFT.c`. |
| `POP_WIND.c` | **`unwind_target_frame`** when **`kos_dyn_pop_top`** returns **`KOS_DYN_UNWIND_ASYNC_STARTED`**. |
| `NEXT.c` | Passes **`ctx`** into **`op_get_next_from_iter`**. |
| `ITER_NEXT.c` | Same. |
| `GLOBALS.c` | **`KosContext_GLOBALS_FRAME`** → **`KosDynFrame_NS`**. |
| `IMPORT_REL.c` | Same. |
| `NOOP.c` | **`(void) ctx`** only. |
| `LABEL.c` | **`(void) ctx`** only. |
| `POSITION.c` | **`(void) ctx`** only. |
| `THEEND.c` | **`(void) ctx`** only. |
| `EXCEPTION_CLEAR.c` | **`(void) ctx`** only. |

**`kos_dyn_transition` without a local `ctx` variable:** `BREAK.c`,
`CONTINUE.c` — context is still touched inside **`dynchain.c`**.

### 5.3 Phase A (hygiene)

Delete the unused **`ctx`** binding in:
`NOOP.c`, `LABEL.c`, `POSITION.c`, `THEEND.c`, `EXCEPTION_CLEAR.c`.
**Zero behavior change.**

---

## 6. `eval.c` — behavior buckets

| Bucket | What uses `KosContext` today | Becomes |
|--------|------------------------------|---------|
| Cache | **`RefreshDynCaches`** / **`ApplyDynCaches`** | Stays implemented in **`eval.c`**; already public where needed. Not duplicated in opcodes. |
| Stack owner walk | **`InnermostStackOwner`** | Stays internal; opcodes use **`KosEvalState_InnermostStackOwner`**. |
| Plan arm / disarm | **`PLAN`**, **`PLAN_STACK`**, **`IN_TRANSFER`**, version checks | **`KosEvalState_ArmTransferPlan`**, **`DisarmTransferPlan`**, **`HasActiveTransferPlan`**, **`SuspendActivePlan`**, **`ResumeSuspendedPlan`** (§10.1). |
| Delim return | **`kos_return_delimited`** (`eval.c`) — build plan, arm, delim cleanup, **`run_transfer_plan_to_completion`** | **`KosEvalState_KReturnDelimited`** — same signature as current **`kos_return_delimited`** (§10.12). |
| Python await bridge | **`py_await_iter`**, **`py_resume_*`** | §10.9 accessors. |
| Suspension event | **`suspension_event`** | §10.8. |
| Lifecycle | **`KosContext_New`**, **free**, traverse | **Stay `KosContext`-centric** — not opcodes. |
| Tail koscall | **`kos_tail_koscall(ctx, code, ns)`** (`eval.c`) | **`KosEvalState_TailKoscallReplaceFrame(es, code, ns)`** — §10.13 (**`ctx` → `es`**). |
| Opcode-visible flags | YIELD/AWAIT/RETURN death and suspend bits | §10.2 helpers — **bit patterns copied verbatim** from the opcode bodies listed there. |
| Eval-only plan teardown | Prologue / driver clears active plan | **`KosEvalState_DisarmTransferPlan`** delegates to **`clear_and_free_transfer_plan`** in **`eval.c`** (frees plan, clears **`IN_TRANSFER`**, continuation-ref handoff). No separate public “clear flag only” helper exists today. |

---

## 7. `dynchain.c` and `dynchain/*.c`

### 7.1 Translation unit

**`koskari/lib/dynchain.c`** is the shell. It `#include`s per-kind
**`dynchain/dyn_*.c`**, **`dynchain/dyn_walk.c`** (walks, pop/unwind,
**`kos_dyn_transition`**), and **`dynchain/xfer_plan.c`** (plan build via
**`KosXferPlanOptions`** / **`kos_build_xfer_plan`**, **`kos_run_transfer_step`**,
suspend/resume, **`kos_invoke_continuation`**). One **`dynchain.o`**; public API
in **`dynchain.h`**.

### 7.2 Surface

**Public `kos_dyn_*` functions take `KosEvalState *es` first.** They reach context
via **`KosEvalState_CONTEXT(es)`** and mutate **`KosContext`** fields.

### 7.3 Where `KosContext` is mutated heavily

| Area | Key symbols | File |
|------|-------------|------|
| Commit head | **`kos_dyn_commit_dyn_top`**, **`kos_dyn_push`** | `dynchain.c` |
| Pop / uninstall | **`kos_dyn_uninstall`**, pop family | `dyn_walk.c`, `dyn_*.c` |
| Transitions | **`kos_dyn_transition`** | `dyn_walk.c` |
| Xfer steps | **`xfer_step_install`**, **`xfer_step_push`**, **`xfer_step_inject`** | `xfer_plan.c` (static; drained via **`kos_run_transfer_step`**) |
| Generator | **`kos_dyn_gen_freeze`**, **`kos_dyn_gen_install`** | `dyn_gen.c` |
| CC unwind setup | **`kos_build_cc_unwind_plan`** | `xfer_plan.c` |
| Plan nesting | **`plan_stack`** suspend/resume | `xfer_plan.c` |
| Transfer drain / replan | **`run_transfer_plan_to_completion`** | `eval/eval_chain.c` |

### 7.4 Public API still taking `KosContext *`

| Symbol | Callers | Migration |
|--------|---------|-----------|
| **`kos_dyn_gen_frozen_needs_install(KosContext *eval_ctx, KosDynGen *g)`** | **`iter.c`**, **`demand.c`** | Add **`kos_dyn_gen_frozen_needs_install_es(KosEvalState *es, KosDynGen *g)`** that delegates to context via **`es`**. Switch call sites that have **`es`**. Remove **`KosContext *`** overload from **public** headers once unused. |

---

## 8. Other `koskari/lib` modules

| Module | Today | Target |
|--------|-------|--------|
| **`func.c`** | Sets frame **`code`/`ns`**, **`KosContext_NAMESPACE`**, **`SyncFromContext`** | **`KosEvalState_SetFrameCodeAndNamespace`** (§10.6) only. |
| **`utils.c`** | **`PENDING_RETURN_*`** after async **`kos_dyn_transition`** | **`KosEvalState_SetPendingReturn(es, result, 1)`** (§10.3). |
| **`cont.c`** | **`KosContext_TOKEN`** vs continuation | **`KosEvalState_Token(es)`** (§10.10). |
| **`coro.c`** | Python bridge on **`coro->ctx`** | Inside **`koskari_eval`:** build **`es`**, use §10.9. Outside bytecode: **temporary** direct **`ctx`** until coroutine driver is unified (Phase **G**). |
| **`domain.c`** | **`kos_domain_push_scope(ctx)`** | Phase **G:** **`KosEvalState_EnterDomainScope` / `Leave`** forwarding to **`ctx`**. Until then: **only** eval bootstrap — **not** opcodes. |
| **`iter.c`**, **`demand.c`** | **`KosContext_DYN_TOP(agen->ctx)`**, frozen install check | Prefer **`agen`** state flags; frozen check uses **`*_es`** overload (Phase **D**). |

---

## 9. Appendix A — `KosContext` field reference (writers and readers)

**Note:** **`eval.c`** and **`dynchain.c`** are the **systematic** writers for most
fields; this table highlights **representative** sites and **opcode** touch
points.

| Field | Writers (representative) | Readers (representative) |
|-------|--------------------------|---------------------------|
| `dyn_top` | **`kos_dyn_commit_dyn_top`**, **`kos_dyn_push`**, xfer INSTALL/PUSH, **`kos_tail_koscall`**, gen install/freeze, CC plan, agen flatten | Walks; **`KosEvalState_CurrentNode`** |
| `frame_top`, `block`, `namespace` | **`KosEvalState_RefreshDynCaches`** | **`ApplyDynCaches`**, **`SyncFromContext`** |
| (module globals) | **`KosEvalState_GlobalsNamespace`** in **`eval_chain.c`** (parent walk) | **`GLOBALS.c`**, **`IMPORT_REL.c`** |
| `plan`, `plan_stack` | **`eval.c`**, shift opcodes, xfer arm, suspend/resume | Prologue driver |
| `flags` | Opcodes (YIELD, AWAIT, RETURN, SHIFT, PROMPT), **`eval.c`**, **`dynchain.c`** | Prologue / revive |
| `pending_return_*` | **`RETURN`**, **`POP_PROMPT`**, **`utils.c`** | Parent return delivery |
| `unwind_target_frame` | **`POP_WIND`**, **`dynchain.c`** unwind | Resume after async wind |
| `inject_*` | **`YIELD_FROM`**; xfer inject/truncate | **`YIELD_FROM`**, throw |
| `py_*` | **`eval.c`**, **`coro.c`** | Resume / AWAIT |
| `suspension_event` | **`eval.c`** | Prologue branches |
| `token`, `domain` | **`KosContext_New`**, domain push/pop | **`cont.c`**, **`domain.c`** |

---

## 10. Target `KosEvalState` API — concrete functions to add

Implementations live in **`eval.c`**. They
**delegate** to existing code; **no semantic change**.

**Convention (every subsection below):**

- **Current:** where the behavior lives today (file + symbol + one-line role).
- **New:** proposed entry point (signature).
- **API / refactor:** argument changes, what to extract as **pure** (no `ctx`
  writes) vs **effectful** (delegates to existing `ctx` mutation).

**Already present (no new wrapper required for naming migration):**

- **`KosEvalState_InnermostStackOwner`** — **`eval.c`**, walks **`KosContext_DYN_TOP`**
  for innermost stack owner; used from **`eval.c`** and **`dynchain.c`**.

---

### 10.1 Transfer and plans

**Proposed declarations:**

```c
void KosEvalState_ArmTransferPlan(KosEvalState *es, KosTransferPlan *plan);
void KosEvalState_DisarmTransferPlan(KosEvalState *es);
int KosEvalState_HasActiveTransferPlan(const KosEvalState *es);
int KosEvalState_SuspendActivePlan(KosEvalState *es);
int KosEvalState_ResumeSuspendedPlan(KosEvalState *es);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`ArmTransferPlan`** | Same open-coded sequence in **`opcode/POP_SHIFT.c`** (~110–113), **`opcode/PUSH_SHIFT.c`**, tail of **`kos_return_delimited`** (`eval.c`, ~682–685), foreign-token path (`eval.c`, ~1266–1269), **`dynchain.c`** continuation arm (~4015–4018): if old **`KosContext_PLAN`**, **`kos_free_transfer_plan`**; assign new plan; **`KOS_CTX_IN_TRANSFER \|=`**. | **`void (KosEvalState *es, KosTransferPlan *plan)`** — **`plan` non-NULL**. Single implementation; opcodes drop **`ctx`** for this block. |
| **`DisarmTransferPlan`** | **`clear_and_free_transfer_plan`** (`eval.c`, ~600–611): null **`ctx->plan`** first, **`KOS_CTX_IN_TRANSFER &= ~`**, optional **`continuation_ref` → `PENDING_SHIFT_WRAPPER`** handoff for prompt plans, then **`kos_free_transfer_plan`**. | **`void (KosEvalState *es)`** — thin rename / non-**`static`** export of that body (or inline wrapper calling it). |
| **`HasActiveTransferPlan`** | **`KosContext_PLAN(ctx)` tests, e.g. **`opcode/RETURN.c`** ~69, **`eval.c`** **`kos_op_outcome_from_call_int`** ~551. | **`const KosEvalState *`** — equivalent to **`KosContext_PLAN(ctx) != NULL`**. |
| **`SuspendActivePlan`** / **`ResumeSuspendedPlan`** | **`kos_suspend_transfer_plan`** / **`kos_resume_transfer_plan`** (`dynchain.c`, ~3847–3890; comments say **reserved, not yet wired**). **`Resume`** returns **`-1`** on chain-version mismatch after **`KosEvalState_SyncFromContext`**. | **Same signatures but `KosEvalState_*` names** in **`eval.h`** (implementations can **`static inline`** to **`kos_*`** or move bodies into **`eval.c`**). No argument change. |

---

### 10.2 Flags and death (opcode-visible)

**Proposed declarations:**

```c
void KosEvalState_MarkSuspendedNotRunning(KosEvalState *es);
void KosEvalState_MarkDeadAfterYield(KosEvalState *es);
void KosEvalState_MarkDeadAfterReturn(KosEvalState *es);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`MarkSuspendedNotRunning`** | **`opcode/AWAIT.c`**, **`arg == 0`**: **`KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING`** (~29), then event setup. | **`void (KosEvalState *es)`** — copy **only** the flag line; opcode keeps **`out_ev`** / **`KOS_OP_POST_SUSPEND`** locally. |
| **`MarkDeadAfterYield`** | **`opcode/YIELD.c`**: **`&= ~RUNNING`**, **`|= DEAD`** (~81–82). | Same. |
| **`MarkDeadAfterReturn`** | **`opcode/RETURN.c`**, top-level return path: **`&= ~RUNNING`**, **`|= DEAD`** (~151–152). | Same. |

**Rule:** Bit masks are **copied verbatim** from those opcode lines — **no**
reinterpretation. **`IN_TRANSFER`** is **not** touched here (see **10.1**
**`DisarmTransferPlan`**).

---

### 10.3 Pending return and unwind target

**Proposed declarations:**

```c
void KosEvalState_SetPendingReturn(KosEvalState *es, PyObject *val, int to_parent);
void KosEvalState_SetUnwindTargetFrame(KosEvalState *es, KosDynFrame *frame);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`SetPendingReturn`** | **`KosContext_PENDING_RETURN_VALUE`** + **`PENDING_RETURN_TO_PARENT`**: **`RETURN.c`** ~93–94 (**`to_parent == 1`**) and ~134–135 (**`0`**); **`opcode/POP_PROMPT.c`** ~69–70 (**`0`**); **`utils.c`** **`apply_native_handler`** ~152–153 (**`1`**). Caller **retains** refcount expectations today (**no extra `INCREF` in those assignments** — wrapper must mirror that exactly). | **`void`**, **`to_parent`** is **`0` or `1`** only. |
| **`SetUnwindTargetFrame`** | **`opcode/POP_WIND.c`** ~39–40 when **`kos_dyn_pop_top`** returns **`KOS_DYN_UNWIND_ASYNC_STARTED`**; also written in **`dynchain.c`** / **`dyn_wind.c`** on unwind paths. | **`void (KosEvalState *es, KosDynFrame *frame)`** — opcode code drops **`ctx`** local if only used here. |

---

### 10.4 Exception inject (yield-from)

**Proposed declaration:**

```c
int KosEvalState_TakeInjectThrow(KosEvalState *es, PyObject **type_out,
                                 PyObject **value_out);
/* Returns 1 if KOS_INJECT_THROW was pending (fields filled, slots cleared). */
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`TakeInjectThrow`** | **`opcode/YIELD_FROM.c`** ~671–676: if **`KosContext_INJECT_MODE == KOS_INJECT_THROW`**, read **`INJECT_VALUE` / `INJECT_TYPE`**, set mode **`NONE`**, clear pointers. | **Optional pure** inner: “if inject throw, output type/value and clear” with **`KosContext *`** or field pointers; **effectful** wrapper reads **`KosEvalState_CONTEXT(es)`**. Return **1** if taken, **0** if not **`THROW`**. |

---

### 10.5 Globals namespace

**Proposed declaration:**

```c
PyObject *KosEvalState_GlobalsNamespace(KosEvalState *es);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`GlobalsNamespace`** | **`eval_chain.c`**: walk **`KosNamespace_PARENT`** from **`KosDynFrame_NS(FRAME(es))`**. | Return **borrowed** namespace; callers **`INCREF`** as in **`GLOBALS.c`**. |

---

### 10.6 Frame retarget (expression / function install)

**Proposed declaration:**

```c
int KosEvalState_SetFrameCodeAndNamespace(KosEvalState *es,
    KosDynFrame *frame, KosCode *code, PyObject *ns);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`SetFrameCodeAndNamespace`** | **`func.c`**: **`kos_install_expression`** (~190–200), **`kos_install_function`** (~234–241), **`kos_vinstall_function`** (~273–280): **`Py_INCREF(ns)`** or prepared ns, **`Py_XDECREF(KosDynFrame_NS(frame))`**, assign **`frame` ns + code**, **`KosContext_NAMESPACE(ctx) =`** same **`ns`**, **`KosEvalState_SyncFromContext(es)`**, return **0**. | **`int`** return and error rules **unchanged** (propagate **-1** from callees). Consolidates three duplicate blocks into one helper **called from** those **`install`** functions. |

---

### 10.7 Transition facade

**Proposed declaration:**

```c
int KosEvalState_Transition(KosEvalState *es, const KosTransitionReq *req,
                            KosTransitionResult *res, KosEvent *out_ev);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`Transition`** | **`kos_dyn_transition`** (`dynchain.c`, ~1737 ff.): **`int`** return **`-1`** on invalid args / internal errors (**`KosEvalState_SyncFromContext(es)`** already called on those paths); **`0`** on success with **`out->status` / `out->raw`** filled. Walk helpers may set **`KOS_TRANS_STATUS_ERROR`**. | **Identical parameters** — one-line forward in **`eval.c`** or **`eval.h` inline. Callers (**`RETURN.c`**, **`POP_PROMPT.c`**, **`BREAK.c`**, **`CONTINUE.c`**, **`eval.c`**, **`utils.c`**) swap the name only in Phase **B1** / **C**. |

---

### 10.8 Suspension event

**Proposed declarations:**

```c
void KosEvalState_SetSuspensionEvent(KosEvalState *es, uint8_t ev);
void KosEvalState_ClearSuspensionEvent(KosEvalState *es);
uint8_t KosEvalState_SuspensionEvent(const KosEvalState *es);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **Get / set / clear** | **`KosContext_SUSPENSION_EVENT`** read/write in **`eval.c`** prologue / revive paths (~2873–2913, ~3022, ~3092, ~3171, ~3204). Cleared to **`KOS_SUSP_NONE`** after handling (~2913). | Thin accessors on **`KosEvalState_CONTEXT(es)`**. **No** semantic change. |

---

### 10.9 Python bridge

**Proposed declarations:**

```c
void KosEvalState_PythonAwaitSetIter(KosEvalState *es, PyObject *iter);
void KosEvalState_PythonAwaitClearIter(KosEvalState *es);
PyObject *KosEvalState_PythonAwaitIter(KosEvalState *es);
void KosEvalState_PythonResumeSet(KosEvalState *es, PyObject *val, uint8_t kind);
int KosEvalState_PythonResumeTake(KosEvalState *es, PyObject **out_val, uint8_t *out_kind);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **Await iter** | **`KosContext_PY_AWAIT_ITER`**: set/clear in **`eager_step_awaitable`** / **`revive_from_await`** paths (`eval.c`, ~1800–1802, ~1571, ~1613, throws ~834–907). | **`Set`** must mirror **`Py_XDECREF` + assign**; **`Clear`** mirrors **`Py_CLEAR`**; **`Iter`** returns **borrowed** pointer. **Ref rules:** match existing **`Py_XDECREF`/`Py_CLEAR`** sites exactly. |
| **Resume inject** | **`py_resume_inject`**, **`py_resume_kind`** (`eval.h` ~366–367, macros ~401–402): written when staging resume value; consumed on re-entry. | **`Set` / `Take`**: **`Take`** should **`XDECREF`** cleared inject if today’s consumer does — **mirror** the **`eval.c`** consume path you replace. |

---

### 10.10 Evaluator token

**Proposed declaration:**

```c
KosEvaluatorToken *KosEvalState_Token(KosEvalState *es);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`Token`** | **`KosContext_TOKEN(ctx)`** compared to **`KosContinuation_TOKEN_REF`** in **`cont.c`** (~150–151). | **`KosEvalState_CONTEXT(es)->token`** (or macro). **No** new semantics. |

---

### 10.11 Iterator helper

**Proposed declaration:**

```c
KosGetNextResult op_get_next_from_iter(KosEvalState *es, KosEvent *out_ev,
    PyObject *iter, PyObject **result_out);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`op_get_next_from_iter`** | **`static`** in **`eval.c`** (~2637): first parameter **`KosContext *ctx`**; calls **`next_from_python_async_iter(ctx, …)`** (~2574) which uses **`eager_step_awaitable(..., ctx, …)`** (stores **`PY_AWAIT_ITER`**, clears **`RUNNING`** on suspend). **`opcode/NEXT.c`** / **`ITER_NEXT.c`** pass **`ctx`**. | **Change:** first parameter **`KosEvalState *es`**; derive **`ctx = KosEvalState_CONTEXT(es)`** once. **Pure** sub-helpers may keep **`KosContext *`** internally. Drop **`ctx`** locals from opcodes. |

---

### 10.12 Delimited `k(value)` return (eval internal today)

**Proposed declaration (export from `eval.c`, still eval-driven):**

```c
int KosEvalState_KReturnDelimited(KosEvalState *es,
    KosDynDelim *delim, PyObject *val, KosEvent *out_ev);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`KReturnDelimited`** | **`static int kos_return_delimited`** (`eval.c`, ~660–699): **`kos_build_return_plan`**, **`Py_DECREF(val)`** after build, sets **`RESUME_*` fields**, **`Arm`**-equivalent (free old plan, assign, **`IN_TRANSFER`**), clears delim fields / may **`kos_dyn_pop_delim`**, then **`run_transfer_plan_to_completion(es, out_ev)`** (may run **multiple** steps **before** opcode returns). | **Phase 1:** non-**`static`** name + **`KosEvalState_*`** prefix; **same parameters and return**. **Optional later:** split **“build + arm”** vs **“run to completion”** only if a second caller needs arm without synchronous drain — **not required** for opcode migration. |

---

### 10.13 Tail koscall (direct `dyn_top` mutation today)

**Proposed declaration:**

```c
int KosEvalState_TailKoscallReplaceFrame(KosEvalState *es,
    KosCode *code, PyObject *ns);
```

| Wrapper | Current implementation | New API / refactor |
|--------|-------------------------|-------------------|
| **`TailKoscallReplaceFrame`** | **`kos_tail_koscall(KosContext *ctx, KosCode *code, PyObject *ns)`** (`eval.c`, ~1693–1726): builds **`KosEvalState`** on stack via **`Init`**, **`FlushLiveRegsToFrame`**, assigns **`KosContext_DYN_TOP(ctx)`** twice around **`kos_dynframe_new`**, bumps **`chain_version`**, **`RefreshDynCaches`**. | **API change:** **`KosContext *` → `KosEvalState *es`** so callers already in **`es`** do not fabricate a context-only entry. Body uses **`KosEvalState_CONTEXT(es)`** instead of **`ctx`**. |

---

### 10.14 Post–Phase B: opcode-facing plan builders

**Current pattern:** e.g. **`opcode/POP_SHIFT.c`**: **`kos_dyn_build_pop_shift_plan`**, fill **`RESUME_*`**, then **arm** (~110–113). **`PUSH_SHIFT.c`** analogous. **Delimited** uses **`kos_build_return_plan`** inside **`kos_return_delimited`**.

**New:** **`KosEvalState_ArmPopShiftPlan`**, **`KosEvalState_ArmPushShiftUnwindPlan`**, … — each **`static inline`** or **`eval.c`** function that calls **`kos_dyn_build_*`** + **`KosEvalState_ArmTransferPlan`**, preserving **every** field assignment currently between **build** and **return `KOS_OP_PROLOGUE()`**.

**Hard rule after this step:** no opcode file contains **`kos_dyn_build_*`** /
**`kos_build_*_plan`** immediately followed by manual **`KosContext_PLAN`**
assignment.

**Gate:** **`tox -qe test`** green after Phase **B1** and **B2**.

---

## 11. Phased migration (ordered, with exit criteria)

| Phase | Work | Done when |
|-------|------|-----------|
| **A** | Remove dead **`ctx`** in five opcodes (§5.3) | Those files contain no **`KosEvalState_CONTEXT`**; **`tox -qe test`** green. |
| **B1** | Add §10 wrappers required by §5.2 opcodes (**§10.1–10.7**, **§10.2–10.5**; **`KReturnDelimited`** §10.12 if **`RETURN.c`** stays on delim path); convert those opcode files | No **`KosContext_*`** in those opcode files. **`tox -qe test`** green. |
| **B2** | **`utils.c`**, **`func.c`**, **`NEXT`/`ITER_NEXT`** (§10.11), **`cont.c`** (§10.10) per §8; **`kos_tail_koscall`** call sites → §10.13 | No **`KosContext_*`** there except Phase **G** exceptions. **`tox -qe test`** green. |
| **C** | **`kos_dyn_transition`**: either **`static`** in **`dynchain.c`** or not in **`dynchain.h`**; all callers use **`KosEvalState_Transition`** | **`rg kos_dyn_transition koskari/lib`** shows only **`dynchain.c`** (and **`KosEvalState_Transition`** body). |
| **D** | **`kos_dyn_gen_frozen_needs_install_es`**; **`iter.c`**, **`demand.c`** | No **`KosContext *`** for this check on paths that hold **`es`**. |
| **E** | Plan driver, Python bridge, tail-koscall, agen flatten live in **`eval.c`** (single TU with the rest of the evaluator) | **`eval.c`** remains coherent; full **`tox -qe test`**. |
| **F** | **`rg 'KosContext_DYN_TOP\([^)]+\)\s*=' koskari/lib`** — fold non-`dynchain`/`eval` assigns into commit helper | All assigns documented; only allowed files remain. |
| **G** | **`coro.c`**, **`domain.c`**: §10.9 / domain wrappers where **`es` exists** | Documented list of remaining **`ctx`-only** entry points (if any). |

**Phase order:** **A → B1 → B2 → C → D → E → F → G**. **C** follows **B1** so
opcodes already call **`KosEvalState_Transition`** before **`kos_dyn_transition`**
visibility is reduced.

---

## 12. Verification

Run from repository root **`sibilant-ng`**:

| Check | Command |
|-------|---------|
| Full suite | **`tox -qe test`** |
| Opcode context binding | **`rg 'KosEvalState_CONTEXT\(es\)' koskari/lib/opcode`** — empty after **B1** (except if a file is exempt; there should be **none**). |
| Opcode context macros | **`rg 'KosContext_' koskari/lib/opcode`** — empty after **B1** (headers may still define types). |
| `dyn_top` assignments | **`rg 'KosContext_DYN_TOP\([^)]+\)\s*=' koskari/lib`** — after **F**, only **`dynchain.c`**, **`eval.c`**, and any explicitly documented exceptions. |

---

## 13. File reference

| Path | Role |
|------|------|
| `koskari/lib/eval.h` | `KosContext`, `KosEvalState`, inline sync |
| `koskari/lib/eval.c` | Driver, refresh/apply, lifecycle, await bridge |
| `koskari/lib/dynchain.{c,h}` | Chain, transition, xfer, plan builders |
| `koskari/lib/dynchain/*.c` | Per-node helpers |
| `koskari/lib/opcode/*.c` | Opcode handlers (**207**) |
| `koskari/lib/func.c` | Callable / expression install |
| `koskari/lib/utils.c` | Apply native handler |
| `koskari/lib/cont.c` | Continuation token |
| `koskari/lib/coro.c` | Coroutine step |
| `koskari/lib/domain.c` | Domain scope |
| `koskari/lib/iter.c`, `demand.c` | Async gen / demand |

---

## Appendix B: Transfer plan and prologue control flow

**Purpose:** Same mental model for **`RETURN`**, **shift**, **delim**, and any
opcode that arms a plan.

```
Opcode / helper                    Eval loop
─────────────────                  ───────────────────────────────────
Build KosTransferPlan *plan   OR   (plan built elsewhere)
        │
        ▼
KosEvalState_ArmTransferPlan(es, plan)
  • kos_free_transfer_plan(old) if old
  • ctx->plan = plan
  • KOS_CTX_IN_TRANSFER |= 1
        │
        ▼
return KOS_OP_PROLOGUE()    ───►  Prologue runs run_transfer_plan_to_completion
        │                         (may suspend; may nest via plan_stack)
        │                         When done: **`clear_and_free_transfer_plan`**
        │                         ( **`DisarmTransferPlan`** ) clears plan + **`IN_TRANSFER`**
        ▼
Later: return KOS_OP_RUN()  ◄───  HasActiveTransferPlan(es) == 0
```

**Exception (delimited `k(value)`):** **`KosEvalState_KReturnDelimited`** (§10.12)
calls **`run_transfer_plan_to_completion`** **inside** the same opcode path
after arming — the diagram still applies, but the “later RUN” may happen in the
same **`RETURN`** invocation after the inner driver returns.

**`RETURN.c` tail:** **`KosContext_PLAN(ctx) ? KOS_OP_PROLOGUE() : KOS_OP_RUN()`**
becomes **`KosEvalState_HasActiveTransferPlan(es) ? KOS_OP_PROLOGUE() :
KOS_OP_RUN()`** — same branch, different spelling.

**`POP_WIND` async:** **`unwind_target_frame`** is set **only** when
**`kos_dyn_pop_top`** reports **`KOS_DYN_UNWIND_ASYNC_STARTED`**; **`RETURN`**
uses that staging when the thunk completes. **`KosEvalState_SetUnwindTargetFrame`**
centralizes the write.

---

## Revision history

| Date | Change |
|------|--------|
| 2026-04-27 | Initial draft |
| 2026-04-27 | Second pass: decisions, field appendix, API list, phases |
| 2026-04-27 | Third pass: TOC, glossary, Appendix B, IN_TRANSFER vs ArmTransferPlan, naming alignment, phase ordering (**C** after **B1**), removed redundant **`LeaveTransfer`** from public §10.2 |
| 2026-04-27 | Fourth pass: §10 **Current / New / refactor** tables; **`KReturnDelimited`** (§10.12), **`TailKoscallReplaceFrame`** (§10.13), opcode plan builders §10.14; **`DisarmTransferPlan`** ↔ **`clear_and_free_transfer_plan`**. |
| 2026-05-26 | Cross-link: [eval-state-live-authority.md](../interpreter/eval-state-live-authority.md) (choke-point funnel, parity constraints; orthogonal to opcode `ctx` macro migration). |
| 2026-05-26 | §1.3: incremental Tier-1 retarget implemented; Tier 2 rebuild deferred per live-authority Phase 2. |
