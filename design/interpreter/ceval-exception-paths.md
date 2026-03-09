# C Eval Loop: Exception Paths and Trace Injection

This document scopes out where exceptions enter and propagate through the Koskari
C evaluator (ceval) loop, where `inject_eval_frame_traceback` is called, and
where it is missing. The goal is to ensure that every propagated exception
carries `__koskari_dynchain__` and a usable traceback so Python-side exception
reporting can show native (Koskari) frames.

## 1. Background

### 1.1 Trace injection

`inject_eval_frame_traceback` (eval.c) does two things:

1. **Decorate**: `decorate_exception_with_dynchain(ctx, *exc_val)` attaches
   `__koskari_dynchain__` to the exception value, capturing the current dynamic
   chain.

2. **Inject frame**: Builds a synthetic Python frame from `CURRENT_FRAME(ctx)`,
   uses `PyTraceBack_Here` to append it to the thread-state traceback, then
   attaches the updated traceback to `exc_val.__traceback__` so it survives
   later `PyErr_SetObject` calls (e.g. in coro.c).

Without decoration, the Python REPL cannot display Koskari-native frames and
falls back to Python-only tracebacks.

### 1.2 Relevant context

* Execution invariants: [execution-invariants.md](execution-invariants.md)
* Trace snapshot design: [introspection-trace.md](introspection-trace.md)
* Implementation: `koskari/lib/eval.c`, `koskari/lib/iter.c`, `koskari/lib/coro.c`

---

## 2. Where `inject_eval_frame_traceback` is called

| Location | Phase / Path | Condition |
|----------|--------------|-----------|
| `try_local_exception_handler` (1) | `eval_phase_post_opcode` → `process_raise` | PyErr set after opcode, handler catches |
| `try_local_exception_handler` (2) | Same | Re-raise branch after handling |
| `KOS_MODE_RAISE` in `eval_phase_entry` | Main loop entry | Previously stored KOS_EV_RAISE payload |

All three are on the "normal" exception path: opcode fails → process_raise →
try_local_exception_handler, or a previously packaged KOS_EV_RAISE re-enters
the loop.

---

## 3. `run_transfer_plan_to_completion` (fixed)

### 3.1 Code path

When `kos_run_transfer_step` returns a negative value (failure), the transfer
plan runner fetches the exception, calls `inject_eval_frame_traceback`, then
stores the decorated payload into the event and sets `KOS_EV_RAISE`. This fix
is implemented; transfer-plan failures now carry dynchain and traceback.

### 3.2 Call sites

`run_transfer_plan_to_completion` is invoked from:

1. **`eval_phase_transfer`** (main loop): after a transfer step fails.
2. **Main dispatch loop** (`eval_phase_post_opcode`): when a transfer plan is
   active and a step fails.
3. **`eval_phase_entry`**: when the evaluator enters with an active transfer
   plan (e.g. after yield-from or boundary return).

In all cases, the exception comes from inside the C extension (e.g. failure
during continuation application, boundary handling, or coroutine step). The
exception is stored and later re-raised via `KOS_MODE_RAISE`, which *does*
call `inject_eval_frame_traceback` when the loop processes the stored payload.
**However**, by that point the loop may have advanced: the stored exception
was captured at the failure site, but when `KOS_MODE_RAISE` runs, the dynamic
chain may have changed (e.g. unwind has already popped frames). So the
injection at `KOS_MODE_RAISE` uses the *current* frame, not the frame at
which the transfer failed.

### 3.3 Implementation

The implementation calls `inject_eval_frame_traceback(ctx, &exc_type, &exc_val,
&exc_tb)` immediately after `PyErr_Fetch` and before storing into the event,
ensuring decoration at the failure site while the chain still reflects the
correct frame.

---

## 4. Other exception paths

### 4.1 Opcode failures (CALL, operators, etc.)

Normal opcode execution:

* Opcode returns -1 or calls `PyErr_Set*`
* `eval_phase_post_opcode` sees `PyErr_Occurred()` and calls `process_raise`
* `process_raise` calls `try_local_exception_handler`
* `try_local_exception_handler` calls `inject_eval_frame_traceback` before
  handling or re-raising

**Status**: Decorated.

### 4.2 `handle_continuation_apply` — foreign-token path

When a `KoskariContinuationApply` exception targets a *different* evaluator
(foreign token):

1. `handle_continuation_apply` builds an unwind/transfer plan.
2. The plan eventually fails or completes; any failure goes through
   `run_transfer_plan_to_completion`.
3. The exception is stored and re-raised as in section 3.

So the foreign-token failure path is covered by fixing section 3. The
continuation exception itself is consumed (not re-raised) in this path.

### 4.3 Coroutine / iterator boundary

When the evaluator yields to Python (e.g. `KosCoroutine` or `KosIterator`):

* `iter.c` / `coro.c` receive `KOS_EV_RAISE` and call `PyErr_SetObject` with
  the stored exception value.
* They do **not** re-decorate or re-inject; they pass through the payload
  as-is.

If the exception was decorated and traceback-injected before it reached
`KOS_EV_RAISE` (via the normal path or after the section 3 fix), it remains
intact. The comment in `inject_eval_frame_traceback` notes that
`PyTraceBack_Here` only updates thread-state; attaching `__traceback__` to
the value ensures the traceback survives `PyErr_SetObject`.

**Status**: Correct as long as the exception is decorated before it is stored
in `ctx->ev`.

### 4.4 `PyTraceBack_Here` limitation

`PyTraceBack_Here` updates the thread-state traceback chain only. It does
**not** update `exc_val.__traceback__`. The current implementation explicitly
sets `exc_val.__traceback__` after `PyTraceBack_Here` so that when `coro.c`
(or similar) does `PyErr_SetObject(value, ...)`, the traceback is preserved.
This is already handled.

---

## 5. Summary

| Path | Decorated? | Notes |
|------|------------|-------|
| Opcode failure → process_raise → try_local_exception_handler | Yes | Both handle and re-raise branches |
| KOS_EV_RAISE at loop entry | Yes | Uses current frame; may be late for transfer failures |
| run_transfer_plan_to_completion | Yes (fixed) | Injects at failure site |
| Foreign-token continuation failure | No (same as above) | Fixed by fixing run_transfer_plan |
| Coro/iter boundary | Pass-through | Relies on prior decoration |

The fix has been applied; transfer-plan failures now receive dynchain and
traceback injection at the failure site.

---

## 6. Python 3.9: traceback still missing (known issue)

Even with the `run_transfer_plan_to_completion` fix, `test_repl_koskari_exception_shows_native_traceback` can fail on Python 3.9: the REPL output for `(/ 1 0)` does not show "Koskari dynchain" even though `test_exception_from_called_function_has_inspectable_fields` (CALL to Python function) shows the exception has `__koskari_dynchain__`.

### Possible causes

1. **`PyErr_SetObject` / `__traceback__` behavior (CPython issue #101578)**  
   Python 3.9 stores exceptions in denormalized form; `PyErr_SetObject(exc_type, exc_val)` does not always propagate `exc_val.__traceback__` when the exception crosses the C→Python boundary. Python 3.12+ normalizes earlier. The newer `PyErr_SetRaisedException()` (3.12+) handles this correctly.

2. **`PyTraceBack_Here` / `exc_val.__traceback__`**  
   `inject_eval_frame_traceback` uses `PyObject_SetAttrString(exc_val, "__traceback__", tb)`. In 3.9, this attribute may be ignored or overwritten when the exception is later re-raised via `PyErr_SetObject` in coro.c.

3. **Different opcode paths**  
   `(/ 1 0)` goes through CALL to the division operator (C extension); `(boom)` calls a Python function. Both should hit `try_local_exception_handler`, but the C-operator path may involve different normalization or propagation behavior under 3.9.

### Diagnostic steps

1. Add a temporary assert in `coro.c` at `KOS_EV_RAISE` handling: log whether `KosEvent_PAYLOAD(&ev)` has `__koskari_dynchain__` before `PyErr_SetObject`. This confirms whether decoration is present at the boundary.
2. Run the same check in `_boundary_call_async`'s `except` block: does `e` (the caught cause) have `__koskari_dynchain__`? If not, the loss happens in coro.c or asyncio under 3.9.
3. Check if `PyObject_SetAttrString(val, "__traceback__", tb)` returns -1 or 0 under 3.9; some exception types may reject or overwrite this attribute.

### Potential workarounds

- Use `PyErr_SetRaisedException()` when building for 3.12+ (or `#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12`).
- On 3.9, avoid relying on `exc_val.__traceback__` across `PyErr_SetObject`; instead ensure `_find_dynchain_in_chain` can recover dynchain from `__cause__`/`__context__` when the primary exception's `__traceback__` is missing or stripped.
