# KosProperty: KosCallable-aware property descriptors

This document specifies **KosProperty**, a C-level subtype of CPython's
`property` that automatically stores **`Demand(...)`** wrappers for Koskari
accessors. Authors use ordinary `@property` / `.setter` / `.deleter` syntax on
Koskari functions without a separate `@demand` or **`define-demanding`** step.

**Implementation status:** Implemented and archived (2026-06-27). See `koskari/lib/property.c`; bootstrap swap in `koskari/bootstrap/__init__.py`; user docs at `docs/patterns/recipes/property-accessors.md`.

**Audience:** maintainers implementing or reviewing this change.

**Related:**

- [kospartial.md](kospartial.md) — same stdlib-subtype family (`functools.partial`
  + `KosCallable` install slots)
- [../api/demand-wrapper.md](../api/demand-wrapper.md) — `Demand` type,
  descriptor binding, bootstrap bindings
- [../archive/demand-evalex-phase2.md](../archive/demand-evalex-phase2.md) —
  `Demand` as `KosCallable` (install-slot flattening)
- [../api/special-call-protocol.md](../api/special-call-protocol.md) —
  `KosCallable` protocol
- [../api/OBJECT-MODEL.md](../api/OBJECT-MODEL.md) §5.3 — `Demand` as descriptor

---

## 1. Motivation

Bootstrap today binds the stock CPython descriptor:

```python
_ty('property', property)
```

(`koskari/bootstrap/__init__.py`)

When a Koskari class defines accessors with `@property`, the getter and setter
callables stored on `fget` / `fset` are usually bare **`KosFunction`** objects.
Property's `__get__` / `__set__` invoke those callables directly. Unless the
author wrapped them manually (**`(demand (function …))`**, **`define-demanding`**,
or **`@demand`** in Python-hosted class bodies), attribute access from the
demand path does not get the same synchronous, no-suspend behavior that
**`Demand`**-backed methods provide.

Today the workaround is redundant ceremony:

```koskari
@demand
define getter [self]: ...

;; or
(define-demanding getter [self] ...)

;; then wire into a class with @property or (property getter ...)
```

**KosProperty** removes that step: any **`KosCallable`** registered as
`fget` / `fset` / `fdel` is stored as **`Demand(callable)`** automatically.
Plain Python callables are unchanged.

---

## 2. Wrap rule

At every point a callable is assigned to the descriptor, run:

```text
if KosCallable_Check(fn):
    store Demand(fn)
else:
    store fn
```

**Predicate:** **`KosCallable_Check`** only (see `koskari/lib/callable.h`). No
per-type allowlist.

**Idempotence:** **`Demand(fn)`** is sufficient; do not special-case existing
**`Demand`** wrappers. **`Demand.__new__`** returns the same object when the
argument is already a **`Demand`** (`demand.c`). **`Demand(Demand(x))`** is
therefore a no-op aside from reference counting.

**Non-KosCallable:** Python functions, lambdas, and other plain callables pass
through unchanged — same as today's **`property`** behavior.

---

## 3. Registration paths (all must apply the wrap rule)

| Path | When |
| ---- | ---- |
| Constructor | `property(fget, fset=..., fdel=..., doc=...)` |
| `@property` decorator | `property.__init__(getter)` |
| `.setter` | returns new descriptor with wrapped setter |
| `.deleter` | returns new descriptor with wrapped deleter |
| `getter` / `setter` / `deleter` properties (if exposed) | assignment to `fget` / `fset` / `fdel` |

Every path that copies or replaces `fget` / `fset` / `fdel` must funnel through
one shared C helper (e.g. **`kos_demand_wrap_accessor`**) before calling the
base **`property`** implementation.

---

## 4. `isinstance` compatibility

Same approach as [kospartial.md](kospartial.md) §2: during module init, import
CPython's **`property`** type and set it as **`KosPropertyType.tp_base`** before
**`PyType_Ready`**. Python code using **`isinstance(obj, property)`** continues
to work.

**Accessing `fget` / `fset` / `fdel`:** use **`PyObject_GetAttrString`** (or
cached member descriptors at init time). Stored values are **`Demand(...)`** for
Koskari accessors; introspection and tests should expect wrappers, with
**`__wrapped__`** pointing at the inner callable.

---

## 5. Runtime behavior after wrap

**Getter (instance access):**

1. CPython **`property.__get__`** runs (inherited or delegated).
2. Invokes stored **`fget`**, which is **`Demand(kos_fn)`** for Koskari functions.
3. **`Demand.__call__`** / bound-method path routes through **`Koskari_DemandSync`**
   and, when the eval loop is active, through **`demand_kos_install`** for
   flattening (see [demand-wrapper.md](../api/demand-wrapper.md)).

This matches the manual pattern **`property((demand getter))`** without author
boilerplate.

**Setter / deleter:** same wrap at registration time; **`__set__`** / **`__del__`**
invoke the stored **`Demand`** wrapper.

**Contrast with `Demand` as the class attribute:** **`define-demanding`** installs
**`Demand(fn)`** as the class dict entry (non-data descriptor). **`KosProperty`**
is a **data descriptor** — precedence and **`__get__`** / **`__set__`** semantics
stay standard; only the stored callables differ.

---

## 6. Bootstrap integration

On landing, **replace immediately**:

```python
_ty('property', property)   # before
_ty('property', KosProperty)  # after
```

No transitional alias. Ki **`property`**, **`@property`**, and layout/class
machinery that references **`property`** pick up **`KosProperty`** automatically.

Same policy as **`KosPartial`**: when implemented, replace any bootstrap binding
of **`functools.partial`** with **`KosPartial`** (if/when that name is exported).

---

## 7. Implementation sketch

### 7.1 C helper

```c
PyObject *kos_demand_wrap_accessor(PyObject *fn) {
  if (! fn) {
    Py_INCREF(fn);
    return fn;
  }
  if (KosCallable_Check(fn))
    return KosDemand_New(fn);  /* idempotent for existing Demand */
  Py_INCREF(fn);
  return fn;
}
```

Use existing **`KosDemand_New`** / constructor path; do not duplicate idempotence
logic.

### 7.2 Type object

- **`KosPropertyType`**: subtype of **`property`** via dynamic **`tp_base`**.
- Override **`tp_init`** (and any **`property`** hooks needed for **`.setter`** /
  **`.deleter`**) to wrap arguments before delegating to the base.
- If **`property`** implementation is pure-Python in some builds, prefer a thin
  Python subclass delegating wrap to C for the accessor slots only; the shipped
  tree targets the C **`property`** type — confirm against the runtime's
  **`property`** layout during implementation.

### 7.3 Module wiring

| File | Change |
| ---- | ------ |
| `koskari/lib/kosproperty.h` | type decl, **`kos_demand_wrap_accessor`**, init/clear |
| `koskari/lib/kosproperty.c` | **`KosPropertyType`**, init hook |
| `koskari/lib/lib.c` / `lib.h` | register type, export to Python |
| `koskari/bootstrap/__init__.py` | **`_ty('property', KosProperty)`** |

---

## 8. Tests

New tests under **`tests/lib/test_kosproperty.py`** (or extend an existing
descriptor test module):

| Test | Verifies |
| ---- | -------- |
| `isinstance(kos_prop, property)` | **`tp_base`** compatibility |
| getter wrap | **`KosFunction`** passed to **`property(fn)`** → **`fget`** is **`Demand`**, **`fget.__wrapped__` is fn** |
| setter wrap | **`.setter`** stores **`Demand`** for Koskari setter |
| idempotent | **`property(demand(fn))`** → same **`Demand`** object on **`fget`** |
| python passthrough | **`property(python_fn)`** → **`fget`** is **`python_fn`**, not **`Demand`** |
| demand path eval | class with **`@property`** Koskari getter; bytecode **`CALL_ATTR`** / attribute read returns correct value synchronously |
| setter demand path | property with Koskari setter; write then read round-trip |

Reuse patterns from **`tests/lib/test_demand_sync.py`** (**`TestDemandDescriptor`**) and **`tests/lib/opcode/test_call_attr.py`** (property + callable).

---

## 9. Non-goals

| Topic | Notes |
| ----- | ----- |
| Replacing **`define-demanding`** | Stays for non-property methods; **`KosProperty`** only covers **`property`** accessors |
| Auto-wrap on bare class methods | Use **`Demand`** / **`define-demanding`** or future descriptor types |
| **`KosStaticMethod` / `KosClassMethod`** | Separate items; see [kospartial.md](kospartial.md) §7 |
| Compiler elision of **`Demand`** | Optional future **`define-demanding`** / property peephole; not required for v1 |
| User docs in **`docs/`** | Add **`compile/property`** or macro note when bootstrap swap lands |

---

## 10. Implementation checklist

1. [ ] Implement **`kos_demand_wrap_accessor`** and **`KosPropertyType`** with
       dynamic **`tp_base = property`**.
2. [ ] Wrap on constructor, **`.setter`**, **`.deleter`** (all **`fget`/`fset`/`fdel`**
       assignment paths).
3. [ ] Export **`KosProperty`** from **`koskari.lib`**; wire **`koskari_*_init`**.
4. [ ] Replace **`_ty('property', property)`** in bootstrap.
5. [ ] Add tests (§8).
6. [ ] **`tox -qe py -- tests/lib/test_kosproperty.py`** — pass.
7. [ ] Update this doc's implementation status line.

# The end.
