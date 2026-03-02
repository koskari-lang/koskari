# Koskari Object Model – First Cut Design

This document defines the initial implementation of Koskari runtime types
(KosType) and instances (KosObject) with interoperability for Python's
sync expectations. It synthesizes the first-cut design with the sibilant-ceval
class creation approach that Koskari will mirror.

---

## 1. Scope

* Minimal deviation from CPython's PyObject and PyTypeObject semantics.
* Explicit async construction support.
* Native KosCallable integration for Koskari-side invocation.
* Descriptor-based method binding via __get__.

This phase deliberately does **not** preserve Ref semantics at the class
dictionary level. Class bodies materialize to normal Python dicts.

---

## 2. Class Creation Model

### 2.1 PUSH_CLASS / POP_CLASS (Design Target)

* PUSH_CLASS installs a temporary collector namespace for executing the
  class body.
* Definitions inside the block populate that namespace.
* POP_CLASS extracts bindings from the collector namespace, materializes
  them into a **plain Python dict**, and invokes the metaclass:

  ```python
  cls = metaclass(name, bases, dict, **kwargs)
  ```

* Ref semantics inside the class body do not persist structurally into
  the class dict in this phase.

### 2.2 Mapping from sibilant-ceval

sibilant-ceval uses equivalent machinery without explicit PUSH_CLASS /
POP_CLASS opcodes:

* **collect-let** special form: Creates a child namespace via
  `child_collect_namespace`, which emits `PUSH_COLLECT` and installs a
  `CollectorNamespace` as the current frame namespace. CollectorNamespace
  blocks closure capture (FindRef does not expose refs) so class members
  do not close over each other, matching Python's class member model.

* **load-locals**: Emits `OP_LOCALS`, which pushes the current frame
  namespace (the CollectorNamespace) onto the stack.

* **dict**: Python builtin. The Namespace (and CollectorNamespace) implements
  the mapping protocol (tp_as_mapping with mp_subscript); `dict(namespace)`
  produces a plain Python dict of name → value from the collector.

* **letrec + metaclass call**: The class special form compiles to:

  ```lisp
  (letrec [[__class__ (metaclass name (#tuple . bases)
                  (collect-let []
                    ,@members
                    (dict (load-locals))))]]
     __class__)
  ```

  So: execute members in a collector namespace, materialize to dict, pass
  to metaclass(name, bases, dict). Default metaclass is `type`.

* **Koskari mapping**: Koskari will use the same pattern—collect-let (or
  equivalent PUSH_COLLECT/CollectorNamespace) for the class body, then
  (dict (load-locals)) to produce the class dict, then invoke the metaclass.
  The metaclass for Koskari classes will be KosType (or a subclass).

---

## 3. KosType

### 3.1 Representation

* Each Koskari-defined class is a real CPython heap type.
* KosType acts as the metaclass (subclasses type).
* Instances are standard PyObject instances of that heap type.

### 3.2 KosCallable Integration

KosType implements the KosCallable interface. When invoked from Koskari code:

* Constructor dispatch uses KosCallable.
* Execution installs frames onto the **current KosContext**.
* No new context is created.
* Async behavior participates in normal Koskari call semantics.

Python invocation does **not** use KosCallable directly (see §4).

---

## 4. Construction Semantics

Construction is unified internally but exposed differently by caller context.

### 4.1 Internal Constructor Engine

Primitive:

```
kos_construct(cls, args, kwargs, mode)
```

Steps:

1. Allocate instance.
2. Call __init__.
3. Interpret result based on mode.

### 4.2 Python Constructor (tp_call) – Awaitable by Default

KosType.tp_call returns an **awaitable construction result**.

* Allocate instance.
* Call __init__.
* If __init__ returns:
  * None → return an awaitable that resolves immediately to the instance.
  * Awaitable / KosCoroutine → return an awaitable that awaits init
    completion, then yields the instance.

Python usage:

```python
inst = await SomeType(1, 2, 3)
```

Notes:

* This is a deliberate deviation from CPython's usual synchronous
  constructor contract, enabled because we control tp_call for Koskari types.
* The awaitable returned by tp_call is the primary async-friendly
  constructor entrypoint.

### 4.3 Synchronous Constructor Adapter (C.new)

Every KosType exposes:

```
C.new(*args, **kwargs) -> instance
```

* Implemented as a runtime-provided classmethod.
* Runs construction to completion synchronously.
* If __init__ is async, synchronizes via _sync.run(...).

Python usage:

```python
inst = SomeType.new(1, 2, 3)
```

### 4.4 Koskari-Side Construction

When Koskari code calls a class:

```
(C ...)
```

* Dispatch occurs through KosCallable.
* Async init participates in Koskari's async semantics.
* Execution installs frames onto the current KosContext; may suspend
  according to normal Koskari call rules.

No special form is required in Koskari.

---

## 5. Method Binding

### 5.1 KosFunction as Descriptor

KosFunction implements:

```
__get__(self, obj, owner)
```

Behavior:

* If accessed via class (obj is None) → return self.
* If accessed via instance → return KosBoundMethod(self, obj).

This allows storing KosFunction directly in the class dict.

### 5.2 KosBoundMethod

Minimal wrapper object:

* Fields: func (KosFunction), self (bound instance).
* Implements KosCallable by prepending self to call arguments.

Future optimization (optional): vectorcall-style fast path.

---

## 6. Descriptor Resolution Order

Class dicts use normal CPython resolution rules:

1. Data descriptors on type.
2. Instance __dict__.
3. Non-data descriptors on type.
4. AttributeError.

No Ref-based attribute storage is introduced in this phase.

---

## 7. Deferred Features (Not in First Cut)

* Ref-backed class dictionaries.
* Ref-backed instance attribute storage.
* Custom attribute resolution order.
* Multimethod integration at class-definition level.

These may be introduced later via alternate metaclass or flag-driven
class creation.

---

## 8. Invariants (First Cut)

* Python `C(...)` returns an awaitable; sync construction uses `C.new(...)`.
* Koskari `(C ...)` uses KosCallable semantics.
* Class dictionaries are plain Python dicts.
* Method binding is descriptor-based via KosFunction.__get__.
* No implicit event loop execution occurs in Python paths.

---

## 9. Implementation Notes

### 9.1 sibilant-ceval Class Special Form Reference

From `sibceval/specials.py`:

* `_class_parents(*parents, metaclass=None)` – keyword extraction for
  metaclass and bases.
* `special_class` – expands to letrec with `(metaclass name (#tuple . bases)
  (collect-let [] ,@members (dict (load-locals))))`.
* `child_collect_namespace` – uses OP_PUSH_COLLECT with CollectorNamespace.
* `special_collect_let` – compile-time special; `special_load_locals` emits
  OP_LOCALS.

### 9.2 Koskari Infrastructure to Add

* KosType – metaclass implementing tp_call (awaitable), KosCallable, and
  C.new classmethod.
* KosObject – base for Koskari instances (or standard PyObject of heap type).
* KosFunction – function type implementing __get__ for descriptor binding.
* KosBoundMethod – bound method wrapper implementing KosCallable.
* collect-let / PUSH_COLLECT equivalent if not already present.
* class special form in koskari compile/specials, mirroring sibilant-ceval.

### 9.3 Namespace → dict for Class Bodies

Koskari's Namespace (and CollectorNamespace) must support conversion to
plain dict for metaclass invocation. sibilant-ceval does this via
`tp_as_mapping` (mp_subscript, mp_length) so `dict(namespace)` works.
Koskari should provide the same mapping protocol on its Namespace types.

---

# The end.
