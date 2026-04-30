# Koskari Object Model – First Cut Design

This document defines the initial implementation of Koskari runtime types
(KosType) and instances (KosObject) with interoperability for Python's
sync expectations. Class creation uses PUSH_COLLECT / POP_COLLECT / BUILD_CLASS opcodes;
sibilant-ceval achieves equivalent semantics via collect-let and metaclass
call (see §2.4).

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

### 2.1 PUSH_COLLECT / POP_COLLECT / BUILD_CLASS

Koskari uses PUSH_COLLECT, POP_COLLECT, and BUILD_CLASS. The class body runs
inside a CollectorNamespace; the class value is produced by BUILD_CLASS
(which calls the metaclass), not by the body's return value.

**PUSH_COLLECT** (CONST arg: constant-pool index of NamespaceSpec; does not match PUSH_NS, which is EXTRA with NSPEC and names in the extra row)

1. Read NSPEC from the constant pool via the instruction operand.
2. Create CollectorNamespace from NSPEC, install as frame ns.
3. Push KOS_DYN_COLLECT node (saves prior ns for restore).

**POP_COLLECT**

1. Verify dyn_top is KOS_DYN_COLLECT.
2. Restore frame ns from saved_ns, pop the node.
3. **Push the collected namespace onto the value stack.**

**BUILD_CLASS**

1. Pop ns (TOS), pop pos (TOS-1). pos = (name, bases) or (name, bases, metaclass).
2. Parse pos; optional metaclass may be omitted (derive from first base or default
   KosType inside `kos_build_class`).
3. Call `kos_build_class(name, bases, ns, metaclass_or_null)` (materializes
   namespace, `__prepare__`, merge, composite metaclass when needed, final
   metaclass call).
4. Push cls onto the stack.

`koskari.lib.build_class(name, bases, namespace, metaclass=None)` is the same
`kos_build_class(...)` surface for tests and embedding. After a successful
metaclass call, `kos_build_class` assigns the new type to the **input**
`namespace` under `__class__` (the same object `BUILD_CLASS` passes in). The
`__class__` key is stripped from the merged mapping before that mapping is
copied into `__prepare__`’s namespace so it is not passed to `type.__new__` as a
class body entry.

### 2.2 Stack Layout for Class Creation

The compiler builds pos on the stack before PUSH_COLLECT; pos stays below
the collect scope. After POP_COLLECT, stack is [..., pos, ns]. BUILD_CLASS
consumes both and pushes the class.

* **pos** = tuple (name, bases) or (name, bases, metaclass), built via BUILD_ARGS.
* **ns** = CollectorNamespace pushed by POP_COLLECT.

### 2.3 Dynamic Extent (KOS_DYN_COLLECT)

The collect block's chain node (KOS_DYN_COLLECT) holds:

* saved_ns (enclosing scope to restore on pop)

CollectorNamespace is a normal Namespace subtype; it is safe to push and
pop on the frame like any other namespace. PUSH_COLLECT creates and
installs it; POP_COLLECT restores the prior namespace, pops the node,
and pushes the collector onto the value stack.

### 2.4 Mapping from sibilant-ceval

sibilant-ceval achieves equivalent semantics:

* **collect-let** special form: Emits PUSH_COLLECT, installs
  CollectorNamespace. Blocks closure capture so class members do not close
  over each other.

* **load-locals + dict**: Emits OP_LOCALS to push the namespace, then
  dict(namespace) materializes to a plain dict.

* **letrec + metaclass call**: Class form compiles to letrec binding
  __class__ to (metaclass name bases (collect-let [] ... (dict
  (load-locals)))), then returns __class__.

Koskari uses PUSH_COLLECT / POP_COLLECT / BUILD_CLASS. The metaclass for
Koskari-defined classes is KosType by default (or a subclass).

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

### 9.1 Koskari Class Compiler Output

The class special form compiles to:

* BUILD_ARGS to construct pos (name, bases) or (name, bases, metaclass).
* PUSH_COLLECT (COUNT-style, NSPEC only for empty class).
* Class body instructions (define, defmethod, etc.).
* POP_COLLECT (pushes ns onto stack).
* BUILD_CLASS (consumes pos and ns, calls metaclass, pushes class).
* VAR_STORE to bind the class to the name.

BUILD_CLASS handles dict materialization and metaclass invocation.

### 9.2 sibilant-ceval Class Special Form Reference

From `sibceval/specials.py` (equivalent semantics, different encoding):

* `_class_parents(*parents, metaclass=None)` – keyword extraction for
  metaclass and bases.
* `special_class` – expands to letrec with `(metaclass name (#tuple . bases)
  (collect-let [] ,@members (dict (load-locals))))`.
* `child_collect_namespace` – uses OP_PUSH_COLLECT with CollectorNamespace
  (Koskari mirrors this with ByteCodeFragment.with_collector_namespace).
* `special_collect_let` – compile-time special; `special_load_locals` emits
  OP_LOCALS.

### 9.3 Koskari Infrastructure to Add

* KosType – metaclass implementing tp_call (awaitable), KosCallable, and
  C.new classmethod.
* KosObject – base for Koskari instances (or standard PyObject of heap type).
* KosFunction – function type implementing __get__ for descriptor binding.
* KosBoundMethod – bound method wrapper implementing KosCallable.
* KOS_DYN_COLLECT – chain node storing saved_ns for restore on POP_COLLECT.
* PUSH_COLLECT / POP_COLLECT / BUILD_CLASS opcode handlers (implemented).
* class special form in koskari compile/specials.

### 9.4 Namespace → dict for Class Bodies

Koskari's Namespace (and CollectorNamespace) must support conversion to
plain dict for metaclass invocation. sibilant-ceval does this via
`tp_as_mapping` (mp_subscript, mp_length) so `dict(namespace)` works.
Koskari should provide the same mapping protocol on its Namespace types.

---

# The end.
