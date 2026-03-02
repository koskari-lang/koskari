# Namespace and Refs: Summary for AI Agents

This document summarizes the Koskari/Sibilant model of **Namespace** (lexical scope and bindings) and **Ref** (mutable symbolic references), including special forms and syntactic shortcuts.

---

## Namespace

### Overview

**Namespace** is the lexical scope container. It holds name-to-value bindings. Implemented in `koskari/lib/namespace.c` (C extension).

### Types

| Type | Description |
|------|-------------|
| **Namespace** | Base lexical scope. Has a parent, optional name, and a map of bindings. |
| **NamespaceSpec** | Layout descriptor: `ns_name`, `local_syms`, `closure_syms`. Describes what a namespace will hold before it is created. |
| **ModuleNamespace** | Module-level scope. Has a `builtins` dict for fallback lookup. Each .ki module gets one. |
| **CollectorNamespace** | Like Namespace but does not expose refs via `FindRef`—blocks closure capture. Used for class member collection. |
| **Module** | Object whose attributes are backed by lookups in a Namespace. Has `__dict__` = the underlying Namespace. |

### Key API (C)

- **KosNamespace_Deref(ns, key)** — Look up binding by name, return value (dereference the Ref). New ref or NULL.
- **KosNamespace_Store(ns, key, val)** — Store value into binding.
- **KosNamespace_Bind(ns, key, ref)** — Bind name to a Ref (used for VAR_BIND).
- **KosNamespace_Clear(ns, key)** — Clear the binding.
- **KosNamespace_Ref / KosNamespace_FindRef** — Look up the Ref for a name (for VAR_REF).

### Bindings

Each binding is a **KosNameBind**: `name`, `held` (value or reference), and `raw` (0/1). Values can be held directly (raw) or wrapped in a Ref.

---

## Ref (Base and Subclasses)

### Overview

**Ref** is a mutable symbolic reference. It has a `name` and holds (or computes) a value. Refs support `deref()`, `store(val)`, and `clear()`.

### Base Ref (KosRef / Ref / SymbolicRef)

- Fields: `name`, `obj` (the held value).
- `deref()` → value, or raises if empty.
- `store(val)` → set value.
- `clear()` → set to empty.

### Ref Subclasses

| Subclass | Purpose | Extra fields |
|----------|---------|--------------|
| **AttributeRef** | Reference to `obj.attr` | `obj`, `attr` |
| **ItemRef** | Reference to `obj[key]` | `obj`, `key` |
| **DynamicRef** | Custom get/set/delete | `getfn`, `setfn`, `delfn`, `emptyfn` |
| **ConstantRef** | Read-only ref, fixed value | (subclass of KosRef) |
| **MemoizedRef** | Lazy, computed once | `getfn`, cached value in `obj` |
| **ParamRef** | Function parameter binding | `key` (param name), `default` in `obj` |

### Ref Methods (Python)

- `ref.deref()` → value
- `ref.store(val)` → None
- `ref.clear()` → None
- `ref.empty` → bool (property)

### Builtin `deref(ref)`

Module-level `deref(ref)` calls `ref.deref()`. Used when passing refs as first-class values and dereferencing them in higher-order code.

---

## Special Forms: refq, bindq, deref

### compile/refq

- **Syntax:** `(compile/refq SYM)`
- **Effect:** Push a Ref for the variable named SYM.
- **Bytecode:** `VAR_REF` (name).
- **Use:** Obtain a mutable reference to a variable to pass around, store into, or dereference.

### compile/bindq

- **Syntax:** `(compile/bindq SYM EXPR)`
- **Effect:** Create a new binding SYM and initialize it with the value of EXPR. Form evaluates to None.
- **Bytecode:** Declare SYM, compile EXPR, `VAR_BIND`, then `CONST None`.
- **Use:** Introduce a new local/block binding.

### compile/deref

- **Syntax:** `(compile/deref EXPR)`
- **Effect:** Compile EXPR (must produce a Ref on the stack), then call `ref.deref()` and push the result.
- **Bytecode:** Compile EXPR, `DEREF`.
- **Use:** Dereference a Ref to get its value.

### compile/ref (polymorphic)

- **Syntax:** `(ref ARG)` or `(compile/ref ARG)`
- **Expansion:**
  - `sym` → `(compile/refq sym)`
  - `(attr obj name)` or `(compile/attr obj name)` → `(compile/ref-attr obj name)`
  - `(item coll key)` or `(compile/item coll key)` → `(compile/ref-item coll key)`
- **Use:** Single form to obtain a Ref for a variable, attribute, or item.

### Related specials

- **compile/setq** — Store into existing binding (`VAR_STORE`).
- **compile/delq** — Clear binding (`VAR_CLEAR`).
- **compile/ref-attr** — Ref to `obj.attr` (`ATTR_REF`).
- **compile/ref-item** — Ref to `obj[key]` (`ITEM_REF` or `CONST_ITEM_REF`).

---

## Syntactic Shortcuts

### &expr — Ref (ampersand)

- **Reader:** Character macro `&` triggers `_read_ref`. Reads one expression, wraps it in `(ref EXPR)`.
- **Examples:**
  - `&foo` → `(ref foo)` → expands to `(compile/refq foo)`
  - `&foo.bar` → `(ref foo.bar)` — symbol `foo.bar`, i.e. ref to a variable literally named `foo.bar` (not attribute access).

### *identifier — Deref (asterisk atom)

- **Reader:** Atom pattern matches `*[a-zA-Z_][a-zA-Z0-9_-]*` and converts to `(compile/deref identifier)`.
- **Examples:**
  - `*bar` → `(compile/deref bar)`
- **Note:** Pattern applies only to simple identifier atoms (no dotted forms). For `*x`, `x` is compiled as an expression; typically `x` holds a Ref, and the result is the dereferenced value.

---

## Bytecode and Runtime

| Opcode | Purpose |
|--------|---------|
| VAR_REF | Push Ref for name from current frame namespace. |
| VAR_BIND | Create/update binding: pop value, bind name to new Ref holding it. |
| VAR_STORE | Pop value, store into existing binding. |
| VAR_CLEAR | Clear binding for name. |
| DEREF | Pop Ref, call `ref.deref()`, push result. |
| ATTR_REF | Pop obj, push AttributeRef for obj.attr. |
| CONST_ITEM_REF / ITEM_REF | Push ItemRef for obj[key]. |

Frame namespaces must be `Namespace` (or compatible) for VAR_REF, VAR_BIND, VAR_STORE, VAR_CLEAR. Plain dicts support only value storage, not ref-based bindings.

---

## Source Locations

- **Namespace:** `koskari/lib/namespace.h`, `koskari/lib/namespace.c`
- **Ref:** `koskari/lib/ref.h`, `koskari/lib/ref.c`
- **Specials:** `koskari/compile/specials/slots.py`, `koskari/compile/specials/variables.py`
- **Reader:** `koskari/reader/parser.py` (`_read_ref`, `_as_deref_atom`, atom patterns)
- **Design:** `design/FEATURES.md`, `design/INVARIANTS-AND-CLARIFICATIONS.md`, `design/SPECIALS.md`
