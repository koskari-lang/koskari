# Demand call flattening (phase 2)

This document specifies **phase 2** of demand-scoped evaluation: when koskari
bytecode calls `Demand(KosCallable)`, detect the pattern at call-dispatch time,
install the inner callable's frame directly into the running eval context, and
mark it `KOS_FRAME_DEMAND` — eliminating the C-stack round-trip through
`demand_tp_call` → `demand_sync_ex` → new `KosContext` → new `koskari_eval`.

**Implementation status:** Not started (design). Prerequisite: phase 1 complete
(branch `demand_in_eval`).

**Audience:** maintainers and agents implementing or reviewing this change.

**Related:**
[demand-evalex-phase1.md](demand-evalex-phase1.md) (phase 1, prerequisite),
[demand-wrapper.md](../api/demand-wrapper.md) (Demand type),
[special-call-protocol.md](../api/special-call-protocol.md) (install slots),
[evaluator.md](evaluator.md).

---

## 1. Goal

When koskari code calls a `Demand`-wrapped `KosCallable` (the common case for
`define-demanding` methods and demand-call hot paths), avoid:

1. Exiting the current `koskari_eval` stack frame into `demand_tp_call`.
2. Allocating a new `KosContext` in `demand_sync_ex`.
3. Entering a nested `koskari_eval` call.
4. Returning through two C-stack frames back into `push_or_await_common`.

Instead, install the inner callable's bytecode frame directly into the
**current** `KosEvalState`, mark it `KOS_FRAME_DEMAND`, and let the existing
eval loop run it — exactly as it would for any ordinary non-demand KosCallable
call, with the demand denial already supplied by phase 1.

Phase 2 is a **pure optimization**: observable behavior is identical to the
non-flattened path when both are correct. Correctness of demand enforcement
remains provided by `KOS_FRAME_DEMAND` + the phase-1 deny guards; phase 2
only removes unnecessary C-stack overhead.

---

## 2. Motivation

### 2.1 Current overhead for `(demand-fn args…)` from koskari

With phase 1 complete, calling a `Demand(KosFunction)` from koskari bytecode:

```
CALL opcode
  → KosEvalState_Call (not KosCallable_Check → PyObject_Vectorcall path)
    → demand_tp_call
      → Koskari_DemandSync → demand_sync_impl → demand_sync_ex
        → KosFunction_PrepareCallNamespace (arg binding)
        → kos_dynframe_new (new frame allocation)
        → KOS_FRAME_DEMAND set on frame
        → KosContext_New (new context allocation)
        → koskari_eval (second nested eval call)
          → … runs callee …
        → KosContext_Free
      → returns result to demand_sync_ex
    → returns result to demand_tp_call
  → result to PyObject_Vectorcall → back in KosEvalState_Call
  → push_or_await_common (result is not awaitable → push on stack)
→ returns to CALL opcode handler
```

Every call crosses **two** C-call-and-return pairs (`demand_tp_call` and
`koskari_eval`) plus allocates and frees a `KosContext`.

### 2.2 Flattened path

```
CALL opcode
  → KosEvalState_Call
      → detect KosDemand_CheckExact → unwrap inner fn
      → do_frame_prep (push new frame)
      → KosCallable_Call (install fn's code + bind args)
      → KOS_FRAME_DEMAND |= on new frame
      → return 0  (eval loop continues in new frame)
→ returns to CALL opcode handler
```

One C-function call, no new `KosContext`, no nested `koskari_eval`.

---

## 3. Scope

### In scope (phase 2)

| Case | Flattened? |
| ---- | ---------- |
| `Demand(KosFunction)` non-yielding, called via `KosEvalState_Call` | Yes |
| `Demand(KosFunction)` non-yielding, called via `KosEvalState_CallArgs` | Yes |
| `Demand(KosFunction)` non-yielding, called via `KosEvalState_CallNoArgs` | Yes |
| `Demand(KosExpression)` non-yielding, any of the above | Yes |
| Tail-call variants (tail_call=1) for all the above | Yes |

### Out of scope / fall-through (phase 2)

| Case | Handled by |
| ---- | ---------- |
| `Demand(python_callable)` | `demand_step_python_result` via `demand_tp_call` (phase 1 §8.1) |
| `Demand(KosFunction)` yielding (YIELDS flag set) | `demand_tp_call` → `DemandGenerator` path |
| `Demand(KosExpression)` yielding | same |
| `Demand(other_KosCallable)` (e.g. `KosType`) | `demand_tp_call` (unchanged) |
| `BoundMethod(Demand(fn))` — bound via descriptor get | deferred (phase 3) |
| `Demand(Demand(fn))` double-wrapping | falls through to demand_tp_call (safe, not optimized) |
| `demand_call` / `demand_apply` from Python | `demand_sync_ex` (unchanged) |

The fall-through paths leave `Demand` as an opaque Python object: the
non-`KosCallable_Check` branch in `KosEvalState_Call*` fires, which calls
`PyObject_Vectorcall(demand_obj, …)` → `demand_tp_call` → `demand_sync_ex`.
Correctness is preserved; only the optimization is absent.

---

## 4. Hook point: `KosEvalState_Call*` in `eval_utils.c`

All three call-dispatch entry points share the same structure. The flattening
logic is a new **preamble** inserted at the start of each, before the existing
`KosCallable_Check` test:

```
KosEvalState_Call(es, out_ev, callable, args, nargs, kwnames, tail_call):
  [NEW] if KosDemand_CheckExact(callable):
            callable, demand_flatten = demand_unwrap_for_flatten(callable)
  if ! KosCallable_Check(callable):
      ... plain Python path (unchanged) ...
  ... KosCallable path (unchanged, except set flag after install) ...
```

`demand_unwrap_for_flatten(callable)` is a small static helper (see §5) that
either returns `(inner_fn, 1)` (flatten) or `(original, 0)` (fall through).

When `demand_flatten == 1`, `callable` has been replaced by the inner
`KosFunction` or `KosExpression`. The rest of the dispatch proceeds as if the
caller wrote `(inner-fn args…)` directly, except we OR in `KOS_FRAME_DEMAND`
after the install slot completes (§6).

When `demand_flatten == 0`, `callable` is still the `Demand` object. The
`KosCallable_Check` test will fail (Demand is not a KosCallable), and the plain
Python branch calls `PyObject_Vectorcall(demand_obj, …)` → `demand_tp_call` as
before. No change to existing behavior.

---

## 5. Unwrap helper: `demand_unwrap_for_flatten`

```c
/**
   If callable is Demand(inner) where inner is a non-yielding KosFunction or
   KosExpression, set *out_callable to inner and return 1 (flatten).
   Otherwise leave *out_callable unchanged and return 0 (fall through).
   Never modifies reference counts — callable is borrowed.
*/
static int demand_unwrap_for_flatten(PyObject *callable,
                                     PyObject **out_callable) {
  if (! KosDemand_CheckExact(callable))
    return 0;

  PyObject *inner = KosDemand_GetWrapped(callable);
  if (! inner)
    return 0;

  if (KosFunction_CheckExact(inner)) {
    PyObject *code = KosFunction_CODE(inner);
    if (code && KosCode_YIELDS((KosCode *) code))
      return 0;   /* yielding → DemandGenerator path */
    *out_callable = inner;
    return 1;
  }

  if (KosExpression_CheckExact(inner)) {
    PyObject *code = KosExpression_CODE(inner);
    if (code && KosCode_YIELDS((KosCode *) code))
      return 0;
    *out_callable = inner;
    return 1;
  }

  return 0;  /* other KosCallable or plain Python → fall through */
}
```

This function **borrows** `callable` and `inner`; it never adds or drops a
reference. The caller continues to hold the borrowed reference to the original
`callable` through `demand_tp_call` if `demand_flatten == 0`.

---

## 6. Setting `KOS_FRAME_DEMAND` after install

After `do_frame_prep` + `KosCallable_Call*` succeed (return value `r >= 0`),
mark the current top frame as a demand root:

```c
/* After KosCallable_Call* returns r: */
if (demand_flatten && r >= 0) {
    KosDynFrame_FLAGS(KosEvalState_FRAME(es)) |= KOS_FRAME_DEMAND;
}
```

### Why this works for non-tail calls

`do_frame_prep` pushed a new empty frame via `kos_dyn_push_frame_empty`.
`KosCallable_Call*` (the install slot) bound args and code to that frame.
After the install slot returns 0, `KosEvalState_FRAME(es)` is the newly
installed frame. Setting `KOS_FRAME_DEMAND` on it gives the phase-1 deny
guards the marker they check on every suspend site.

Child frames pushed during the callee's execution inherit `KOS_FRAME_DEMAND`
automatically via `kos_dyn_push_frame`'s propagation logic (implemented in
phase 1).

### Why this works for tail calls

`do_frame_prep` reset the **current** frame via `kos_dynframe_reset_for_tail`,
which uses `&= KOS_FRAME_DEMAND` to preserve the bit if it was already set.
`KosCallable_Call*` installed the callee's code into the same (reset) frame.
`KosEvalState_FRAME(es)` still points to that frame. OR-ing in
`KOS_FRAME_DEMAND` sets it whether or not the caller was already on the demand
path.

### Edge case: install slot returns non-zero

If `KosCallable_Call*` returns `> 0` (e.g. `KOS_OP_RES_PARENT_RETURN_DELIVER`
for a native handler that completed immediately without installing a persistent
frame), no new frame remains on the stack and the result is already delivered.
In that case demand was trivially satisfied (no suspension occurred) so no
marking is needed. The `r >= 0` guard is safe — it includes both 0 and
positive values — but for defensive correctness consider restricting to `r == 0`
if callers can distinguish "frame installed" from "native immediate return":

```c
/* Strictly: only mark when a frame was installed and eval will continue */
if (demand_flatten && r == 0) {
    KosDynFrame_FLAGS(KosEvalState_FRAME(es)) |= KOS_FRAME_DEMAND;
}
```

Review `KosCallable_Call` return-value semantics before committing to either
form.

---

## 7. Three entry points — annotated diffs

All three entry points in `koskari/lib/eval/eval_utils.c` need the same
treatment. Below, `[+]` marks new lines.

### 7.1 `KosEvalState_Call` (vectorcall)

```c
int KosEvalState_Call(
    KosEvalState *es, KosEvent *out_ev,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call) {

[+] int demand_flatten = demand_unwrap_for_flatten(callable, &callable);

  if (! KosCallable_Check(callable)) {
    /* ... plain Python path unchanged ... */
    PyObject *result = (nargs == 0 && ! kwnames)
        ? PyObject_CallNoArgs(callable)
        : PyObject_Vectorcall(callable, args, (size_t) nargs, kwnames);
    if (! result)
      return -1;
    int r = push_or_await_common(es, out_ev, result, 0);
    if (r == 0 && tail_call)
      r = KOS_OP_RES_CONTROL_RETURNED;
    return r;
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_Call(es, out_ev, callable, args, nargs, kwnames,
                           tail_call);
[+] if (demand_flatten && r == 0)
[+]   KosDynFrame_FLAGS(KosEvalState_FRAME(es)) |= KOS_FRAME_DEMAND;
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
}
```

### 7.2 `KosEvalState_CallArgs` (tuple + dict)

```c
int KosEvalState_CallArgs(
    KosEvalState *es, KosEvent *out_ev,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call) {

[+] int demand_flatten = demand_unwrap_for_flatten(callable, &callable);

  if (! KosCallable_Check(callable)) {
    /* ... plain Python path unchanged ... */
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_CallArgs(es, out_ev, callable, args_tuple, kwds_dict,
                               tail_call);
[+] if (demand_flatten && r == 0)
[+]   KosDynFrame_FLAGS(KosEvalState_FRAME(es)) |= KOS_FRAME_DEMAND;
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
}
```

### 7.3 `KosEvalState_CallNoArgs`

```c
int KosEvalState_CallNoArgs(
    KosEvalState *es, KosEvent *out_ev,
    PyObject *callable,
    int tail_call) {

[+] int demand_flatten = demand_unwrap_for_flatten(callable, &callable);

  if (! KosCallable_Check(callable)) {
    /* ... plain Python path unchanged ... */
  }

  PyTypeObject *typ = Py_TYPE(callable);
  if (do_frame_prep(es, callable, typ, tail_call) < 0)
    return -1;

  int r = KosCallable_CallNoArgs(es, out_ev, callable, tail_call);
[+] if (demand_flatten && r == 0)
[+]   KosDynFrame_FLAGS(KosEvalState_FRAME(es)) |= KOS_FRAME_DEMAND;
  if (r == 0 && tail_call)
    r = KOS_OP_RES_CONTROL_RETURNED;
  return r;
}
```

---

## 8. Reference count discipline

`demand_unwrap_for_flatten` is **borrow-only**: it reads through `Demand` to
the inner callable using `KosDemand_GetWrapped` (which returns a borrowed
reference) and writes the borrowed inner pointer into `*out_callable`. No
`Py_INCREF` / `Py_DECREF` occurs inside the helper.

In each `KosEvalState_Call*` function, `callable` is a borrowed reference
supplied by the CALL opcode handler from the operand stack. When
`demand_flatten == 1`, we replace the local `callable` variable with the
borrowed inner reference. The **original Demand object** remains alive on
the operand stack (the opcode handler holds a reference) for the duration of
the call. Because `Demand` holds a strong reference to `inner`, `inner`
cannot be collected while `Demand` is alive.

Therefore: **no reference count changes are needed** in the hot path.

If the opcode handler pops the Demand object from the stack before the call
dispatch runs, the borrowed pointer becomes dangling. Verify that the calling
convention in each CALL opcode holds the callable reference across the dispatch.
If not, take a temporary strong reference:

```c
/* Only if necessary — prefer the borrow path */
if (demand_flatten)
    Py_INCREF(callable);  /* inner; Demand may be popped */
/* ... dispatch ... */
if (demand_flatten)
    Py_DECREF(callable);
```

Review each CALL opcode's stack discipline before deciding.

---

## 9. `KosExpression` argument handling

`KosExpression` takes no arguments (`nargs == 0`, `kwnames == NULL`). Its
install slot should enforce this. If `Demand(expr)` is called with arguments
(e.g. via `KosEvalState_Call` with `nargs > 0`), the KosExpression install
slot returns an error (or the arg-count check in `demand_sync_ex` fires). No
special case is needed in the flattening preamble — the install slot handles it.

For `KosEvalState_CallNoArgs`: calling `Demand(expr)` with no args is the
canonical case and flattens cleanly.

---

## 10. Interaction with phase 1 deny guards

After the flattened frame is installed and `KOS_FRAME_DEMAND` is set:

- **Nested non-demand calls**: child frames inherit `KOS_FRAME_DEMAND` via
  `kos_dyn_push_frame` propagation (phase 1). Deny guards fire anywhere in
  the subtree.
- **Tail calls within the callee**: `kos_dynframe_reset_for_tail` preserves
  `KOS_FRAME_DEMAND` via `&= KOS_FRAME_DEMAND` (phase 1). The flag survives
  tail-call chains within the demand extent.
- **Return**: when the callee's frame is popped, the caller's frame (which may
  or may not have `KOS_FRAME_DEMAND`) becomes current. Demand enforcement
  returns to whatever the caller's state was — correct.
- **Exception propagation**: `KoskariSyncViolation` raised by the deny guards
  propagates through the normal eval exception machinery (TRY/finally, etc.)
  just as any in-eval exception does. The demand origin is already in the
  exception at raise time.

No changes to phase 1 guard sites are needed.

---

## 11. Interaction with `BoundMethod(Demand(fn))`

When koskari resolves an attribute access `obj.method` where `method` is a
`Demand(fn)` class attribute, `Demand.__get__` returns
`KosBoundMethod(Demand(fn), obj)`. Calling this bound method goes through the
`KosBoundMethod` install slot, which prepends `obj` to the arg list and then
calls the inner func (`Demand(fn)`) via `demand_tp_call`.

Phase 2 does **not** intercept this path. `Demand(fn)` is dispatched from
inside the BoundMethod install slot, not from `KosEvalState_Call*`. The result
is correct (demand_tp_call handles it) but not flattened.

A future **phase 3** could intercept at BoundMethod install: detect
`KosDemand_CheckExact(bm->func)`, unwrap to `fn`, install `fn`'s frame with
self prepended to args, set `KOS_FRAME_DEMAND`. This requires changes to the
`KosBoundMethod` install slot and is deferred.

---

## 12. Tests

Add to `tests/lib/test_demand_sync.py`, new class `TestDemandCallFlattening`
with marker `phase_lib` (or extend `TestDemandInEvalDenial`):

| Test | What it verifies |
| ---- | ---------------- |
| `test_flatten_kos_function_returns_value` | `demand(fn)(args)` from koskari returns correct value (equivalence with `demand-call`) |
| `test_flatten_kos_function_deny_await` | Koskari code calls `demand(fn)` where `fn` does AWAIT; raises `KoskariSyncViolation` via in-eval deny |
| `test_flatten_finally_runs_on_deny` | `fn` has `try`/`finally` around AWAIT; finally block executes before `KoskariSyncViolation` propagates |
| `test_flatten_nested_call_inherits_demand` | `demand(outer)()` where `outer` calls `inner()` (no Demand wrapper); `inner` does AWAIT; still raises via frame-flag inheritance |
| `test_flatten_tail_call_preserves_demand` | `demand(fn)()` where `fn` tail-calls `g`; `g` does AWAIT; raises (tail reset preserves KOS_FRAME_DEMAND) |
| `test_flatten_kos_expression` | `demand(expr)()` where expr doesn't suspend; returns value |
| `test_flatten_yielding_function_falls_through` | `demand(gen_fn)()` falls through to `demand_tp_call` and produces `DemandGenerator` (not a raw generator) |
| `test_flatten_python_callable_falls_through` | `demand(lambda: None)()` from koskari hits `demand_step_python_result` path, not the KosCallable flatten |

Most of these tests can be written using the `build_code` / `make_callable`
test helpers already used in `TestDemandInEvalDenial`. The koskari-from-koskari
call can be constructed by building two code objects and linking them via a
namespace.

---

## 13. Out of scope / follow-ups

| Topic | Phase 2 | Later |
| ----- | ------- | ----- |
| `BoundMethod(Demand(fn))` flattening | No | Phase 3 (BoundMethod install change) |
| `Demand(Demand(fn))` optimization | No | Trivial to add if needed |
| `define-demanding` compile-time Demand elision | No | Compiler change; demand semantics still correct without it |
| `KOS_DYN_DEMAND` dyn node | No | Would enable dynamic-extent checks outside the frame |
| `DemandSyncGenerator` step under KOS_FRAME_DEMAND | No | Out of scope |

---

## 14. Implementation checklist

1. [ ] Add `demand_unwrap_for_flatten` as a static function in
       `koskari/lib/eval/eval_utils.c` (before `KosEvalState_Call`). Include
       `demand.h` if not already included via `lib.h`.
2. [ ] Patch `KosEvalState_Call`: add preamble + post-install flag (§7.1).
3. [ ] Patch `KosEvalState_CallArgs`: same pattern (§7.2).
4. [ ] Patch `KosEvalState_CallNoArgs`: same pattern (§7.3).
5. [ ] Verify reference-count discipline for each CALL opcode's stack pop order
       (§8); add `Py_INCREF`/`DECREF` guard only if the callable is popped
       before dispatch.
6. [ ] Add `TestDemandCallFlattening` tests (§12).
7. [ ] `tox -qe py -- tests/lib/test_demand_sync.py` — all tests pass.
8. [ ] `make test` — full suite passes.
9. [ ] Update this doc's implementation status line.

---

## 15. File index

| File | Change |
| ---- | ------ |
| `koskari/lib/eval/eval_utils.c` | `demand_unwrap_for_flatten`; preamble + post-install flag in `KosEvalState_Call`, `KosEvalState_CallArgs`, `KosEvalState_CallNoArgs` |
| `tests/lib/test_demand_sync.py` | `TestDemandCallFlattening` class |
| `design/interpreter/demand-evalex-phase2.md` | this file |
| `design/TODO.md` | mark phase 2 complete when done |
| `design/api/demand-wrapper.md` | update status when done |

# The end.
