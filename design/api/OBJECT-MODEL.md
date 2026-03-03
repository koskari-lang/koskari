# Koskari Object Model – First Cut Design

This document defines the initial implementation of Koskari runtime types
(KosType) and instances (KosObject) with interoperability for Python's
sync expectations. Class creation uses PUSH_CLASS / POP_CLASS opcodes;
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

### 2.1 PUSH_CLASS / POP_CLASS

Koskari uses explicit PUSH_CLASS and POP_CLASS opcodes. The class body runs
inside a CollectorNamespace; the class value is produced by the metaclass
invocation at POP_CLASS, not by the body's return value.

**PUSH_CLASS**

1. Pop and parse arguments from the stack (same format as CALL_STAR).
2. Parse pos tuple via PyArg_ParseTuple with format "OO|O" → (name, bases,
   [metaclass]). If metaclass is absent, use default KosType.
3. Allocate KOS_DYN_CLASS node with metaclass, name, bases, kwargs, and
   class_binding_ns (parent scope with __class__ slot for super).
4. Install the node. The install handler:
   * Pushes a new stack block for the class body.
   * Creates a CollectorNamespace (parent = class_binding_ns).
   * Installs it as the frame's current namespace.
5. Body runs; definitions populate the collector via VAR_BIND / VAR_STORE.

**POP_CLASS**

1. Current frame namespace is the collector.
2. Materialize to plain Python dict (Namespace implements mapping protocol).
3. Call cls = metaclass(name, bases, cls_dict, **kwargs).
4. Store cls in class_binding_ns.__class__ (for super lookups in methods).
5. Pop the node. The uninstall handler:
   * Restores frame namespace to saved_ns.
   * Pops the class-body stack block.
6. Push cls onto the enclosing stack (TOS after block pop).

Ref semantics inside the class body do not persist into the class dict in
this phase.

### 2.2 PUSH_CLASS Argument Format (CALL_STAR-style)

PUSH_CLASS uses the same stack layout as CALL_STAR for consistency and
compiler reuse:

* **arg** = bitflags: 1 = has pos, 2 = has kwd (same as CALL_STAR).
* **Stack (opa = 3):** [pos, kwd] with pos at TOS-1, kwd at TOS.
* **pos** = tuple (name, bases) or (name, bases, metaclass).
* **kwd** = dict of extra keyword args for metaclass (e.g. metaclass=MyMeta).

Parse with PyArg_ParseTuple(pos, "OO|O", &name, &bases, &metaclass). If
metaclass is NULL, also check kwd["metaclass"]. If still absent, use
default KosType. Before calling the metaclass, remove "metaclass" from
kwargs (it is consumed for selection, not passed through). The compiler
can reuse BUILD_ARGS,
BUILD_KWDS, and COMBINE_ARGS to construct pos and kwd.

### 2.3 Dynamic Extent (KOS_DYN_CLASS)

The class block's chain node holds:

* metaclass (PyObject *)
* name (PyObject *, str)
* bases (PyObject *, tuple)
* kwargs (PyObject *, dict or NULL)
* class_binding_ns (parent of collector; has __class__ slot for super)
* saved_ns (enclosing scope to restore on pop)

CollectorNamespace is a normal Namespace subtype; it is safe to push and
pop on the frame like any other namespace. The install handler creates and
installs it; the uninstall handler restores the prior namespace and pops
the stack block. No special frame logic is required for the collector itself.

### 2.4 Mapping from sibilant-ceval

sibilant-ceval achieves the same effect without PUSH_CLASS / POP_CLASS:

* **collect-let** special form: Emits PUSH_COLLECT, installs
  CollectorNamespace. Blocks closure capture so class members do not close
  over each other.

* **load-locals + dict**: Emits OP_LOCALS to push the namespace, then
  dict(namespace) materializes to a plain dict.

* **letrec + metaclass call**: Class form compiles to letrec binding
  __class__ to (metaclass name bases (collect-let [] ... (dict
  (load-locals)))), then returns __class__.

Koskari consolidates this into PUSH_CLASS / POP_CLASS. The metaclass for
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

* BUILD_ARGS (and optionally BUILD_KWDS / COMBINE_ARGS) to construct pos and
  kwd on the stack.
* PUSH_CLASS (STAR-style arg for pos/kwd presence).
* Class body instructions (define, defmethod, etc.).
* POP_CLASS.

No letrec, load-locals, or dict call; POP_CLASS handles dict materialization
and metaclass invocation internally.

### 9.2 sibilant-ceval Class Special Form Reference

From `sibceval/specials.py` (equivalent semantics, different encoding):

* `_class_parents(*parents, metaclass=None)` – keyword extraction for
  metaclass and bases.
* `special_class` – expands to letrec with `(metaclass name (#tuple . bases)
  (collect-let [] ,@members (dict (load-locals))))`.
* `child_collect_namespace` – uses OP_PUSH_COLLECT with CollectorNamespace.
* `special_collect_let` – compile-time special; `special_load_locals` emits
  OP_LOCALS.

### 9.3 Koskari Infrastructure to Add

* KosType – metaclass implementing tp_call (awaitable), KosCallable, and
  C.new classmethod.
* KosObject – base for Koskari instances (or standard PyObject of heap type).
* KosFunction – function type implementing __get__ for descriptor binding.
* KosBoundMethod – bound method wrapper implementing KosCallable.
* KOS_DYN_CLASS – chain node with install/uninstall handlers; stores
  metaclass, name, bases, kwargs, class_binding_ns, saved_ns.
* PUSH_CLASS / POP_CLASS opcode handlers (implemented).
* class special form in koskari compile/specials.

### 9.4 Namespace → dict for Class Bodies

Koskari's Namespace (and CollectorNamespace) must support conversion to
plain dict for metaclass invocation. sibilant-ceval does this via
`tp_as_mapping` (mp_subscript, mp_length) so `dict(namespace)` works.
Koskari should provide the same mapping protocol on its Namespace types.

---

# The end.
