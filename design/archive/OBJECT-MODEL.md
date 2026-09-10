# Koskari Object Model – First Cut Design

**Status**: archived 2026-09-10, superseded. Describes a KosType metaclass with kostype_tp_call and composite metaclass bases that no longer exists; object.c now demand-wraps dunders at class build and hands the namespace to the ordinary winning metaclass.

This document defines the initial implementation of Koskari runtime types
(KosType) and instances (KosObject) with interoperability for Python's
sync expectations. Class creation uses PUSH_COLLECT / POP_COLLECT / BUILD_CLASS opcodes;
sibilant-ceval achieves equivalent semantics via collect-let and metaclass
call (see §2.4).

---

## 1. Scope

* Synchronous construction via `kostype_tp_call` (`tp_new` + demand `__init__`).
* KosCallable integration for functions and bound methods (not KosType).
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

KosType is **not** a KosCallable. Class construction from Python or Koskari
uses `PyObject_Call` on the class object, which dispatches to the metaclass
`tp_call` (`kostype_tp_call`; see §4.2 and
[dunder-adapter.md](dunder-adapter.md) §5.4).

KosFunction and KosBoundMethod remain KosCallables for ordinary invocation.
When Koskari bytecode calls a **function**, dispatch uses KosCallable as today.
Class calls are a separate host path through `kostype_tp_call`.

---

## 4. Construction Semantics

### 4.1 Implemented constructor (`kostype_tp_call`)

Python `C(...)` and Koskari `(C ...)` both reach the metaclass `tp_call`.
For Koskari-defined classes that is **`kostype_tp_call`** (pure `KosType` or
composite `KosCombinedMeta_*` inheriting `KosType`).

Steps (see [dunder-adapter.md](dunder-adapter.md) §5.4 for detail):

1. **`tp_new(cls, args, kwargs)`** — real allocation (`object.__new__`, Pydantic
   model setup, enum machinery, etc.).
2. If `__new__` returned a non-instance of `cls`, return it (CPython rule).
3. **`PyType_Lookup(cls, "__init__")`** — MRO resolution; skip bare
   `object.__init__`.
4. **`Koskari_DemandSyncEx(init, (instance, *args), kwargs)`** — demand path
   for Koskari `__init__`; plain Python call for foreign `__init__`.
5. Return **`instance`** synchronously (not a coroutine or awaitable).

### 4.2 Composite metaclass and foreign bases

When a Koskari class subclasses a foreign base (plain Python, `ABC`, `IntEnum`,
`pydantic.BaseModel`, etc.), `kos_build_class` may install a composite
metaclass (`KosCombinedMeta_<foreign>`). Instance construction still uses
`kostype_tp_call` as long as `KosType` appears in the metaclass MRO.

This requires **`tp_new`** (foreign `__new__` must run) and **MRO `__init__`**
(inherited foreign or Kos inits must be found). Full-stack reproducers live under
`tests/reproducers/scripts/class_hierarchy.ki` and
`tests/reproducers/scripts/pydantic_model.ki`.

### 4.3 Deferred: awaitable `tp_call`

An earlier design called for `KosType.tp_call` to return an **awaitable**
construction result so Python could `await SomeType(...)`. That is **not**
implemented. Sync construction via `C(...)` is the supported Python entrypoint
today.

### 4.4 Deferred: `C.new` classmethod

A synchronous `C.new(*args, **kwargs)` adapter was sketched as an explicit
escape hatch. It is **not** implemented; `C(...)` already constructs
synchronously on the demand path.

### 4.5 Koskari-side construction

When Koskari code calls a class:

```
(C ...)
```

* Dispatch is `PyObject_Call` on the class → `kostype_tp_call` as above.
* Koskari `__init__` runs via `Koskari_DemandSyncEx` (no suspension).
* If `__init__` were written to suspend, construction raises
  `KoskariSyncViolation` on the Python/demand path.

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

* Fields: `func` (`KosFunction` or `Demand`), `self` (bound instance).
* Implements KosCallable by prepending `self` to call arguments.
* When `func` is `Demand`, `__call__` routes through `Koskari_DemandSync`
  (sync path); eval install resolves the inner `KosFunction` when present.

### 5.3 Demand as descriptor

`Demand` implements `tp_descr_get` for `define-demanding` and similar:

* Class access (`obj is None`) → return the unbound `Demand` wrapper.
* Instance access → **`KosBoundMethod(Demand(fn), obj)`** (not
  `Demand(BoundMethod(...))`).

See [demand-wrapper.md](demand-wrapper.md) §3.4.

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

* Python `C(...)` and Koskari `(C ...)` construct synchronously via
  `kostype_tp_call` (`tp_new` + demand `__init__`).
* Class dictionaries are plain Python dicts.
* Method binding is descriptor-based: `KosFunction.__get__` → `KosBoundMethod`;
  `Demand.__get__` → `KosBoundMethod(Demand, obj)`.
* No implicit event loop execution occurs in Python host paths.

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

* KosType – metaclass implementing synchronous `kostype_tp_call` (§4.1).
* KosObject – base for Koskari instances (or standard PyObject of heap type).
* KosFunction – function type implementing `__get__` for descriptor binding.
* KosBoundMethod – bound method wrapper; `func` may be `KosFunction` or
  `Demand`.
* Demand – sync adapter type with `tp_descr_get` (see demand-wrapper.md).
* KOS_DYN_COLLECT – chain node storing saved_ns for restore on POP_COLLECT.
* PUSH_COLLECT / POP_COLLECT / BUILD_CLASS opcode handlers (implemented).
* class special form in koskari compile/specials.

Deferred from the original sketch: awaitable `tp_call`, `C.new` classmethod.

### 9.4 Namespace → dict for Class Bodies

Koskari's Namespace (and CollectorNamespace) must support conversion to
plain dict for metaclass invocation. sibilant-ceval does this via
`tp_as_mapping` (mp_subscript, mp_length) so `dict(namespace)` works.
Koskari should provide the same mapping protocol on its Namespace types.

---

# The end.
