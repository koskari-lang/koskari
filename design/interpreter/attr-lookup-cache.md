# Caching Attribute Lookup for CALL_ATTR* Opcodes

This document specifies the design for a shared attribute-resolution facility
used by CALL_ATTR, CALL_ATTR_NAMES, and CALL_ATTR_STAR. It separates
**resolution** (finding the attribute) from **invocation** (calling it), and
defines a per-call-site cache to accelerate repeated lookups.

**Status (2026-07-11):** Phases 1 and 2 are implemented — the resolver lives
in `eval.c` and the CALL_ATTR* family calls it with `cache = NULL`. Phase 3
(the cache itself) is the open work; the *Per-Site Cache Table* section
below records the agreed structure and allocation design, which supersedes
the older opcode-metadata sketch. Since compiler commits `58f21bd3` and
`27381299`, implicit dotted calls `(c.bump args)` route through the fused
`CALL_FAST_ATTR*` opcodes, so this cache now sits on the hot path of every
idiomatic method call.

The attribute-*read* opcodes (`FAST_ATTR`, `VAR_ATTR`) are also intended
consumers: they currently use bare `PyObject_GetAttr` and would share the
resolver, the table, and the entry form. The INSTANCE_DICT mode below exists
primarily for them (`self.x` reads).

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
| **INSTANCE_DICT** | Negative fact: no data descriptor (and no class-side hit worth caching) for this name on this type version; the value lives in the instance `__dict__`. This is the `self.x` read case. | None | Skip the MRO walk entirely; one instance-dict lookup. Miss in the dict → slow path (the attribute may have been deleted or the name may now resolve on the class). |
| **GENERIC_LOOKUP** | Must use full attribute semantics; no shortcut (custom `tp_getattro`, `__getattr__` fallback, etc.) | None | Perform full resolution (same logic as slow path). Caching this verdict still pays: it prevents a failed populate attempt on every access. |

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
   - INSTANCE_DICT         → single instance __dict__ lookup; hit → return
     value; miss → fall through to slow path
   - GENERIC_LOOKUP        → fall through to full resolution

5. Slow path: full resolution
   - Follow data descriptor → instance dict → non-data descriptor order.
   - Classify result into DATA_DESCRIPTOR, DIRECT_OBJECT_GUARDED,
     INSTANCE_DICT, or GENERIC_LOOKUP.
   - Populate cache: type, type_version, mode, resolved object/descriptor (owned).
```


## Cache Entry Structure

```c
typedef enum {
  KOS_ATTR_CACHE_UNCACHED,        /* zero: a freshly-allocated slot is valid */
  KOS_ATTR_CACHE_DATA_DESCRIPTOR,
  KOS_ATTR_CACHE_DIRECT_OBJECT_GUARDED,
  KOS_ATTR_CACHE_INSTANCE_DICT,
  KOS_ATTR_CACHE_GENERIC_LOOKUP,
} KosAttrCacheMode;

typedef struct {
  PyTypeObject *type;        /* Guard: receiver type; NULL = uncached */
  uint64_t type_version;     /* Guard: must match type's current version */
  KosAttrCacheMode mode;     /* Tag; family-local (see table section) */
  PyObject *resolved;        /* Descriptor or class object; owned ref when set */
} KosAttrCacheEntry;
```

This is the **attr-family variant of a tagged union**. The per-site table
(next section) stores fixed-size slots; the mode/tag byte discriminates, and
each consuming opcode family owns its own tag namespace and payload
interpretation. A future family with a different guard (e.g. a namespace
version for global-load caching) reuses the same slot shape — "a guard
pointer, a guard version, a tag, a payload" — with its own semantics. Slots
never migrate between families because indexes are assigned at assembly.

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
   :return: -1 on error (exception set); 0 = send self (unbound class method:
     KosFunction or PyFunction; caller invokes fn(obj, *args)); 1 = omit self
     (bound, instance dict, data descriptor result, etc.; caller invokes
     callable(*args)).
*/
int kos_resolve_named_attr(
    PyObject *obj,
    PyObject *name,
    KosAttrCacheEntry *cache,
    PyObject **out);
```

When `cache == NULL`, the function skips all cache logic and performs full
resolution only.

**Implementation** (eval.c): Static helpers `is_plain_instance_method` (KosFunction/PyFunction), `descr_is_data` (tp_descr_set != NULL), `instance_dict_get` (via `_PyObject_GetDictPtr`). Resolution order: custom tp_getattro → data descriptor → instance dict → non-data descriptor / class attribute → __getattr__.


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


## Per-Site Cache Table (agreed design, 2026-07-11)

This section supersedes the earlier opcode-metadata sketch. Decisions from
design discussion; recorded so they survive until implementation.

### Where the index lives: spare EXTRA row fields, not instruction bytes

Every current cache consumer is an EXTRA-style opcode with a spare schema
field — `CALL_FAST_ATTR` is `(FASTVAR, CONST, COUNT, NOARG)` with a3 free,
`FAST_ATTR` is `(FASTVAR, CONST, -, -)` with two spares. The cache index is
stored in a spare field of the instruction's extra row:

- Zero added instruction bytes; the handler already fetched the row.
- A caching opcode's extra row is made **unique** (never deduped with an
  identical row elsewhere) so each site gets its own slot; the assembler
  assigns the next slot index at the same time it numbers extras.
- Non-caching opcodes are untouched.

A uint16 cache-index field on *every* instruction was considered and
deferred: instruction streams are small so the 2 bytes/op would be
affordable, but it buys nothing until an opcode *outside* the EXTRA family
wants a cache — and the cheaper response then is to promote that opcode to
EXTRA (the same move the call family already made for multi-field args).

### Table structure: fixed-size tagged-union slots

- One flat array of fixed-size slots on **KosCode** (KosCode is a normal
  heap object; the arena seals code *data*, not the KosCode struct, so a
  writable table on KosCode does not conflict with sealing).
- Slot form: guard pointer + guard version + tag byte + payload pointer
  (see `KosAttrCacheEntry` above). Tags are family-local; the table itself
  has no cross-family semantics.
- Slot count is known at assembly and stamped on KosCode.

### Lifecycle

- **Allocation:** lazy — first eval of the code object allocates
  `count × sizeof(slot)`, zeroed. Zero = UNCACHED, so no init pass is
  needed. A `.kio` load therefore starts cold by construction.
- **Serialization:** the codex format ignores the table entirely; it is
  per-process runtime state.
- **GC:** cached `resolved` payloads are **strong refs**; KosCode's
  `tp_traverse` / `tp_clear` must cover the table or types and functions
  leak across reloads. (Borrowed-ref schemes were rejected: cheaper-looking,
  quietly wrong.)
- **Eviction / teardown:** `Py_XDECREF(entry->resolved)` on overwrite and on
  code deallocation, as before.

### Tracing

Wire per-family hit/miss counters into the `KOS_TRACE_*` framework from day
one, reported under a `cache` section of `get_trace_report()`. The
`churn_method` / `churn_starmap` benchmark pairs then give an immediate
hit-rate readout, and a bad guard shows up as a miss-rate anomaly instead of
a silent perf mystery.

### Expected magnitude (from 2026-07-10 churn_method traces)

Resolve is roughly 30ns of `CALL_FAST_ATTR`'s 171ns; `FAST_ATTR` reads
(`self.x`) are ~25ns each through full `PyObject_GetAttr` and appear twice
per benchmark iteration. Cache hits on all three sites are worth ~15% on the
method pair. The remaining method-call gap vs CPython is frame + namespace
allocation per call, which this cache does not address.


## Implementation Phases

### Phase 1: Resolver only (no cache) — DONE

Implemented in `eval.c`: `kos_resolve_named_attr(obj, name, NULL, out)` with
static helpers `is_plain_instance_method`, `descr_is_data`,
`instance_dict_get`. The cache parameter is accepted and ignored; the cache
insertion point is marked with a comment referencing this document.


### Phase 2: Opcodes use resolver — DONE

CALL_ATTR, CALL_ATTR_NAMES, CALL_ATTR_STAR and their VAR/FAST variants call
the resolver with `cache = NULL` and carry the send-self invocation logic.


### Phase 3: Cache integration — OPEN

Per the *Per-Site Cache Table* section above:

- Assembler: unique extra rows for caching opcodes; assign slot index into
  the spare row field; stamp slot count on KosCode.
- KosCode: table pointer slot; lazy zeroed allocation at first eval;
  `tp_traverse` / `tp_clear` coverage; serializer ignores.
- Resolver: cache hit/miss logic per the linear algorithm — type + version
  guards, instance shadow check, INSTANCE_DICT negative-fact mode.
- Extend the consumers beyond CALL_ATTR*: `FAST_ATTR` / `VAR_ATTR` reads.
- Real `koskari_type_dict_version` where available (version-gated shim,
  3.9+ support; unstable API is 3.12+).
- Trace hit/miss counters under a `cache` section of `get_trace_report()`.


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
