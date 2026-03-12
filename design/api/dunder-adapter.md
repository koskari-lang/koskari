# Koskari Dunder Adapter Design

This document unifies the design for bridging Koskari's async-capable evaluator with Python's synchronous object protocol (dunder methods, type slots, iteration).

**Implementation status**: Core design implemented. Host path (demand_sync, slot
wiring, KoskariSyncViolation) and `__init__` as demand dunder are done.
Implemented dunder slots: `__add__` (nb_add), `__repr__` (tp_repr), `__str__`
(tp_str), `tp_richcompare` (__eq__, __lt__, __le__, __gt__, __ge__, __ne__),
`__len__` (sq_length), `__bool__` (nb_bool), `__iter__` (tp_iter). For
yielding `__iter__`, DemandSyncEx returns DemandSyncGenerator (sync iterator).
Deferred: demand_next, remaining slot coverage, tp_setattro refresh, __radd__
fallback, OBJECT_* push_or_await.

---

## 1. Problem

KosType instances expose dunder methods (`__add__`, `__eq__`, etc.) that are implemented as Koskari functions. Because Koskari functions are async by default, these methods return coroutines when called. Two integration surfaces need adaptation:

1. **Internal Koskari execution**: `OBJECT_*` opcodes (e.g. `OBJECT_ADD`, `OBJECT_EQ`) call Python C APIs (`PyNumber_Add`, `PyObject_RichCompareBool`). Those APIs invoke dunder methods via slot lookup. If a dunder returns a coroutine, the opcode currently pushes it as a raw value rather than driving it to completion.

2. **Python host interop**: Python's synchronous protocols (binary ops, rich compare, iteration) expect synchronous results. Calling `obj + 1` or `obj == x` on a KosType instance yields a coroutine, which Python does not await. The user sees a coroutine object instead of a value.

The solution distinguishes these two paths and provides appropriate adapters for each.

---

## 2. Core Principle: No Suspension in Sync Context

For the Python host bridge, the requirement is:

> Python synchronous APIs must never cause the Koskari evaluator to **suspend**.

Koskari methods may internally use awaitable operations, but execution must complete without crossing a suspension boundary. If suspension would occur, the adapter raises an error rather than running an event loop or blocking.

The relevant distinction is **not** whether a function is declared async, but whether execution **requires suspension** to produce a result.

---

## 3. Two Execution Paths

### 3.1 Internal path (Koskari evaluator)

When Koskari bytecode executes an object opcode (e.g. `compile/+` compiles to `OBJECT_ADD`), evaluation happens inside the Koskari evaluator loop. Flattened async is already supported: the evaluator can suspend on awaitables and resume via `push_or_await`.

**Change required**: Object opcodes must treat their results as potentially awaitable. Replace `push_result` with `push_or_await` at all sites where the result can be a coroutine:

- `obj_op_reducing` (used by `OBJECT_ADD`, `OBJECT_MULTIPLY`, etc.)
- `op_OBJECT_EQ_impl` (used by `OBJECT_EQ`, `OBJECT_NE`, `OBJECT_LT`, etc.)
- Any other `OBJECT_*` opcode whose underlying Python API can invoke KosType dunders

If a dunder returns a coroutine, `push_or_await` drives it (eager-step or suspend), and the opcode semantics remain correct. No new primitives are needed for the internal path.

### 3.2 Host path (Python slot calls)

When Python invokes a slot (e.g. `nb_add` for `a + b`) on a KosType instance, Python expects an immediate return value. The call happens outside the Koskari evaluator loop. We must:

1. Look up the Koskari dunder method (e.g. `__add__`).
2. Invoke it with the evaluator.
3. Drive execution until the first boundary event.
4. Interpret the event: if it is `RETURN` or `RAISE`, translate to a Python result; if it is `AWAIT` or `YIELD`, raise a sync violation.

This is implemented by two native evaluator helpers:

- **`demand_sync`** – synchronous call adapter for slot/dunder invocations
- **`demand_next`** – synchronous iterator step adapter for sync iteration

---

## 4. Native Adapters: `demand_sync` and `demand_next`

Both adapters operate by **greedily driving the Koskari evaluator until the first boundary event**. They create a **temporary evaluator context** (sub-context) that is:

- Not flattened into the caller's Koskari call chain
- Discarded after the boundary event
- Isolated from continuation capture, transfer plans, and resumable state

This keeps the sync bridge as a host boundary.

### 4.1 `demand_sync`

Used for **Python synchronous dunder / slot calls**.

**Input**: callable (bound dunder method or equivalent), args, kwargs.

**Mechanism**:

1. Create a temporary evaluator context.
2. Install a frame that invokes the callable with the given args.
3. Drive `koskari_eval` until the first boundary event.

**Allowed events**:

| Event   | Result                     |
| ------- | -------------------------- |
| RETURN  | return payload to Python   |
| RAISE   | propagate exception        |

**Forbidden events**:

| Event  | Result                                  |
| ------ | --------------------------------------- |
| AWAIT  | discard context; raise KoskariSyncViolation |
| YIELD  | discard context; raise KoskariSyncViolation |

**Example error message**:

```
async suspension is not allowed in Python __repr__ for Koskari type Foo
```

### 4.2 `demand_next`

Used for **synchronous iteration** over Koskari generators or iterables (e.g. implementing a `SyncIterator` wrapper).

**Input**: iterator or generator object (or equivalent resumable state).

**Mechanism**: same sub-context model; drive until boundary.

**Allowed events**:

| Event   | Result                                |
| ------- | ------------------------------------- |
| YIELD   | return yielded value                  |
| RETURN  | translate to StopIteration with value |
| RAISE   | propagate exception                   |

**Forbidden events**:

| Event  | Result                                  |
| ------ | --------------------------------------- |
| AWAIT  | discard context; raise KoskariSyncViolation |

---

## 5. Slot Wiring (Metaclass Integration)

KosType instances are Python heap types. Python's slot machinery (nb_add, tp_richcompare, sq_contains, etc.) is used when the type does not provide C-level slots; Python falls back to dunder lookup and `PyObject_Call`.

To intercept at the slot level, KosType (or a base) must provide **custom C slots** that:

1. Look up the dunder from the class (e.g. `__add__`, `__eq__`).
2. Build the call (receiver + operands).
3. Invoke `demand_sync` with that call.
4. Return the result or propagate the exception.

Installation happens in `kostype_tp_new` (or equivalent) when a KosType class is created. Slots are filled on the new type so that Python's `nb_add`, `tp_richcompare`, etc. dispatch to our adapter instead of raw dunder lookup.

### 5.1 Slot Coverage

**Implemented**: `__add__` (nb_add), `__repr__` (tp_repr), `__str__` (tp_str),
tp_richcompare (__eq__, __lt__, __le__, __gt__, __ge__, __ne__), __len__
(sq_length), __bool__ (nb_bool), __iter__ (tp_iter). **Deferred**: remaining
slots below.

| Category    | Slots                                  | Dunder mapping (non-exhaustive)      | Status   |
| ----------- | -------------------------------------- | ------------------------------------ | -------- |
| Number      | nb_add, nb_subtract, nb_multiply, …    | __add__, __sub__, __mul__, …         | nb_add ✓ |
| Number      | nb_bool                                | __bool__                             | ✓        |
| Reflected   | (via same slots, swap operands)        | __radd__, __rsub__, …                | deferred |
| Rich compare| tp_richcompare                         | __eq__, __lt__, __le__, __gt__, __ge__, __ne__ | ✓ |
| Sequence    | sq_length, sq_item, sq_contains        | __len__, __getitem__, __contains__   | sq_length ✓ |
| Mapping     | mp_subscript, mp_length                | __getitem__, __len__                 | deferred |
| Other       | tp_repr, tp_str, tp_hash               | __repr__, __str__, __hash__          | tp_repr ✓, tp_str ✓ |
| Iteration   | tp_iter, tp_iternext                   | __iter__, __next__ (via demand_next) | tp_iter ✓ |

Slot implementations follow a uniform pattern: lookup dunder, build call, demand_sync, return.

### 5.2 Reflected operations

For `a + b` when `a` does not support `__add__` but `b` supports `__radd__`, Python's binary-op logic already tries the reflected slot. Our nb_add (or equivalent) must implement the same fallback: try left.__add__(right); if NotImplemented, try right.__radd__(left). Each attempt uses demand_sync.

### 5.3 Slot dispatcher methodology

The goal is to mirror CPython's special-method-to-slot wiring for Koskari heap types so Koskari-defined dunders participate in Python builtin protocols, with actual invocation routed through `demand_sync`.

#### Class creation path

```text
KosType.tp_new (kostype_tp_new)
  → allocate / construct subclass type object
  → finalize dict / bases / MRO
  → PyType_Ready (or equivalent)
  → koskari_fixup_slot_dispatchers(type)
  → return type
```

#### Runtime path

```text
PyNumber_Add / PyObject_RichCompareBool / iteration helpers / etc.
  → slot on the Koskari-backed type
  → shared Koskari shim (slot implementation)
  → demand_sync(method, ...)
  → result or KoskariSyncViolation if suspension is not allowed
```

#### CPython reference (slot refresh)

For Python-defined classes, CPython uses:

```text
type.__new__
  → type_new()
    → fixup_slot_dispatchers()
      → update_one_slot()
```

Key internal names to mirror or parallel:

| CPython name             | Role |
| ------------------------ | ---- |
| `type_new`               | Class allocation and construction entry |
| `fixup_slot_dispatchers` | Bulk pass over the slot-definition table; inspects class dict for dunder presence and updates corresponding C slots |
| `update_one_slot`        | Core helper: inspects one special-method (e.g. `__add__`), updates the corresponding slot (e.g. nb_add) |
| `type_setattro`          | Slot refresh after class attribute mutation (e.g. adding `__add__` dynamically) |
| `PyType_Ready`           | Distinct role: type initialization, MRO, inheritance; runs before slot fixup |

`update_one_slot` is the atomic unit: given a (dunder_name, slot_ptr) pair, it checks whether the type or its MRO defines the dunder and, if so, installs the Koskari shim for that slot. `koskari_fixup_slot_dispatchers` iterates over a table of such pairs and calls an equivalent of `update_one_slot` for each.

For dynamic attribute changes (e.g. `Foo.__add__ = fn`), `type_setattro` or a KosType-specific `tp_setattro` must trigger a slot refresh so new dunders are picked up. The same fixup pass can be invoked on attribute set when the attribute name is a known dunder.

---

## 6. Exception: KoskariSyncViolation

A new exception type, `KoskariSyncViolation`, is raised when a sync adapter receives an AWAIT (or YIELD, for demand_sync) event. It indicates that the invoked code attempted to suspend, which is not allowed in the sync bridge.

**Subclass of**: `TypeError` (or a dedicated `KoskariError` base if one exists).

**Use cases**:

- demand_sync receives AWAIT or YIELD
- demand_next receives AWAIT

The adapter discards the temporary evaluator context before raising.

---

## 7. Relationship to Existing Infrastructure

| Component            | Role                                                |
| -------------------- | --------------------------------------------------- |
| `push_or_await`      | Internal path: drives awaitables in OBJECT_* opcodes |
| `demand_sync`        | Host path: drives dunder calls without suspension    |
| `demand_next`        | Host path: drives sync iteration without suspension  |
| `Koskari_RunSync`    | **Not** used for dunders. Runs coroutines via event loop; dunder adapter forbids suspension instead. |
| `__init__` (KosType) | demand dunder: kostype_tp_call uses Koskari_DemandSyncEx(__init__, ...). C() returns instance directly; C.new removed. |

---

## 8. Advantages of This Design

- **Correct semantics**: Python sync APIs never implicitly block or run an event loop.
- **No hidden event loops**: Sync context means “must not suspend”; failure is explicit.
- **Minimal runtime complexity**: Adapters drive ceval until a boundary event; no coroutine wrappers for the host path.
- **Flexible Koskari code**: Methods may use async constructs internally if execution completes without suspension.
- **Clear failure mode**: Suspension attempt → `KoskariSyncViolation`.
- **Unified handling**: Both internal (push_or_await) and host (demand_sync/demand_next) paths share the same understanding of boundary events.

---

## 9. Summary

| Path          | Mechanism             | Allowed                 | Forbidden        |
| ------------- | --------------------- | ----------------------- | ---------------- |
| Internal eval | push_or_await         | RETURN, RAISE, YIELD    | (none; may suspend) |
| Host dunder   | demand_sync           | RETURN, RAISE           | AWAIT, YIELD     |
| Host iter     | demand_next           | YIELD, RETURN, RAISE    | AWAIT            |

Implementation steps:

1. Add `demand_sync` and `demand_next` to the C evaluator. — **demand_sync done** (Koskari_DemandSyncEx); demand_next deferred.
2. Add `KoskariSyncViolation` exception type. — **done** (cached in object.c).
3. Implement shared Koskari shims (one per slot category) that look up dunder, build call, invoke demand_sync. — **done** (dunder_slot_unary, dunder_slot_binary, dunder_slot_richcompare, dunder_slot_sq_length, dunder_slot_nb_bool_shim, dunder_slot_iter_shim; nb_add, tp_repr, tp_str, tp_richcompare, sq_length, nb_bool, tp_iter).
4. Implement `koskari_fixup_slot_dispatchers` and per-slot `update_one_slot` equivalent; maintain slot-definition table (dunder name → slot). — **done** (koskari_update_one_slot, koskari_fixup_slot_dispatchers).
5. Call `koskari_fixup_slot_dispatchers(type)` from `kostype_tp_new` after PyType_Ready. — **done**.
6. Wire KosType `tp_setattro` (or equivalent) to trigger slot refresh when a dunder attribute is set. — **deferred**.
7. Change OBJECT_* opcodes to use `push_or_await` instead of `push_result` where results may be awaitable. — **deferred**; current slot shims return concrete values via demand_sync.

Additionally: `__init__` is a demand dunder. `kostype_tp_call` uses alloc + Koskari_DemandSyncEx(__init__, ...); C() returns the instance directly. KosType no longer extends KosCallable; (Foo 1 2) and C(...) both use the default PyObject_Call path.
