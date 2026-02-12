# Handoff: Fix CALL path for undelimited continuations (test_call_cc_capture_and_invoke)

## Goal

Make **`test_call_cc_capture_and_invoke`** pass by fixing the implementation. The test must not be removed or skipped; it must pass.

## Test and behavior

**Test:** `tests/lib/test_ceval.py::TestUndelimitedContinuations::test_call_cc_capture_and_invoke`

**Intended behavior:** `(call/cc (lambda (k) (k 42)))` => `42`. Bytecode: PUSH_CC "resume", CAPTURE_CC 0, POP_CC, CONST 42, **CALL 1** (invoke continuation with 42), LABEL "resume", RETURN 1.

**Current state:** The test **segfaults** inside **`koskari_eval`** (in the extension), **after** the transfer plan has run to completion. So: continuation is invoked via CALL, the plan runs (INSTALL then INJECT), the plan is freed, then the eval loop continues and crashes (likely first instruction after the transfer).

## What was fixed so far

### 1. Token check (eval.c)

**File:** `koskari/lib/eval.c` (CALL handler, ~1054)

**Change:** Treat undelimited continuations with no token as same-evaluator so they are invoked directly instead of raising `KoskariContinuationApply`:

```c
if (k->token_ref == (PyObject *) ctx->token
    || (k->prompt_id == KOS_PROMPT_ID_UNDELIMITED && ! k->token_ref)) {
```

### 2. Double-free when freeing continuation (stack + dynchain + func.c)

**Cause (from gdb):** Crash in `kos_free_transfer_plan` -> `_Py_Dealloc`. The continuation's **`resume_stack`** holds a reference to the **continuation Python wrapper**. Releasing that stack DECREF'd the wrapper; its dealloc then freed the C struct `k` while the same wrapper was still referenced by `plan->continuation_ref`, leading to double-free / use-after-free.

**Changes:**

- **stack.c:**  
  - `KosStackBlock_Release` now calls `KosStackBlock_ReleaseExclude(self, NULL)`.  
  - **`KosStackBlock_ReleaseExclude(self, exclude)`** – when refcount goes to 0, for each slot `objs[i]`, if `objs[i] == exclude` set slot to NULL and do **not** DECREF; otherwise DECREF as usual.  
  - **`KosStackBlock_ReleaseChainExclude(block, exclude)`** – walks the chain and calls `ReleaseExclude` on each block with the same `exclude`.

- **dynchain.c / dynchain.h:**  
  - **`kos_continuation_free_exclude(KosContinuation *k, PyObject *exclude_from_stack)`** – frees `k` but when releasing `k->resume_stack` uses `KosStackBlock_ReleaseChainExclude(..., exclude_from_stack)` so the wrapper is not DECREF'd.  
  - **`kos_continuation_free(k)`** now just calls **`kos_continuation_free_exclude(k, NULL)`**.

- **func.c:**  
  - **`continuation_dealloc`** and **`delimited_continuation_dealloc`** call **`kos_continuation_free_exclude(cont->captured, self)`** so the wrapper (`self`) is passed as the exclude when releasing the continuation's stack.

### 3. Use-after-free of plan's segment ("cannot clone dynnode kind" / garbage kind)

**Cause:** When invoking a multi-shot undelimited continuation we clone `k` -> `to_use`, build the plan from `to_use->captured_chain`, then free `to_use` with **`kos_continuation_free(to_use)`**. That released `to_use->resume_stack`, which DECREF'd the continuation wrapper, ran its dealloc, freed **`k`** and **`k->captured_chain`**. The allocator could reuse that memory before **INSTALL** ran, so the plan's segment (still pointing at the clone's chain) could be overwritten -> garbage `node->kind` in **`kos_dynnode_clone`** or corruption later.

**Changes:**

- **dynchain.c / dynchain.h:**  
  - Do **not** call **`kos_continuation_free(to_use)`** when `to_use != k`.  
  - Set **`plan->invoke_clone = to_use`** (and **`to_use->captured_chain = NULL`** so we don't free the segment when we later release the clone).  
  - **`KosTransferPlan`** has a new field: **`KosContinuation *invoke_clone`**.  
  - In **`kos_free_transfer_plan`**, before releasing **`plan->resume_stack`**: if **`plan->invoke_clone`** is set, call **`KosStackBlock_ReleaseChainExclude(clone->resume_stack, plan->continuation_ref)`** (so we don't DECREF the continuation yet), then **`Py_XDECREF(clone->token_ref)`**, then **`PyMem_Free(clone)`**, then clear **`plan->invoke_clone`**.  
  So the clone and its stack are only released when the plan is freed, i.e. **after** the plan has run (INSTALL + INJECT). The segment used in INSTALL stays valid.

### 4. globals_frame after INSTALL (dynchain.c)

**File:** `koskari/lib/dynchain.c`, INSTALL case (~1506–1530)

**Change:** After **`kos_update_frame_top(ctx)`** in the INSTALL step, set:

- **`ctx->globals_frame = ctx->frame_top`**  
- **`ctx->globals_cache_version = ctx->chain_version + 1`** (and after **`ctx->chain_version++`**, set **`ctx->globals_cache_version = ctx->chain_version`**)

so the resumed segment has a valid globals frame for op_GLOBALS and any cache checks.

## Relevant code locations

- **CALL continuation path:** `koskari/lib/eval.c` ~1010–1082 (continuation detection, token check, **`kos_invoke_continuation`**).
- **Transfer plan run:** `eval.c` ~89–115 **`run_transfer_plan_to_completion`**; start of eval loop ~4480 checks **`ctx->plan`** and runs it.
- **Plan build / invoke:** `koskari/lib/dynchain.c` **`kos_build_transfer_plan`**, **`kos_invoke_continuation`** (~1272, ~1637), **`kos_free_transfer_plan`** (~1422), INSTALL step (~1470–1530), **`kos_run_transfer_step`**.
- **Continuation free / clone:** **`kos_continuation_free`** / **`kos_continuation_free_exclude`**, **`kos_continuation_clone`**; **`KosTransferPlan`** in **dynchain.h** (~398–412) including **`invoke_clone`**.
- **Stack release with exclude:** **`koskari/lib/stack.c`** **`KosStackBlock_ReleaseExclude`**, **`KosStackBlock_ReleaseChainExclude`**; **stack.h** declares them.
- **Wrapper dealloc:** **`koskari/lib/func.c`** **`continuation_dealloc`**, **`delimited_continuation_dealloc`** (call **`kos_continuation_free_exclude(cont->captured, self)`**).
- **Eval loop:** **`eval.c`** ~4476–4620 (plan check, **`code = ctx->frame_top->code`**, **`pc = ctx->frame_top->pc`**, fetch insn, dispatch).

## Test status

- **PASS:** `TestDelimitedContinuations::test_reset_shift_invoke` (INVOKE_K).
- **PASS:** `TestUndelimitedContinuations::test_call_cc_capture_returns_continuation`.
- **PASS:** `TestUndelimitedContinuations::test_capture_cc_without_push_cc_raises`.
- **FAIL (segfault):** **`TestUndelimitedContinuations::test_call_cc_capture_and_invoke`**.

Crash: **SIGSEGV in `koskari_eval`** (extension), **after** the plan has completed (backtrace showed **`koskari_eval`** and **`coroutine_send`**; no longer in **`kos_free_transfer_plan`** or **`_Py_Dealloc`**). So the bug is in the **resumed execution** path (first instruction(s) after the transfer), not in plan free or transfer steps.

## Reproducing under GDB

From repo root (sibilant-ng), after a normal tox run so the venv is ready:

```bash
cd .tox/py312 && gdb -batch \
  -ex "set pagination off" \
  -ex "handle SIGSEGV stop print" \
  -ex "run" \
  -ex "bt 20" \
  -ex "quit" \
  --args bin/python -m pytest /home/siege/sibilant/sibilant-ng/tests/lib/test_ceval.py::TestUndelimitedContinuations::test_call_cc_capture_and_invoke -v
```

Running from **`.tox/py312`** ensures the installed extension is used. A debug build (e.g. **`CFLAGS="-g -O0"`** in the build) will give line numbers in the backtrace.

## What's left to do

1. **Find the exact crash site** in **`koskari_eval`** after the plan completes (e.g. which line/op or which field of **`ctx`** / **`frame_top`** / **`code`** / stack is invalid).
2. **Fix that path** so the resumed frame and first instruction(s) are valid (e.g. correct **`frame_top`**, **`code`**, **pc**, stack, or any other state the eval loop assumes after INSTALL).
3. **Re-run the test** until **`test_call_cc_capture_and_invoke`** passes without crash.

## Constraints

- **Do not** remove or skip **`test_call_cc_capture_and_invoke`**; the correct fix is to make the implementation pass the test.
- Prefer **tox** for running tests (e.g. **`tox -e py312 -- tests/lib/test_ceval.py::TestUndelimitedContinuations::test_call_cc_capture_and_invoke -v`**).
- Project rules: **`.cursor/rules/git.mdc`**, **`.cursor/rules/python.mdc`**, **`.cursor/rules/tox.mdc`**; testing: **`.cursor/rules/testing.mdc`** (no muted/skipped tests).
