# Caching Attribute Lookup for CALL_ATTR* Opcodes

This document specifies the design for a shared attribute-resolution facility
used by CALL_ATTR, CALL_ATTR_NAMES, and CALL_ATTR_STAR. It separates
**resolution** (finding the attribute) from **invocation** (calling it), and
defines a per-call-site cache to accelerate repeated lookups.

**See also:** [OPCODES.md](../OPCODES.md) (CALL_ATTR* opcodes),
[vector-call.md](vector-call.md) (call conventions).


## Goals

1. **Shared resolver** — One function, `kos_resolve_named_attr`, used by all
   three CALL_ATTR* opcodes.
2. **MRO-aware resolution** — Walk the type's MRO; honor data descriptors,
   instance dict, and non-data descriptors per Python semantics.
3. **Avoid bound-method allocation when possible** — When the attribute is a
   KosFunction or Python function in the class, the caller can invoke it
   directly with `(obj, *args)` instead of creating a KosBoundMethod.
4. **Per-call-site cache** — Each CALL_ATTR* instruction has its own cache
   entry. The name is fixed per call site; the cache key is effectively the
   type of `obj`.
5. **Type-version invalidation** — When a type's dict is modified, the cache
   must be invalidated. A type version (or dict version) is stored and checked.


## Separation of Concerns

### Resolution (this design)

The resolver returns **the attribute value**. It does not decide how to call it.
The cache stores **resolution facts** (descriptor, class-side object, or "use
generic lookup"), not call behavior.

### Invocation (caller responsibility)

The CALL_ATTR* opcodes receive the resolved object and decide how to invoke it:

- Unbound function (KosFunction, PyFunction) → prepend `obj` and call.
- Already bound (KosBoundMethod, Python bound method) → call as-is.
- Plain callable → call directly.

Invocation logic lives in the opcode handlers, not in the resolver.


## Resolution Algorithm

### Full (Slow) Path

When no cache exists or the cache is invalid, perform full resolution in this
order (Python semantics):

1. **Data descriptor** — Search the type's MRO for a data descriptor (has
   `__set__` or `__delete__`; use `PyDescr_IsData`). If found, call
   `descriptor.__get__(obj, type)`. No shortcut: the value is computed per
   access (e.g. property).

2. **Instance dict** — If the instance has `__dict__` and it contains the name,
   return that value. No descriptor, no intermediary.

3. **Non-data descriptor / class attribute** — Search the MRO for a non-data
   descriptor or plain attribute. If found:
   - **Plain instance-method descriptor** (KosFunction, PyFunction): Return the
     descriptor itself. Do **not** call `__get__`. The caller invokes it with
     `(obj, *args)`. This avoids allocating a KosBoundMethod or
     `types.MethodType`.
   - **Method descriptor** (slot wrapper, e.g. `list.append`, `dict.get`): Must
     call `__get__`; return the result. We cannot avoid the method-wrapper
     allocation, but we cache the descriptor to avoid MRO lookup on subsequent
     accesses.
   - **staticmethod / classmethod**: Must call `__get__`; their behavior depends
     on descriptor invocation. Return the result.
   - **Plain attribute** (no `__get__`): Return the object as-is.

4. **__getattr__** — If not found, call `__getattr__` if present; otherwise
   raise `AttributeError`.

### Avoiding Intermediary Objects

The only shortcut that avoids allocation is for **plain instance-method
descriptors** (KosFunction, PyFunction in the class):

- **Normal path:** `descriptor.__get__(obj, type)` allocates a bound method
  (KosBoundMethod or `types.MethodType`), then the caller invokes it.
- **Our path:** Return the descriptor; the caller invokes `fn(obj, *args)`.
  Same effect, no bound-method allocation.

We **cannot** shortcut data descriptors (property, etc.), staticmethod,
classmethod, or method descriptors (slot wrappers); we must call `__get__` and
return the result. For method descriptors we can still cache the descriptor to
avoid the MRO walk, though the method-wrapper allocation occurs each time. For
instance dict and plain class attributes, no descriptor is involved and no
intermediary is created.

### Cached Modes

The result of full resolution is classified into one of three cacheable modes:

| Mode | Meaning | Cached value | Fast-path behavior |
|------|---------|--------------|--------------------|
| **DATA_DESCRIPTOR** | Data descriptor in class; instance dict irrelevant | Descriptor (owned ref) | Call `descriptor.__get__(obj, type)` |
| **DIRECT_OBJECT_GUARDED** | Non-data descriptor or class attribute; instance may shadow. Caches: (a) plain instance methods (KosFunction, PyFunction) as unbound descriptor; (b) method descriptors (slot wrappers) for MRO shortcut. | Class-side descriptor/object (owned ref) | After instance shadow check: if plain method, return cached; if method descriptor, call `__get__` and return |
| **GENERIC_LOOKUP** | Must use full attribute semantics; no shortcut | None | Perform full resolution (same logic as slow path) |

**UNCACHED** — No cache entry yet. Treated as cache miss.


### Instance Shadow Check (DIRECT_OBJECT_GUARDED)

When using a cached DIRECT_OBJECT_GUARDED result, the instance may have
overridden the attribute. Before returning the cached class-side object:

1. If `obj` has `__dict__` (e.g. `tp_dictoffset != 0`).
2. And `PyDict_GetItem(obj.__dict__, name)` returns non-NULL.

Then **do not** use the cached object; fall through to the slow path.


### Type Version Check

Before using any cached result, validate:

1. `Py_TYPE(obj) == cache->type`
2. `koskari_type_dict_version(Py_TYPE(obj)) == cache->type_version`

If either fails, treat as cache miss and perform full resolution. The type
version must be obtained via a compat helper (e.g. `PyType_GetDictVersion` on
3.12+ or an internal API on older versions). When versioning is unavailable,
return a sentinel (e.g. 0) that never matches.


## Linear Cache Algorithm

```
1. Check cache presence
   - If no cache entry (UNCACHED), go to slow resolution.

2. Check cache validity
   - type(obj) == cached_type
   - type_version(type(obj)) == cached_type_version

3. For DIRECT_OBJECT_GUARDED only: instance shadow check
   - If instance __dict__ contains name, do not use cached result; go to slow.

4. Use cached resolution if guards pass
   - DATA_DESCRIPTOR       → resolve via descriptor.__get__(obj, type)
   - DIRECT_OBJECT_GUARDED → if plain method (KosFunction, PyFunction), return
     cached (Py_INCREF); if method descriptor, call __get__ and return
   - GENERIC_LOOKUP        → fall through to full resolution

5. Slow path: full resolution
   - Follow data descriptor → instance dict → non-data descriptor order.
   - Classify result into DATA_DESCRIPTOR, DIRECT_OBJECT_GUARDED, or GENERIC_LOOKUP.
   - Populate cache: type, type_version, mode, resolved object/descriptor (owned).
```


## Cache Entry Structure

```c
typedef enum {
  KOS_ATTR_CACHE_UNCACHED,
  KOS_ATTR_CACHE_DATA_DESCRIPTOR,
  KOS_ATTR_CACHE_DIRECT_OBJECT_GUARDED,
  KOS_ATTR_CACHE_GENERIC_LOOKUP,
} KosAttrCacheMode;

typedef struct {
  PyTypeObject *type;        /* Cache key; NULL = uncached */
  uint64_t type_version;     /* Must match type's current version */
  KosAttrCacheMode mode;
  PyObject *resolved;        /* Descriptor or class object; owned ref when set */
} KosAttrCacheEntry;
```

- **DATA_DESCRIPTOR:** `resolved` is the descriptor. On use, call
  `Py_TYPE(resolved)->tp_descr_get(resolved, obj, type)`.
- **DIRECT_OBJECT_GUARDED:** `resolved` is the class-side descriptor or object.
  Two sub-behaviors after instance shadow check: (a) **Plain method** (KosFunction,
  PyFunction): return the descriptor directly; caller invokes with `(obj, *args)`.
  (b) **Method descriptor** (slot wrapper, e.g. list.append): call
  `descriptor.__get__(obj, type)` and return the result; caller invokes as-is.
  Detect via `KosFunction_CheckExact`, `PyFunction_Check`, `PyMethodDescr_Check`,
  or equivalent. Caller Py_INCREFs on returned value.
- **GENERIC_LOOKUP:** `resolved` is NULL. No shortcut; perform full resolution.

On cache eviction (miss, overwrite, or teardown): `Py_XDECREF(entry->resolved)`.


## Resolver API

```c
/**
   Resolve attribute by name on an object. Uses cache when provided and valid.

   :param obj: Object to resolve attribute on (borrowed).
   :param name: Attribute name (borrowed, PyUnicode).
   :param cache: Optional per-call-site cache (NULL = always full lookup).
   :param out: Output: resolved attribute (new ref on success).
   :return: 0 on success, -1 on error (exception set).
*/
int kos_resolve_named_attr(
    PyObject *obj,
    PyObject *name,
    KosAttrCacheEntry *cache,
    PyObject **out);
```

When `cache == NULL`, the function skips all cache logic and performs full
resolution only.


## Caller (CALL_ATTR*) Invocation

After `kos_resolve_named_attr` returns successfully, the opcode:

1. Checks that `*out` is callable (or raises `TypeError`).
2. Dispatches invocation:
   - **KosCallable** (KosFunction, KosBoundMethod, etc.) → `KosContext_Call`
     or equivalent. KosBoundMethod is unwrapped internally; unbound KosFunction
     receives `(obj, *args)`.
   - **Plain Python callable** → `PyObject_Vectorcall` (or `PyObject_Call` for
     CALL_STAR). For unbound functions, prepend `obj` to the argument vector.

The resolver does not distinguish Kos vs Python or bound vs unbound for
invocation; it only returns the attribute. Invocation logic is in the opcode.


## Cache Allocation and Location

### Per-call-site storage

Each CALL_ATTR* instruction has a dedicated cache entry. The association is
via a **cache index** stored in the opcode metadata:

- Opcode layout: `(opcode, meta, arg)` where `meta` is a cache index (or
  reused for other flags on non-caching opcodes).
- Code object holds a pre-allocated array of `KosAttrCacheEntry` at creation.
- At execution: `cache = &code->attr_cache[insn->meta]`.


### Lifecycle

- **Allocation:** When the code object is created, allocate
  `attr_cache[0..num_call_attr_sites]`. Initialize all entries to UNCACHED
  (e.g. `type = NULL`).
- **Teardown:** When the code object is deallocated, `Py_XDECREF` each
  non-NULL `resolved` in the cache array.


## Implementation Phases

### Phase 1: Resolver only (no cache)

- Implement `kos_resolve_named_attr(obj, name, NULL, out)`.
- Full resolution path only; no cache parameter used.
- Add compat helper `koskari_type_dict_version(PyTypeObject *)` (can return 0
  initially).
- Place in new module (e.g. `attr.c` / `attr.h`) or existing lib.
- No opcode changes yet.


### Phase 2: Opcodes use resolver

- Implement CALL_ATTR, CALL_ATTR_NAMES, CALL_ATTR_STAR.
- Each calls `kos_resolve_named_attr(obj, name, NULL, &resolved)`.
- Add invocation logic: KosCallable vs plain Python; bound vs unbound handling.
- Stack layout and argument extraction per OPCODES.md.
- Cache parameter remains NULL.


### Phase 3: Cache integration

- Extend KosCode with `attr_cache` array and cache index in opcode metadata.
- Pass `&code->attr_cache[insn->meta]` into `kos_resolve_named_attr`.
- Implement cache hit/miss logic, type version check, instance shadow check.
- Implement cache lifecycle (init on code creation, DECREF on eviction/teardown).
- Ensure `koskari_type_dict_version` uses a real implementation where
  available.


## CPython Source Research

Findings from CPython 3.12 `Objects/object.c` and `Objects/typeobject.c`:

### __getattr__ Invocation

- `_PyObject_GenericGetAttrWithDict` does **not** invoke `__getattr__`. It raises
  `AttributeError` when the attribute is not found.
- `__getattr__` is invoked by the `tp_getattro` slot when a type defines it:
  - `_Py_slot_tp_getattr_hook` is installed for types with `__getattr__` (via
    `update_one_slot` when `TPSLOT(__getattr__, ...)` is set).
  - That hook: (1) calls `__getattribute__`, (2) on `AttributeError`, calls
    `__getattr__(self, name)`.
- **Implication:** A custom resolver that avoids `PyObject_GetAttr` must, after
  the standard MRO/dict/descriptor lookup fails, check
  `PyType_Lookup(tp, &PyId(__getattr__))` and if non-NULL, call it with
  `(obj, name)` to preserve `__getattr__` semantics.

### Instance Dict Access

- Use `_PyObject_ComputedDictPointer(obj)` (or `PyObject_GenericGetDict` for
  `__dict__` itself). Internally:
  - `dictoffset = tp->tp_dictoffset`
  - `dictoffset == 0` → no instance dict
  - `dictoffset < 0` → offset from end of variable-size part
  - Otherwise: `(PyObject **)((char *)obj + dictoffset)` for the dict pointer
- Python 3.12+ may use `Py_TPFLAGS_MANAGED_DICT` or `Py_TPFLAGS_INLINE_VALUES`;
  `_PyObject_GetDictPtr` or equivalent may be needed. Koskari should check
  support for its target Python versions.

### Descriptor Detection

- `PyDescr_IsData` (in `Include/cpython/descrobject.h`, CPython-internal)
  distinguishes data vs non-data descriptors.
- Method descriptor types: `PyMethodDescr_Type`, `PyWrapperDescr_Type` in
  `Include/descrobject.h`. Use `Py_IS_TYPE(descr, &PyMethodDescr_Type)` or
  `Py_IS_TYPE(descr, &PyWrapperDescr_Type)` (or public checks if available).

### Custom __getattribute__

- If `tp->tp_getattro != PyObject_GenericGetAttr`, the type overrides attribute
  lookup. A custom resolver cannot safely shortcut; fall back to
  `PyObject_GetAttr(obj, name)` for correctness.

### call_attribute and Py_TPFLAGS_METHOD_DESCRIPTOR

- `call_attribute` in `typeobject.c` checks
  `_PyType_HasFeature(Py_TYPE(attr), Py_TPFLAGS_METHOD_DESCRIPTOR)` to decide
  how to invoke a descriptor. Method descriptors (e.g. slot wrappers) use
  `PyObject_Vectorcall` with `(self, name)`; otherwise normal descriptor
  protocol applies.


## Compatibility Notes

- **Type version:** Use `PyType_GetDictVersion` on 3.12+; fall back to internal
  API or 0 on older versions. When 0, cache validity relies only on type
  identity; document that monkey-patching may lead to stale cache until
  type identity changes.
- **Instance dict access:** Use `tp_dictoffset` and `PyObject_GenericGetDict`
  or equivalent to access instance `__dict__` when checking for shadowing.
- **Method descriptor detection:** For DIRECT_OBJECT_GUARDED fast-path
  branching, detect plain methods (KosFunction, PyFunction) vs method
  descriptors (slot wrappers). Use `PyMethodDescr_Check`, `PyWrapperDescr_Check`,
  or type comparison against `PyMethodDescr_Type` / `PyWrapperDescr_Type` as
  appropriate for the Python version.


## Summary

| Component | Responsibility |
|-----------|----------------|
| `kos_resolve_named_attr` | Attribute lookup; cache management; returns resolved object |
| CALL_ATTR* opcodes | Stack layout; call `kos_resolve_named_attr`; invoke result |
| Cache entry | type, type_version, mode, resolved (owned ref) |
| Modes | DATA_DESCRIPTOR, DIRECT_OBJECT_GUARDED, GENERIC_LOOKUP, UNCACHED |

The end.
