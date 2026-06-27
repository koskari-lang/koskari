# KosPartial: KosCallable-aware partial application

This document specifies **KosPartial**, a C-level subtype of CPython's
`functools.partial` that implements the `KosCallable` interface. When koskari
bytecode calls a `KosPartial` object whose inner callable is a KosCallable, the
CALL opcode dispatches through the install-slot fast path — no `KosContext`
allocation, no nested `koskari_eval`.

**Implementation status:** Not started (design).

**Audience:** maintainers implementing or reviewing this change.

**Related:**
- [kosproperty.md](kosproperty.md) — same stdlib-subtype family (`property` +
  auto-`Demand` on accessors)
- [archive/demand-evalex-phase2.md](../archive/demand-evalex-phase2.md) — `KosDemand` as
  KosCallable (same pattern; read this first)
- [demand-wrapper.md](../api/demand-wrapper.md) — Demand wrapper API and status

---

## 1. Motivation

The current dispatch path for `partial(kos_fn, *bound_args)(call_args)`:

```
CALL opcode
  → KosCallable_Check fails  (plain functools.partial is not a KosCallable)
  → PyObject_Vectorcall(partial_obj, ...)
  → partial_tp_vectorcall / partial_tp_call
  → PyObject_Vectorcall(kos_fn, merged_args)
  → kos_fn tp_call
  → new KosContext → koskari_eval
```

After this change, when `partial_obj` is a `KosPartial` and `kos_fn` is a
non-yielding KosCallable:

```
CALL opcode
  → KosCallable_Check passes
  → do_frame_prep (delegates wants_frame to inner callable)
  → kospartial_kos_install
      → merge bound_args + call_args → merged_args
      → KosCallable_CallArgs(inner, merged_args, ..., tail_call=1)
      → inner install slot (e.g. demand_kos_install → kos_install_function)
  → frame populated, eval continues
```

One frame in play throughout. No intermediate KosContext.

---

## 2. `isinstance` compatibility

Python code using `isinstance(obj, functools.partial)` must continue to work.

### Recommended approach: dynamic `tp_base` at init time

`KosPartialObject` at the C level is just a header:

```c
typedef struct {
    PyObject_HEAD
} KosPartialObject;
```

During `koskari_partial_init`, look up `functools.partial`'s type object and set
it as `KosPartialCallableType.type.tp_base` before calling `PyType_Ready`:

```c
PyObject *functools = PyImport_ImportModule("functools");
PyObject *partial_type = PyObject_GetAttrString(functools, "partial");
KosPartialCallableType.type.tp_base = (PyTypeObject *) partial_type;
PyType_Ready(KOS_CALLABLE_PYTYPE(KosPartialCallableType));
```

`PyType_Ready` propagates `tp_basicsize` from the base, so `KosPartialObject`
instances are large enough to hold all of `partialobject`'s fields without
naming them in the C struct.

**Accessing `func`, `args`, `keywords`:** use `PyObject_GetAttrString` (or
cache the member descriptor objects at init time to skip name hashing). These
are `T_OBJECT` member descriptors on `partial`, so each access is a single
pointer read through the descriptor machinery — still O(1).

**Why not embed `partialobject`:** copying `partialobject`'s struct from
`_functoolsmodule.c` gives direct field access but ties the implementation to
CPython internals that can change between releases without notice. The attribute
protocol cost is negligible compared to what it replaces.

---

## 3. `tp_kos_wants_frame_fn`: delegation

```c
static int kospartial_wants_frame(PyObject *self) {
    PyObject *inner = PyObject_GetAttrString(self, "func");
    if (! inner)
        return 0;
    int r = 0;
    if (KosCallable_Check(inner)) {
        KosCallableTypeObject *ctyp = (KosCallableTypeObject *) Py_TYPE(inner);
        r = ctyp->tp_kos_wants_frame_fn
            ? ctyp->tp_kos_wants_frame_fn(inner)
            : ctyp->tp_kos_wants_frame;
    }
    Py_DECREF(inner);
    return r;
}
```

Returns 1 only when the inner callable wants a frame (non-yielding
KosFunction/KosExpression, or a KosCallable wrapper like Demand whose inner is
flattenable). Returns 0 for yielding functions, plain Python callables, etc.

When `kospartial_wants_frame` returns 0, `do_frame_prep` does nothing and the
install slot falls through to the Python call path (§4.2).

---

## 4. `tp_kos_install`: merge and tail-call through

### 4.1 Flattenable inner (inner is KosCallable, `wants_frame` = 1)

```c
static int kospartial_kos_install(
    KosEvalState *es,
    KosEvalState *es,
    PyObject *self,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

    PyObject *inner = PyObject_GetAttrString(self, "func");
    PyObject *bound_args = PyObject_GetAttrString(self, "args");
    PyObject *bound_kw   = PyObject_GetAttrString(self, "keywords");
    /* error checks omitted for clarity */

    /* Merge positional args: bound_args ++ args_tuple */
    PyObject *merged_args = NULL;
    if (PyTuple_GET_SIZE(bound_args) == 0) {
        merged_args = args_tuple;   /* borrow — no allocation */
        Py_XINCREF(merged_args);
    } else {
        merged_args = PySequence_Concat(bound_args, args_tuple);
    }

    /* Merge keyword args: {**bound_kw, **kwds_dict} */
    PyObject *merged_kw = NULL;
    /* ... (see §5 for reference-count details) ... */

    /* Tail-call through: partial has no epilogue, always reuse the frame */
    int r = KosCallable_CallArgs(es, out_ev, inner,
                                 merged_args, merged_kw, 1 /* tail_call */);

    Py_XDECREF(merged_args);
    Py_XDECREF(merged_kw);
    Py_DECREF(inner);
    Py_DECREF(bound_args);
    Py_DECREF(bound_kw);
    return r;
}
```

**Why `tail_call=1` unconditionally:** partial has no code of its own and no
epilogue. Passing `tail_call=1` to the inner install slot tells it "install
into the current frame rather than pushing a new one on top." The current frame
was prepared by the outer `do_frame_prep` (either freshly pushed for a non-tail
outer call, or reset-in-place for a tail outer call). Either way the inner
callable fills that single frame. Frame count: identical to calling the inner
callable directly.

Verify during implementation that `tail_call=1` in the inner install slot does
not double-call `kos_dynframe_reset_for_tail` on a freshly-pushed frame; if it
does, interpose a `do_frame_prep(es, inner, wants_frame, tail_call=1)` call in
the install slot before `KosCallable_CallArgs` to make the frame state
consistent with what the inner expects.

### 4.2 Non-flattenable inner

When `kospartial_wants_frame` returns 0 (yielding fn, Python callable, or inner
not a KosCallable), `do_frame_prep` did nothing. The install slot falls back to
the Python call path:

```c
    PyObject *result = PyObject_Call(self, args_tuple, kwds_dict);
    /* (calls partial's own tp_call which merges bound args) */
    if (! result) return -1;
    if (KosEvalState_StackPUSH(es, result) < 0) return -1;
    return 0;
```

Alternatively, merge args and call `inner` directly via `PyObject_Call(inner,
merged_args, merged_kw)` to avoid redundant arg merging inside partial's own
`tp_call`. Either is correct; the latter avoids one extra merge.

---

## 5. Reference counting in the install slot

- `inner`, `bound_args`, `bound_kw`: new references from `GetAttrString`;
  decremented before return.
- `merged_args`: new reference if `PySequence_Concat` was called; borrowed
  reference (with `Py_INCREF`) if `args_tuple` was reused. Decremented before
  return.
- `merged_kw`: if both `bound_kw` and `kwds_dict` are non-NULL, new reference
  from `PyDict_Copy` + `PyDict_Update`; decremented before return. If only one
  is non-NULL, borrowed + `Py_INCREF`; decremented before return. If both NULL,
  pass NULL to `KosCallable_CallArgs` (no allocation).
- `KosCallable_CallArgs` borrows `merged_args` and `merged_kw`.
- `KosEvalState_StackPUSH` steals the reference to `result`; do not decrement
  on success.

---

## 6. Interaction with Demand

### `partial(demand(fn), *bound_args)`

- `kospartial_wants_frame(self)` → `demand_wants_frame(demand_obj)` →
  `demand_inner_is_flattenable(fn)` → 1 if fn is non-yielding KosFunction.
- Install slot merges args, calls `KosCallable_CallArgs(demand_obj, merged_args,
  tail_call=1)`.
- `demand_kos_install` calls `KosCallable_CallArgs(fn, merged_args, tail_call=1)`.
- fn's install slot fills the frame. Demand ORs `KOS_FRAME_DEMAND` onto it.
- Result: one frame, correctly flagged.

### `demand(partial(fn, *bound_args))`

- `demand_wants_frame` → `demand_inner_is_flattenable(kospartial_obj)` →
  queries `kospartial_wants_frame(kospartial_obj)` → 1 if fn is non-yielding.
- `demand_kos_install` calls `KosCallable_CallArgs(kospartial_obj, call_args,
  tail_call)`.
- `kospartial_kos_install` merges bound_args + call_args, tail-calls through fn.
- Demand ORs `KOS_FRAME_DEMAND` onto fn's frame after install.
- Result: one frame, correctly flagged.

`partial(demand(fn))` is the more natural ordering for `define-demanding` use
cases (wrap the function, then bundle arguments). Both orderings are correct.

---

## 7. Non-goals

| Topic | Notes |
| ----- | ----- |
| `KosStaticMethod` | `staticmethod.__get__` returns the inner fn directly; no intermediate object in the CALL path. Low priority. |
| `KosClassMethod` | `classmethod.__get__` returns a `MethodType`; if that's a `KosBoundMethod`, it already has the fast path. Low priority. |
| `BoundMethod(Demand(fn))` flattening | Tracked as phase 3 in demand-evalex-phase2.md. Unrelated to KosPartial. |
| Deep nesting `partial(partial(fn))` | Each layer tail-calls through; correct automatically with recursive dispatch. |
| `partial` with keyword-only args | Handled by the kwargs merge; no special case needed. |

---

## 8. Tests

New class `TestKosPartialDispatch` in `tests/lib/test_partial.py`:

| Test | What it verifies |
| ---- | ---------------- |
| `test_kospartial_isinstance_partial` | `isinstance(KosPartial(fn, x), functools.partial)` is True |
| `test_kospartial_flat_positional_args` | `KosPartial(kos_fn, x)(y)` from koskari bytecode returns `kos_fn(x, y)` |
| `test_kospartial_kwargs_merge` | Bound kwargs and call-site kwargs are merged correctly |
| `test_kospartial_demand_inner_sets_flag` | `KosPartial(demand(fn), x)()` raises `KoskariSyncViolation` when fn does AWAIT |
| `test_kospartial_yielding_inner_fallback` | `KosPartial(yielding_fn)(...)` returns `DemandGenerator` (fallback path) |
| `test_kospartial_python_inner_fallback` | `KosPartial(lambda x: x, 42)()` returns 42 via Python call fallback |
| `test_kospartial_empty_bound_args_no_alloc` | Smoke test: `KosPartial(fn)` with no bound args avoids tuple concat |

---

## 9. Implementation checklist

1. [ ] Implement `koskari_partial_init`: import `functools.partial`, set as
       `tp_base`, call `PyType_Ready(KOS_CALLABLE_PYTYPE(KosPartialCallableType))`.
2. [ ] Implement `kospartial_wants_frame` (§3): delegate via attr lookup to
       inner callable's wants-frame slot.
3. [ ] Implement `kospartial_kos_install` (§4): merge positional and keyword
       args (§5); tail-call through to inner with `tail_call=1`; fall back to
       Python call for non-KosCallable inners.
4. [ ] Define `KosPartialCallableType` with `.tp_kos_install = kospartial_kos_install`,
       `.tp_kos_wants_frame = 0`, `.tp_kos_wants_frame_fn = kospartial_wants_frame`.
       Retain `partial`'s `tp_call` (inherited via `tp_base`) for Python-side
       calls.
5. [ ] Wire `koskari_partial_init` into `lib.c` and declare in `lib.h`.
6. [ ] Add `TestKosPartialDispatch` tests (§8).
7. [ ] `tox -qe py -- tests/lib/test_partial.py` — all tests pass.
8. [ ] `make test` — full suite passes.
9. [ ] Update this doc's implementation status line.

---

## 10. File index

| File | Change |
| ---- | ------ |
| `koskari/lib/partial.h` | `KosPartialObject`; `extern KosCallableTypeObject KosPartialCallableType`; `KosPartial_CheckExact` macro; `koskari_partial_init` / `koskari_partial_clear` declarations |
| `koskari/lib/partial.c` | `kospartial_wants_frame`, `kospartial_kos_install`, `KosPartialCallableType`, `koskari_partial_init` |
| `koskari/lib/lib.c` | call `koskari_partial_init` |
| `koskari/lib/lib.h` | declare `koskari_partial_init` |
| `tests/lib/test_partial.py` | `TestKosPartialDispatch` |
| `design/interpreter/kospartial.md` | this file |
| `design/TODO.md` | this entry |

# The end.
