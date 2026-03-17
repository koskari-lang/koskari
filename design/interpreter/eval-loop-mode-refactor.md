# Mode-Based Eval Loop Refactor (Design)

This document describes a refactor of the Koskari eval loop around a **mode** as the single outcome of each opcode (and of the loop prologue). The goal is to optimize the common case—**MODE_NORMAL** ("just continue handling opcodes")—by gating prologue checks and using tail-dispatch so that the fast path does not run transfer plan, pending exception, or native-frame checks.

**Status:** Design. Not yet implemented.

---

## 1. Goals

- **Optimize MODE_NORMAL:** When an opcode returns "normal," skip the loop prologue (transfer plan, pending exception, native frame) and tail-dispatch directly to the next opcode (`prepare_dispatch` then `goto *optable[op]`).
- **Unify outcome:** Replace ad-hoc opcode return values (-1, 0, 1, 2) with a single **mode** (and optional **event** when stopping). Mode indicates what the loop should do next; event (existing `KosEvent`) carries boundary details when we stop.
- **Clear contract:** Every opcode (and every install path invoked by CALL) must be mapped to the modes and events it can produce, under which conditions. Identify gaps where the current return value does not determine the correct mode.

---

## 2. Mode Enum and Macros

### 2.1 Modes

```c
typedef enum KosEvalLoopMode {
  KOS_MODE_NORMAL = 0,      /* Continue: tail-dispatch to next opcode; no prologue */
  KOS_MODE_PROLOGUE,       /* Next iteration run prologue (transfer plan, pending exception, native frame); refresh dc */
  KOS_MODE_POST_OPCODE,    /* Run post_opcode (control returned, exception handling, suspend bookkeeping); then re-enter at mode */
  KOS_MODE_EVENT,          /* Boundary event in out_ev (RETURN/RAISE/YIELD/AWAIT); stop and return to caller */
  KOS_MODE_ERROR,          /* PyErr set; fatal, eval returns -1 */
} KosEvalLoopMode;
```

- **NORMAL** — By far the most common. No prologue, no post_opcode; optionally refresh dispatch context (stack/ns) from current frame, then tail-dispatch.
- **PROLOGUE** — Next iteration must run the full prologue (transfer plan, pending exception, native frame). Also the signal to **repopulate local frame data** (stack block pointer, namespace) in the dispatch context from `CURRENT_FRAME(ctx)` before the next opcode.
- **POST_OPCODE** — Run `eval_phase_post_opcode` (e.g. control returned to wind/with, exception handling, suspend). Then loop continues at mode (prologue runs next iteration unless we stop).
- **EVENT** — `out_ev` has been filled with a boundary event (kind + payload). Eval stops and returns to caller; no change to `KosEvent` structure.
- **ERROR** — Fatal: PyErr is set; eval returns -1. (Can be represented as a mode or as a separate "negative" return; see macros below.)

### 2.2 Opcode Return Macros

Opcode handlers should return an `int` that the loop maps to a mode. For clarity and to avoid magic numbers, use named macros that document intent:

```c
/* Success: continue to next opcode without prologue (tail-dispatch). */
#define KOS_OP_SUCCESS           0

/* Next iteration must run prologue (e.g. transfer plan set, native handler installed). */
#define KOS_OP_NEED_PROLOGUE     3

/* Run post_opcode (control returned, suspend, WITH rollback, etc.). */
#define KOS_OP_POST_OPCODE       2   /* same as current KOS_OP_RES_CONTROL_RETURNED where applicable */
#define KOS_OP_SUSPEND           1   /* suspend / WITH async / KOS_DYN_UNWIND_ASYNC_STARTED */

/* Boundary event: out_ev filled (RETURN/RAISE/YIELD/AWAIT). Caller handles. */
#define KOS_OP_EVENT             1   /* when out_ev->kind is set; overloaded with SUSPEND in current code */

/* Fatal error: PyErr set. */
#define KOS_OP_ERROR            -1
```

**Note:** Today `1` is used both for "suspend (await/yield)" and for "WITH async rollback"; post_opcode branches on `dc->insn` and `out_ev` to decide. Under the refactor, we can keep a single "non-zero means not NORMAL" and have the loop/post_opcode decode via mode or out_ev. The exact numeric encoding can be chosen so that MODE_NORMAL is the only case that skips prologue (e.g. only 0 → NORMAL, all positive → POST_OPCODE or EVENT, negative → ERROR). Suggested mapping:

- Return **0** → MODE_NORMAL (tail-dispatch).
- Return **positive** → MODE_POST_OPCODE or MODE_EVENT; loop runs post_opcode or handles event. Optionally encode which in a separate field (e.g. `out_ev->loop_action`) or in the high bits.
- Return **KOS_OP_NEED_PROLOGUE** (e.g. 3) → MODE_PROLOGUE; next iteration run prologue, refresh dc, then dispatch.
- Return **-1** → MODE_ERROR.

So macros can be:

```c
#define KOS_OP_SUCCESS           0
#define KOS_OP_NEED_PROLOGUE     3
#define KOS_OP_POST_OPCODE       2   /* control returned, wind/with handling */
#define KOS_OP_SUSPEND           1   /* suspend (await/yield/WITH async); out_ev may be set */
#define KOS_OP_ERROR            -1
```

Event case: when the opcode fills `out_ev` with RETURN/RAISE/YIELD/AWAIT and returns 1, the loop treats that as MODE_EVENT (stop). So "return 1" can mean either POST_OPCODE (continue after post_opcode) or EVENT (stop); the loop distinguishes by checking `out_ev->kind` or a dedicated flag. For the design we just need: **any non-zero return that means "don't tail-dispatch"**; the loop then runs post_opcode and/or handles event.

---

## 3. Two-Stage Loop (Mode → Opcode → Mode)

The eval loop is a two-stage state machine:

1. **Mode (first):** At the start of each iteration, the **mode** (from the previous step or entry) decides whether to run the **prologue**: transfer plan, pending exception check, native frame check. If mode is NORMAL and we're continuing from a previous NORMAL, we skip prologue and go straight to opcode. Otherwise we run prologue (and refresh dc), then go to opcode.
2. **Opcode (second):** Run one instruction: `prepare_dispatch` → dispatch (computed goto to opcode handler). The handler returns a **result** (int) and may set **out_ev**.
3. **Back into mode:** The result (and out_ev) determine the **next mode**. If NORMAL, tail-dispatch (prepare_dispatch for next insn, then `goto *optable[op]`). If PROLOGUE, next iteration run prologue. If POST_OPCODE, run post_opcode then re-enter at mode. If EVENT, stop and return. If ERROR, cleanup and return -1.

Flow:

```
  [MODE]  →  (prologue or skip)  →  [OPCODE]  →  (result → next mode)  →  [MODE]  →  ...
```

Mode comes first each time; opcode runs once; then the outcome flows back into the next mode.

---

## 4. Dispatch Context (dc) Refresh

When the loop runs the **prologue** (MODE_PROLOGUE or first time after POST_OPCODE/EVENT), it should **repopulate** the dispatch context from the current frame so that opcodes see up-to-date stack and namespace pointers without chasing ctx every time:

- `dc->stack = &KosDynFrame_STACK(CURRENT_FRAME(ctx));`
- `dc->ns = KosDynFrame_NS(CURRENT_FRAME(ctx));`

So MODE_PROLOGUE (and any re-entry that runs prologue) is the natural place to refresh dc. After a NORMAL tail-dispatch we stay in the same frame, so dc remains valid; after CALL (new frame) or RETURN (back to caller) we eventually hit PROLOGUE or a re-entry that runs prologue and refresh.

---

## 5. Current Return Codes and Mapping to Modes

| Current return | Meaning (current loop)           | Target mode      |
|----------------|-----------------------------------|------------------|
| 0              | Normal continue                   | MODE_NORMAL      |
| 1              | Suspend / WITH async / async started | MODE_POST_OPCODE or MODE_EVENT |
| 2 (KOS_OP_RES_CONTROL_RETURNED) | Control returned to dyn node   | MODE_POST_OPCODE |
| -1             | Error (PyErr set)                 | MODE_ERROR       |

Opcodes and helpers also return **KOS_DYN_UNWIND_ASYNC_STARTED** (1) and **KOS_DYN_UNWIND_SWALLOWED** (2); the latter is not the same as KOS_OP_RES_CONTROL_RETURNED. So we have:

- **-1** → ERROR
- **0** → NORMAL
- **1** → SUSPEND / EVENT / async started (post_opcode or stop with event)
- **2** → CONTROL_RETURNED (post_opcode) or unwind swallowed (0 in POP_WITH/POP_DYN_WITH)

The refactor should assign a single canonical meaning to each opcode return value and map that to a mode.

---

## 6. Per-Opcode Mode and Event Reference

For each opcode we list: **modes** it can produce, **events** it can set in `out_ev`, and **conditions**. This is the contract for the refactor and for finding gaps.

### 6.1 Opcodes That Only Return 0 or -1 (Pure NORMAL or ERROR)

These have no ambiguity; they return 0 (→ NORMAL) or -1 (→ ERROR).

| Opcode | Modes | Conditions |
|--------|--------|------------|
| NOOP, LABEL, POSITION | NORMAL, ERROR | ERROR on internal bug |
| DUP, POP | NORMAL, ERROR | ERROR on stack underflow |
| CONST, CAR, CDR | NORMAL, ERROR | ERROR on missing const or stack |
| CONS, PROPER, PAIR_SPLICE | NORMAL, ERROR | ERROR on stack or allocation |
| JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR | NORMAL, ERROR | ERROR on bad label |
| CHECK_* | NORMAL, ERROR | ERROR on check failure (or jump) |
| GLOBALS, LOCALS | NORMAL, ERROR | |
| IMPORT, IMPORT_REL, IMPORT_FROM | NORMAL, ERROR | |
| EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH | NORMAL, ERROR | |
| VAR, VAR_BIND, VAR_CLEAR, VAR_REF, VAR_STORE, VAR_OBJECT_* | NORMAL, ERROR | |
| FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE, FAST_OBJECT_* | NORMAL, ERROR | |
| ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE | NORMAL, ERROR | |
| ITEM, ITEM_CLEAR, ITEM_REF, ITEM_STORE | NORMAL, ERROR | |
| CONST_ITEM, CONST_ITEM_CLEAR, CONST_ITEM_REF, CONST_ITEM_STORE | NORMAL, ERROR | |
| DEREF | NORMAL, ERROR | |
| BUILD_ARGS, BUILD_CLASS, BUILD_FUNCTION, BUILD_KWDS, BUILD_KWDS_NAMES, COMBINE_ARGS, COMBINE_KWDS | NORMAL, ERROR | |
| OBJECT_* (all) | NORMAL, ERROR | |
| PUSH_FOREACH, PUSH_LOOP, PUSH_NS, PUSH_TRY, PUSH_FINALLY, PUSH_COLLECT | NORMAL, ERROR | |
| CLEAR_*, POP_LOOP, POP_NS, POP_COLLECT, POP_FINALLY | NORMAL, ERROR | |
| PUSH_PARAM, PARAM, POP_PARAM | NORMAL, ERROR | |
| PUSH_PROMPT | NORMAL, ERROR | |
| PUSH_WIND | NORMAL, PROLOGUE(suspend), ERROR | See 6.3 |
| POP_WIND | NORMAL, PROLOGUE(suspend), ERROR | Can return KOS_DYN_UNWIND_ASYNC_STARTED |
| THEEND | NORMAL, ERROR | |

### 6.2 Control and Call Opcodes (Multiple Modes/Events)

| Opcode | Modes | Events | Conditions |
|--------|--------|--------|------------|
| **RAISE** | ERROR, **PROLOGUE** | — | **Gap:** Currently returns 0 after setting PyErr. Must return a mode that causes exception handling (PROLOGUE or POST_OPCODE) so next iteration sees the exception. |
| **RETURN** | NORMAL, POST_OPCODE, EVENT, ERROR | RETURN, RAISE | opa=0/1/N; delimited path; normal return to parent (POST_OPCODE + control returned); top-level (EVENT RETURN); async/finally paths (0); error. |
| **YIELD** | NORMAL, EVENT, ERROR | YIELD | In-gen: EVENT YIELD. Flattened: NORMAL (value pushed to caller). |
| **YIELD_FROM** | NORMAL, POST_OPCODE, EVENT, ERROR | YIELD, AWAIT, RETURN | Many paths; suspend (1), complete with value (0), event (1 + out_ev). |
| **AWAIT** | NORMAL, EVENT, ERROR | AWAIT | push_or_await: 0 push, 1 suspend (out_ev AWAIT). |
| **BREAK** | NORMAL, PROLOGUE(suspend), ERROR | — | kos_dyn_transfer_to_loop: 0 (same-frame), 1 (async started), -1. |
| **CONTINUE** | NORMAL, PROLOGUE(suspend), ERROR | — | Same as BREAK. |
| **CALL** | NORMAL, **PROLOGUE**, POST_OPCODE, EVENT, ERROR | AWAIT | KosCallable path: install can set native handler (apply) → must return PROLOGUE. Plain Python: push_or_await → 0 or 1 (EVENT AWAIT). |
| **CALL_NAMES** | NORMAL, PROLOGUE, POST_OPCODE, EVENT, ERROR | AWAIT | Same as CALL. |
| **CALL_STAR** | NORMAL, PROLOGUE, POST_OPCODE, EVENT, ERROR | AWAIT | Via KosContext_CallArgs. |
| **CALL_ATTR**, **CALL_ATTR_NAMES**, **CALL_ATTR_STAR** | NORMAL, PROLOGUE, POST_OPCODE, EVENT, ERROR | AWAIT | Same pattern. |
| **TAIL_CALL**, **TAIL_CALL_NAMES**, **TAIL_CALL_STAR** | NORMAL, PROLOGUE, POST_OPCODE, EVENT, ERROR | AWAIT | Can return KOS_OP_RES_CONTROL_RETURNED (2). |
| **PUSH_WITH**, **PUSH_DYN_WITH** | NORMAL, PROLOGUE(suspend), ERROR | — | Return 1 when enter is async (KOS_DYN_UNWIND_ASYNC_STARTED). |
| **POP_WITH**, **POP_DYN_WITH** | NORMAL, PROLOGUE(suspend), ERROR | — | Return KOS_DYN_UNWIND_ASYNC_STARTED (1) or 0 (swallowed). |
| **PUSH_SHIFT** | NORMAL, **PROLOGUE**, ERROR | — | **Gap:** When it sets `KosContext_PLAN(ctx) = plan` and returns 0, next iteration must run prologue. Must return PROLOGUE. |
| **POP_SHIFT** | NORMAL, **PROLOGUE**, ERROR | — | **Gap:** Same; sets plan, returns 0. Must return PROLOGUE. |
| **INVOKE_K**, **INVOKE_K_VAR** | NORMAL, **PROLOGUE**, ERROR | — | **Gap:** kos_invoke_continuation sets `KosContext_PLAN(ctx)` and returns 0. Must return PROLOGUE. |
| **POP_PROMPT** | NORMAL, PROLOGUE(suspend), EVENT, ERROR | RETURN | Can return 0, 1 (event), or -1. |
| **POP_FINALLY** | NORMAL, ERROR | — | |
| **ITER_NEXT** | NORMAL, POST_OPCODE, ERROR | — | Can return 0 (jump), 1 (suspend), -1. |
| **NEXT** | NORMAL, POST_OPCODE, EVENT, ERROR | AWAIT | Attach gen frame (0); suspend (1); error. |
| **CAPTURE_CC** | NORMAL, ERROR | — | |
| **POP_CC** | NORMAL, ERROR | — | |

### 6.3 Install Paths (Invoked by CALL)

These are not opcodes but are reached via CALL when the callable is a KosCallable with an install function. They determine the mode after the call.

| Install | Modes | Conditions |
|---------|--------|------------|
| **kos_install_function** | NORMAL | Sets frame code/ns; no plan, no native handler. |
| **kos_install_expression** | NORMAL | Same. |
| **kos_install_boundmethod** | NORMAL | Delegates to kos_install_function. |
| **kos_install_apply** | **PROLOGUE** | **Gap:** Sets `KosDynFrame_NATIVE_HANDLER(frame) = apply_native_handler` and returns 0. Next iteration must run native-frame check. Must return PROLOGUE (or equivalent). |
| **kos_install_continuation** (cont.c) | NORMAL or PROLOGUE? | May set plan via kos_invoke_continuation; needs audit. |

---

## 7. Gap Analysis (Places Where Mode Cannot Be Inferred From Current Return)

These are cases where the **current** return value (0 or 1) does **not** allow the loop to choose the correct next step. They must be fixed so that the refactored loop can rely on mode.

1. **RAISE**  
   Returns **0** after setting PyErr. With a gated prologue, the next iteration would skip prologue and run the next opcode, so the exception would never be handled.  
   **Fix:** RAISE must return a mode that forces prologue (or post_opcode) so that the pending-exception check runs (e.g. **PROLOGUE** or a dedicated "exception set" that runs process_raise).

2. **kos_install_apply**  
   Sets a **native handler** on the current frame and returns **0**. Next iteration would skip the native-frame check and run bytecode for a frame that should be driven by the native handler.  
   **Fix:** When install sets `NATIVE_HANDLER`, return **PROLOGUE** (or a code that maps to MODE_PROLOGUE).

3. **PUSH_SHIFT** (unwind path)  
   Sets `KosContext_PLAN(ctx) = plan` and **returns 0**. Next iteration would skip prologue and never run `run_transfer_plan_to_completion`.  
   **Fix:** When PUSH_SHIFT sets a transfer plan, return **PROLOGUE**.

4. **POP_SHIFT**  
   Sets `KosContext_PLAN(ctx) = plan` and **returns 0**. Same as PUSH_SHIFT.  
   **Fix:** Return **PROLOGUE** when plan is set.

5. **INVOKE_K / INVOKE_K_VAR**  
   `kos_invoke_continuation` sets `KosContext_PLAN(ctx)` and returns 0. Next iteration must run the transfer plan.  
   **Fix:** When the call succeeds and a plan was set, return **PROLOGUE** (from the opcode, which can check whether ctx now has a plan if the C API doesn’t return a distinct value).

6. **Return 1 overload**  
   The value **1** is used for: suspend (YIELD, AWAIT), WITH/POP_WITH async started, ITER_NEXT suspend, YIELD_FROM suspend. The loop and post_opcode distinguish by opcode and out_ev. For the refactor, ensure that whenever we return 1 we also set out_ev or a mode flag so the loop can decide between POST_OPCODE (continue after handling) and EVENT (stop with event). No semantic gap if we keep decoding 1 via out_ev and opcode type.

7. **KOS_DYN_UNWIND_SWALLOWED (2)**  
   POP_WITH/POP_DYN_WITH return 0 when unw == KOS_DYN_UNWIND_SWALLOWED. So 0 is correct (NORMAL). No gap.

8. **Delimited return (RETURN)**  
   RETURN has many paths; some return 0 (e.g. finally taking return, async started). Those 0 returns are correct (eval continues). CONTROL_RETURNED (2) and top-level EVENT (1) are already distinct. No additional gap if all branches are documented.

---

## 8. Suggested Implementation Order

1. **Introduce mode enum and macros** in eval.h; keep existing numeric returns but add macros (KOS_OP_SUCCESS, KOS_OP_NEED_PROLOGUE, KOS_OP_POST_OPCODE, KOS_OP_SUSPEND, KOS_OP_ERROR).
2. **Fix gaps:** RAISE, kos_install_apply, PUSH_SHIFT, POP_SHIFT, INVOKE_K/INVOKE_K_VAR so they return a value that maps to MODE_PROLOGUE when they set plan or native handler.
3. **Refactor loop:** Two entry points: loop_head (run prologue, then dispatch) and fast path (prepare_dispatch, dispatch). On opcode return 0 → tail-dispatch (prepare_dispatch, goto *optable[next]). On non-zero → post_opcode / event handling, then goto loop_head.
4. **Add dc refresh** in prologue (stack, ns from CURRENT_FRAME).
5. **Migrate opcodes** to use the new macros and, where helpful, set a single "next mode" in out_ev or a small extension so the loop doesn’t depend on multiple magic integers.

---

## 9. Summary Table: Opcode Return Value → Mode

| Opcode return | Next mode   | Loop action |
|---------------|------------|-------------|
| 0 (KOS_OP_SUCCESS) | NORMAL   | Tail-dispatch to next opcode; no prologue. |
| 3 (KOS_OP_NEED_PROLOGUE) | PROLOGUE | Next iteration run prologue, refresh dc, then dispatch. |
| 2 (KOS_OP_POST_OPCODE) | POST_OPCODE | Run post_opcode; then continue (re-enter at mode). |
| 1 (KOS_OP_SUSPEND / event) | POST_OPCODE or EVENT | Run post_opcode or stop with out_ev. |
| -1 (KOS_OP_ERROR) | ERROR | Cleanup, return -1. |

---

## 10. Quick Reference: Opcodes with Non-Trivial Returns

| Opcode | Returns | → Mode / Event |
|--------|---------|----------------|
| RAISE | 0 (today; **must change**) | PROLOGUE so exception is seen |
| RETURN | 0, 1, 2, -1 | 0: NORMAL (finally/async); 2: POST_OPCODE (control returned); 1: EVENT (top-level); -1: ERROR |
| YIELD | 0, 1, -1 | 0: NORMAL (flattened); 1: EVENT YIELD; -1: ERROR |
| YIELD_FROM | 0, 1, -1 | 0: NORMAL; 1: EVENT or POST_OPCODE; -1: ERROR |
| AWAIT | 0, 1, -1 | 0: NORMAL; 1: EVENT AWAIT; -1: ERROR |
| CALL / CALL_* / TAIL_CALL_* | 0, 1, -1 (2 for tail) | 0: NORMAL or PROLOGUE if install set plan/native; 1: EVENT AWAIT; 2: POST_OPCODE |
| PUSH_WITH / PUSH_DYN_WITH | 0, 1, -1 | 1: POST_OPCODE (async enter) |
| POP_WITH / POP_DYN_WITH | 0, 1, -1 | 1: POST_OPCODE (async exit) |
| PUSH_WIND | 0, 1, -1 | 1: POST_OPCODE (async wind) |
| POP_WIND | 0, 1, -1 | 1: POST_OPCODE (async wind) |
| PUSH_SHIFT | 0, -1 | **0 when plan set → must be PROLOGUE** |
| POP_SHIFT | 0, -1 | **0 when plan set → must be PROLOGUE** |
| INVOKE_K / INVOKE_K_VAR | 0, -1 | **0 when plan set → must be PROLOGUE** |
| BREAK / CONTINUE | 0, 1, -1 | 0: NORMAL; 1: POST_OPCODE (async unwind); -1: ERROR |
| ITER_NEXT | 0, 1, -1 | 1: POST_OPCODE (suspend) |
| NEXT | 0, 1, -1 | 0: NORMAL; 1: EVENT AWAIT or POST_OPCODE |
| POP_PROMPT | 0, 1, -1 | 1: EVENT RETURN |

---

*Document version: 1. Design may be updated as implementation proceeds.*
