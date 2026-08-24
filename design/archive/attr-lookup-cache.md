# Caching Attribute Lookup for CALL_ATTR* Opcodes

This document specifies the design for a shared attribute-resolution facility
used by CALL_ATTR, CALL_ATTR_NAMES, and CALL_ATTR_STAR. It separates
**resolution** (finding the attribute) from **invocation** (calling it), and
defines a per-call-site cache to accelerate repeated lookups.

**Status (2026-07-12):** Implemented and active. The per-site cache framework
(`KosCacheSlot` tagged union, widened `KosOp`, trailing slot array on
`KosCode`, the two pragmas) is in place, and two cache families consume it:

- **Method cache** (`KOS_CACHE_METHOD`) — the CALL_ATTR* family resolves the
  callable through the cache. Modes per the *Cached Modes* section, plus a
  `KOS_METHOD_MEGAMORPHIC` churn bypass. Since compiler commits `58f21bd3`
  and `27381299`, implicit dotted calls `(c.bump args)` route through the
  fused `CALL_FAST_ATTR*` opcodes, so this cache sits on the hot path of
  every idiomatic method call.
- **Field cache** (`KOS_CACHE_FIELD`) — the attribute read/write opcodes
  (`FAST_ATTR`, `FAST_ATTR_STORE`, `VAR_ATTR`, `VAR_ATTR_STORE`) resolve the
  *storage location* through the cache: a `__slots__` member offset (SLOT,
  all versions) or an inline managed-dict value index (INSTANCE_VALUE,
  3.11+). See *Attribute-storage (field) cache* below.

The *Per-Site Cache Table* section is authoritative for the shared mechanism;
the two family sections describe what each stores and how it guards.

**See also:** [OPCODES.md](../OPCODES.md) (CALL_ATTR* opcodes),
[vector-call.md](vector-call.md) (call conventions),
[concepts/opcode-caching.md](../../docs/reference/concepts/opcode-caching.md)
is the user-facing summary (behavior and pragmas, no internals).


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


## Cache Slot Structure

A cache slot is a **tagged union**, in the same shape as `KosDynNode`: one
`kind` discriminant selects the active union member, which is a layout
**specialized to one caching use case**. All slots share a single size (the
largest layout), so the trailing array on a KosCode is flat and
index-addressable. `KOS_CACHE_EMPTY` (zero) is an unused slot -- the state of
every freshly zeroed slot -- and owns nothing.

```c
typedef enum KosCacheKind {
  KOS_CACHE_EMPTY = 0,   /* zeroed slot: no layout, no owned refs, no cleanup */
  KOS_CACHE_METHOD,      /* method-resolution layout (attribute/method lookup) */
  KOS_CACHE_FIELD,       /* attribute-storage location (load/store/delete) */
  /* future: KOS_CACHE_JUMP, KOS_CACHE_EXCEPTION, ... */
} KosCacheKind;

typedef enum KosMethodMode {
  KOS_METHOD_DATA_DESCRIPTOR = 0,     /* call resolved->__get__ on each hit */
  KOS_METHOD_DIRECT_OBJECT_GUARDED,   /* class-side object, instance-shadow checked */
  KOS_METHOD_INSTANCE_DICT,           /* negative fact: value in instance dict */
  KOS_METHOD_GENERIC_LOOKUP,          /* no shortcut; full resolution */
  KOS_METHOD_MEGAMORPHIC,             /* churn bypass: skip guard, never populate */
} KosMethodMode;

typedef enum KosFieldMode {
  KOS_FIELD_GENERIC = 0,      /* no cacheable location; PyObject_GetAttr/SetAttr */
  KOS_FIELD_INSTANCE_VALUE,   /* inline managed-dict values; index = value slot */
  KOS_FIELD_SLOT,             /* __slots__ member; index = byte offset from obj */
  KOS_FIELD_DICT_HINT,        /* materialized instance __dict__; index = keys hint */
  KOS_FIELD_MEGAMORPHIC,      /* churn bypass: skip guard, never populate */
} KosFieldMode;

typedef struct KosCacheSlot {
  KosCacheKind kind;           /* discriminant; KOS_CACHE_EMPTY (0) = unused */
  union {
    struct {                   /* KOS_CACHE_METHOD */
      PyTypeObject *type;      /* guard receiver type; owned */
      uint64_t type_version;   /* guard: type dict version at populate */
      KosMethodMode mode;      /* which resolution behavior */
      PyObject *resolved;      /* cached descriptor/object; owned, may be NULL */
    } method;
    struct {                   /* KOS_CACHE_FIELD -- ref-free */
      PyTypeObject *type;      /* guard receiver type; borrowed */
      uint64_t type_version;   /* guard: type dict version at populate */
      KosFieldMode mode;       /* which storage mechanism */
      uint8_t misses;          /* consecutive guard misses; drives the bypass */
      uint32_t index;          /* value slot / byte offset / dict-key hint */
    } field;
  } u;
} KosCacheSlot;
```

The two layouts differ in a way that matters for GC. The method layout owns
strong refs (`type`, `resolved`), so a populated method slot makes `KosCode`
GC-tracked. The field layout is **ref-free**: it guards on a *borrowed* type
plus a version, and its payload is an integer, so it needs no
release-before-overwrite and a no-op teardown. `DICT_HINT` is reserved (the
materialized-dict case currently resolves to `GENERIC`); it is the parked
growth slot for a keys-hint fast path.

### Specialized layouts, discriminated only at the top

The union member is chosen by `kind` alone. Layouts are **specialized per use
case and never sub-branched**: two different uses do not share or fork a
layout, they get distinct kinds. A later field-accessor cache, a jump-target
cache, or an exception-type cache each adds its own kind and its own union
member; none reshapes an existing one. There is no universal slot shape and
no attempt to wedge every use into one -- a layout may resemble another, but
resemblance never justifies merging them. Method resolution is the only kind
being built now; `KOS_CACHE_METHOD` is the bootstrap non-empty kind.

The four attribute-resolution behaviors are an **internal `mode` field of the
one method layout**, not top-level kinds -- they share the method layout's
fields and only select its fast-path behavior:

- **DATA_DESCRIPTOR:** `resolved` is the descriptor. On use, call
  `Py_TYPE(resolved)->tp_descr_get(resolved, obj, type)`.
- **DIRECT_OBJECT_GUARDED:** `resolved` is the class-side descriptor or object.
  Two sub-behaviors after instance shadow check: (a) **Plain method** (KosFunction,
  PyFunction): return the descriptor directly; caller invokes with `(obj, *args)`.
  (b) **Method descriptor** (slot wrapper, e.g. list.append): call
  `descriptor.__get__(obj, type)` and return the result; caller invokes as-is.
  Detect via `KosFunction_CheckExact`, `PyFunction_Check`, `PyMethodDescr_Check`,
  or equivalent. Caller Py_INCREFs on returned value.
- **INSTANCE_DICT / GENERIC_LOOKUP:** `resolved` is NULL (a cached negative
  fact); take the slow path per the mode.

### Populate, cleanup, and GC all dispatch on `kind`

- **First use (populate).** An opcode reads its slot's `kind`. `KOS_CACHE_EMPTY`
  means cold: it fills its layout's fields, then sets `kind` to its own value.
  Every later hit reads `kind` to select the union member; a member is never
  read without first checking `kind`.
- **Cleanup / eviction.** `switch (kind)` and drop only that layout's owned
  refs (`KOS_CACHE_METHOD` drops `type` and `resolved`); `KOS_CACHE_EMPTY`
  does nothing. This is `cache_slot_release` in `bytecode.c`.
- **GC.** The same `switch (kind)` drives `tp_traverse` (visit the layout's
  owned refs) and `tp_clear`. Owning the guard `type` (not just `resolved`)
  keeps the identity guard sound even where a dict-version is unavailable, at
  the cost of pinning the type for the code object's life. See *Table storage*
  and *Lifecycle* for when KosCode becomes GC-tracked.


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
    KosCacheSlot *cache,       /* NULL, or a slot the resolver reads/populates
                                  as KOS_CACHE_METHOD */
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


## Attribute-storage (field) cache

Where the method cache resolves a *handle* (a class-side callable), the field
cache resolves a *location*: where an instance attribute lives, so a load, a
store, and a delete at any site for the same type+name share one resolved
`(mode, index)`. The four consumers are `FAST_ATTR` / `VAR_ATTR` (read) and
`FAST_ATTR_STORE` / `VAR_ATTR_STORE` (write). Lives in `eval/eval_cache.c`
(`kos_resolve_attr_location` and helpers).

### The location result

The resolved location is a discriminated `(mode, index)` pair, uniform across
load/store/delete and branching only on the storage *mechanism*:

| Mode | `index` is | Load / store |
|------|-----------|--------------|
| **SLOT** | byte offset of a `__slots__` member from `obj` | direct `*(PyObject **)((char *)obj + index)` |
| **INSTANCE_VALUE** | value slot in the instance's inline managed-dict values | direct read/write of `values[index]` |
| **DICT_HINT** *(reserved)* | keys-index hint into a materialized `__dict__` | parked; resolves to GENERIC today |
| **GENERIC** | — | `PyObject_GetAttr` / `PyObject_SetAttr` (full semantics) |
| **MEGAMORPHIC** | — | churn bypass: re-resolve every time, never cache |

### Classifier

`resolve_attr_location_slow` classifies once, on the cold path:

1. Custom `tp_getattro`/`tp_setattro` (not the generic ones) → **GENERIC**;
   either could intercept, so no direct location is safe.
2. A **data descriptor** for the name: if it is a `__slots__` member
   descriptor (`PyMemberDescr`, `T_OBJECT_EX`, valid offset) → **SLOT** (the
   offset is authoritative for both load and store, and takes precedence over
   any instance dict). Any other data descriptor (property, ...) → **GENERIC**.
3. No data descriptor, and the type stores attributes as addressable inline
   values → **INSTANCE_VALUE**, with `index` found by scanning the type's
   shared keys. A non-data descriptor of the same name does not defeat this:
   the instance value shadows it on read, and an unset slot falls back to
   GENERIC, which then finds the class attribute.
4. Otherwise → **GENERIC**.

GENERIC is cached guard-free (it is correct for any receiver type) and doubles
as the give-up state. SLOT and INSTANCE_VALUE carry a type-identity +
`tp_version_tag` guard; on repeated guard misses the site degrades to
MEGAMORPHIC and stops caching.

### Guards and safety

The cached `(mode, index)` is guarded by type identity + version, exactly as
the method cache. On top of that, the direct modes re-check the instance's
own state on each access, because the cache is a *type* fact and materialization
is a *per-instance* event:

- **SLOT** load: an unset member reads NULL → fall through to the generic path
  (AttributeError / `__getattr__`).
- **INSTANCE_VALUE** load: if the instance has materialized its dict, or the
  slot is unset, the accessor reports inactive/NULL → fall through (which then
  reads the materialized dict, or raises). Guards match CPython's own
  `LOAD_ATTR_INSTANCE_VALUE` (type version + values-mode + non-NULL slot).
- **INSTANCE_VALUE** store is **overwrite-only**: a first assignment (slot was
  unset) or a materialized instance falls back to `PyObject_SetAttr`, which
  keeps the values' used-count and insertion-order bookkeeping correct. The
  common in-place update (slot already set) takes the fast path.

### Version coverage and the compat layering

The inline-value layout is CPython-internal and unexported, so the field cache
replicates just the layout it reads, under koskari's own names -- no
`Py_BUILD_CORE`, no internal headers at build time, no unexported symbol
linked. The design splits along what is shared vs what drifts:

- **Shared** (`compat.h`, `PY_MINOR_VERSION >= 11`): the name→index scan,
  `kos_type_value_index`, over the type's `ht_cached_keys` (a public
  `PyHeapTypeObject` field). The shared-keys layout is identical across
  3.11-3.14 for GIL builds. Replaces the unexported `_PyDictKeys_StringLookup`
  with a cold-time entry scan (`me_key == name`, rich-compare fallback).
- **Per-version** (`compat_3xx.h`): two hooks, `kos_type_has_inline_values`
  and `kos_inline_value_slots`, isolate where the values live and how "still
  inline" is tested:

  | Version | Values at | "inline?" test |
  |---------|-----------|----------------|
  | 3.11 | pre-header slot `obj[-4]` | pointer non-NULL |
  | 3.12 | tagged union `obj[-3]` | low tag bit set |
  | 3.13 | `obj + sizeof(PyObject)` | `values->valid` |
  | 3.14 | `obj + tp_basicsize` | `values->valid` |

`eval_cache.c` and the four handlers are version-agnostic over those hooks
(`PY_MINOR_VERSION >= 11`). **3.9 / 3.10** predate the managed dict (PEP 412
split dicts, and an opaque `PyDictKeysObject` in their shipped headers); they
have no INSTANCE_VALUE and stay on SLOT + GENERIC -- correct, unaccelerated
for dict attributes. The floor sits at 3.11 deliberately: the older split-dict
layout would mean blind-replicating an opaque keys struct for two EOL /
near-EOL versions with the smallest payoff, so they keep the correct generic
path instead. The fast path is also **GIL-only**: under free-threading
`kos_type_has_inline_values` returns 0, so those builds stay correct on the
generic path.


## Per-Site Cache Table (agreed design, 2026-07-11)

This section is authoritative. It supersedes every earlier sketch, including
the spare-EXTRA-field and lazy-allocation notes that previously appeared
here.

### Opcode cache count: opcodes.incl, immediately after the name

`opcodes.incl` gains a cache-slot **count** as the argument **immediately
after the opcode name**, before the style:

```
OP(name, cache, style)                    // cache = number of slots wanted
OP(name, cache, EXTRA, f0, f1, f2, f3)
```

`cache` is the number of cache slots the opcode reserves per site; `0` means
non-consumer. It is a count from the start, not a bool: the allocator treats
`0`, `1`, and `N` uniformly, so a single opcode can later reserve a
contiguous run (see *Monomorphic by law; the counter as growth path*). The
initial landing sets **every** opcode to `0` — the framework ships first,
then individual opcode implementations are switched on one at a time. The
count is a static per-opcode fact, exposed through a
`kos_opcode_cache_count(op)` accessor parallel to `kos_opcode_style` /
`kos_opcode_extra_schema`.

The flag lives on the **opcode enumeration**, never on the serialized
instruction. Slot assignment is therefore **mandatorily rebuildable**: it is
recomputed from the opcode flags of the reading build every time a code
object is assembled or a `.kio` is re-hardened, and nothing about slots
crosses the wire.

The `OP` macro is already variadic (`OP_NARGS` dispatch); inserting the flag
shifts the destructured arities from 2/6 to 3/7. Consumers that spell the
macro `OP(name, ...)` are unaffected.

### Per-instruction cache index: a widened KosOp

`KosOp` widens to carry the cache index. The instruction word gains a 16-bit
cache-index field alongside `op` and `arg`. The unset value is all-high bits
(`0xFFFF`) — an instruction with no reserved slot. A cache-consuming
instruction carries the index of the slot reserved exclusively for it.

The index is a per-instruction (per-call-site) fact, distinct from the
static opcode flag: the flag says the opcode *can* cache; the index says
*which* slot this particular site owns. The code array lives in the sealed
mmap block, and the index is written at assembly before sealing, so it is
read-only at runtime.

The index lives on every instruction uniformly, so any opcode — EXTRA-family
or not — can cache without opcode promotion or bespoke extra-row handling.

### Slot assignment: counted at compile time

During assembly, count the instructions whose opcode has the cache flag set.
Each such instruction is assigned the next slot index, stamped into its
widened cache-index field. That running count is the number of slots the
code object needs, and it is known before the KosCode object is finalized.

When the cache is gated off for this code object (see *Gating the cache*
below), the count short-circuits to zero: no instruction is assigned an
index, and every cache-index field is left at the unset value. The gate and
the slot count move together — there is no state where the gate is off yet
some instruction still holds a live index.

Counting, numbering, and sizing follow the **same discipline as the extras
table**, in a separate segment of memory (the trailing array below rather
than the sealed block). The one rule that does **not** carry over is
dedup: extras rows may be merged when structurally identical, but cache
slots are **per-site and never merged**. Two identical instructions still
receive independent slots, because the entire purpose is a site-local fact.
Do not borrow the extras dedup pass for cache assignment.

### Monomorphic by law; the counter as growth path

Each slot answers **one most-expensive question** for its cache family, under
a single guard. Polymorphism is an explicit **non-goal**: a slot never holds
a chain, and a megamorphic site degrades to the uncached path rather than
growing a multi-way cache. This keeps every fast path to a single guard check
with no chain walk, and keeps the slot fixed-size. Do not grow a chain into a
slot; if a site is polymorphic, it takes the miss.

The sanctioned growth path, should a *single opcode* ever need to cache more
than one distinct fact (not polymorphism — several different questions at one
site), is to widen the flag from a boolean into a small **count**:

- The flag becomes `0..k`; it is still a static per-opcode constant on the
  enumeration, so the assembler knows the run length from the opcode alone.
- The opcode receives the index of its **first** slot; slots
  `first .. first + N - 1` are its by a contiguity invariant, indexed against
  its compile-time-known `N`. No per-site width is stored.
- Assignment is **all-or-nothing**: if the compiler cannot grant the full run
  (gated off, or would not fit under the sentinel), the opcode receives the
  unset value `0xFFFF` and takes the uncached path. A shortfall reuses the
  exact branch a non-consumer already has; it adds no new runtime state.
- The only new bound: a run of `N` must fit under the sentinel, so the last
  legal first-index is `0xFFFE - (N - 1)`.

This is recorded as future-sanctioned, not current. The landing flag is
`0`/`1`; the counter is the pre-agreed shape if the need arises.

### Table storage: trailing array on the KosCode object

The cache slots hang off the **end of the KosCode object**. KosCode is a
normal heap object; the slot array is part of the object's own allocation, a
trailing array sized by the slot count. Because it lives in the KosCode
object (writable), not in the sealed mmap code block, it is unaffected by
arena sealing — sealing protects the code *data*, never the KosCode object.

- **Allocation:** the object is sized to include the trailing cache array at
  construction, and the array is **mandatorily zeroed at allocation**. Zero
  is `KOS_CACHE_EMPTY`, so no separate init pass is needed. A `.kio` load
  re-hardens from softened instructions through the same path, so slot
  assignment and the zeroed table are reconstructed deterministically on load.
- **Slot form:** the tagged union of *Cache Slot Structure* above -- a `kind`
  discriminant plus a per-kind specialized layout. The table stores fixed-size
  slots; interpretation is entirely per-kind. There is no cross-slot or
  cross-family shared shape.
- **Serialization:** the codex format ignores the table entirely; it
  round-trips through softened `(opcode, arg)` instructions and never stores
  raw KosOp bytes or slot state. The cache index is recomputed at assembly
  on load, not serialized.
- **GC:** a live layout's owned refs (for `KOS_CACHE_METHOD`, the guard `type`
  and `resolved`) are **strong refs**. While every slot is `KOS_CACHE_EMPTY`
  the table holds nothing and KosCode stays non-GC (its standing invariant).
  Once a consumer populates ref-holding slots, KosCode must become GC-tracked
  with a `tp_traverse` / `tp_clear` that `switch (kind)` over the slots, or
  types and functions leak across reloads. (Borrowed-ref schemes were
  rejected: cheaper-looking, quietly wrong.)
- **Eviction / teardown:** `cache_slot_release` drops the live layout's owned
  refs, dispatching on `kind`, on overwrite and on code deallocation.
  `KOS_CACHE_EMPTY` releases nothing.

### Gating the cache (pragma-controlled)

Caching is a compile-time choice per code object, not an unconditional
feature. A code object known to be evaluated at most once gains nothing from
a cache, and the slots would be pure overhead. Such an object is compiled
with the cache gated off:

- The trailing cache array has length **zero** — no slots on the object.
- Every instruction, **including** cache-supporting opcodes, carries the
  unset value (`0xFFFF`) in its cache-index field. No slot is assigned to
  anyone.

This collapses into the same runtime condition an opcode already has to
tolerate. A cache-supporting opcode must never assume its slot exists; it
reads its cache-index field and treats `0xFFFF` as "no slot — run
uncached," identical to today's `cache = NULL` path (full resolution, no
populate). The two ways an index can be unset are indistinguishable at
runtime and handled by the same guard:

- **Gated off** — the whole code object has a zero-length array; every index
  is `0xFFFF`.
- **Non-consumer opcode** — an opcode whose flag is `0` never had an index
  assigned, so it is also `0xFFFF`.

Either way: `index == 0xFFFF` → uncached path.

#### The gate is two pragmas

The gate is driven by compiler pragmas, split by compile context because the
two contexts have opposite cost/benefit profiles:

| Pragma | Governs | Compile context | Baseline |
|--------|---------|-----------------|----------|
| `compiler/cache-expression` | Expression code objects | Top level of a module load or a REPL loading step | **off** |
| `compiler/cache-function` | Function/lambda code objects | Bodies of `lambda` and function forms nested inside a module or REPL compile | **on** |

The defaults follow evaluation frequency. Top-level expressions typically
run once as the module or REPL line loads, so their default is **off** — a
cache would never amortize. Function and lambda bodies are the reused code
that idiomatic method calls flow through, so their default is **on**.

Each is a `pragma_setting` in `koskari/compile/pragma/settings.py`
(`compiler/cache-expression` baseline `False`, `compiler/cache-function`
baseline `True`), resolved at compile time and consulted when the code
object is finalized to decide whether the count pass runs or short-circuits
to zero.

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


### Phase 3: Cache framework — DONE

Stand up the mechanism that guarantees a zeroed cache slot for any opcode
that asks for one. No opcode is switched on yet; every count is `0`, so the
framework is inert -- but the plumbing (flag, widened KosOp, slot
allocation, tagged-union slot, pragma gate) is fully in place.

- **DONE** `opcodes.incl`: a per-opcode cache-slot **count** after each name
  (a count from the start, not a bool, so an opcode may reserve a contiguous
  run), all `0`. `kos_opcode_cache_count(op)` accessor; `OP_3` / `OP_7` macro
  arities.
- **DONE** `KosOp`: widened to carry the 16-bit cache-index field; unset =
  `KOS_CACHE_UNSET` (`0xFFFF`). `KosOp_CACHE` accessor; sizing via
  `sizeof(KosOp)`, arena relocation via memcpy, soften still emits `(op, arg)`.
- **DONE** Assembly: `count_cache_slots` sums the per-opcode counts to size
  the object; `code_emit` assigns each cache-consuming site its slot run into
  `KosOp.cache` and verifies the total matches the allocation.
- **DONE** KosCode: variable-length object (`PyObject_VAR_HEAD`, `tp_itemsize`
  = `sizeof(KosCacheSlot)`), trailing slot array sized by the count and
  **zeroed at allocation** (`KOS_CACHE_EMPTY`); serializer ignores it;
  `code_dealloc` runs `cache_slot_release` (per-`kind` dispatch) over the
  slots. The `KosCacheSlot` tagged union and the bootstrap `KOS_CACHE_METHOD`
  layout are defined; no opcode populates them yet.
- **DONE** Gate: `compiler/cache-expression` (baseline off) and
  `compiler/cache-function` (baseline on) pragmas. The compiler branches on
  compile mode -- `CompilerMode.FUNCTION` follows cache-function, everything
  else (module/REPL top-level expressions) follows cache-expression -- and
  passes `cache=` to `Code(...)`. `KosCode_New` records it as the
  `KOS_CODE_CACHE` flag; when off, `count_cache_slots` is skipped (zero
  slots) and `code_emit` assigns none, so every index stays `0xFFFF`. The
  flag is introspectable as `code.caches`, and the allocated slot count as
  `code.cache_slots`. Like `protect`, the gate is **not serialized**: a
  `.kio` reload reconstructs `Code(...)` at the `cache=1` default, so
  functions keep caching and expressions regain it on reload (a footprint
  choice, never a correctness one).

GC-tracking is deliberately **not** part of the framework: while every slot
is `KOS_CACHE_EMPTY` the table holds no references, so KosCode keeps its
standing non-GC invariant. It moves to Phase 4, landing with the first
ref-holding layout.

### Phase 4: Activate consumers — DONE

Both cache families are active. Every consumer first checks `index == 0xFFFF`
and falls back to the uncached path (gated off, or opcode not enabled here);
on first use it checks `kind == KOS_CACHE_EMPTY`, populates its layout, then
sets `kind`.

- **Method cache** (`KOS_CACHE_METHOD`): the CALL_ATTR* family. Type + version
  guards, instance shadow check, the four modes plus a `MEGAMORPHIC` churn
  bypass after repeated misses.
- **Field cache** (`KOS_CACHE_FIELD`): `FAST_ATTR` / `VAR_ATTR` reads,
  `FAST_ATTR_STORE` / `VAR_ATTR_STORE` writes, and the augmented in-place family
  `ATTR_INPLACE` / `FAST_ATTR_INPLACE` / `VAR_ATTR_INPLACE`, per
  *Attribute-storage (field) cache* above. SLOT on all versions; INSTANCE_VALUE
  on 3.11-3.14 behind the compat hooks; ref-free, so it does not add to
  KosCode's GC surface.
- **Read-modify-write on one slot** (the in-place family, cache arity 1):
  `obj.attr OP= vals` resolves the field location *once* and uses it for both
  the accumulator load and the write-back, so a single cache slot serves the
  whole augmented assignment (the point of collapsing these into one opcode).
  The three variants differ only in how `obj` is obtained -- a fast slot
  (`FAST_`), a frame-namespace name (`VAR_`), or the stack (`ATTR_INPLACE`,
  which may be polymorphic and lean on the megamorphic bypass) -- and all route
  through the shared `kos_attr_inplace_cached` (eval_cache.c). The fold between
  load and store runs arbitrary user code (an `__iadd__` may retype `obj` or
  materialize its dict), so the direct store is gated by re-checking the slot's
  `field_guard_ok` against `obj`'s current type rather than re-resolving: a hit
  reuses the resolved offset (the guard's type+version invariant makes it
  valid), a miss falls back to `PyObject_SetAttr`. That re-guard -- not a second
  resolve -- is what keeps the raw `field_slot_store` off a stale offset. On a
  cache miss or when caches are gated off, the whole thing degrades to the
  generic `kos_attr_inplace_reduce` (getattr / fold / setattr).
- **GC-tracking of KosCode** landed with the method layout (the first
  ref-holding layout): KosCode is a GC type with `tp_traverse` / `tp_clear`
  that `switch (kind)` over the slots, matching `cache_slot_release`.
- **Version guard**: `kos_type_version` / `kos_type_version_assign`
  (version-gated) — `tp_version_tag` on 3.12+ (forced via
  `PyUnstable_Type_AssignVersionTag`, since PyType_Lookup only assigns it
  conditionally and 3.14 stops setting the old flag); the
  `Py_TPFLAGS_VALID_VERSION_TAG` flag through 3.11.
- **Tracing**: hit/miss/bypass counters under an `opcache` section of
  `get_trace_report()`, gated by `KOS_TRACE_OPCACHE`. `churn_method` and
  `churn_field` read hit rates directly; a bad guard shows up as a miss-rate
  anomaly rather than a silent perf mystery.


### Phase 5: Further cache families — OPEN (investigative)

The mechanism generalizes: a new family is a new `kind` plus a specialized
layout and a consumer. Candidates to evaluate (each its own investigation,
not committed): a global/builtin name cache (`LOAD_GLOBAL`-class opcodes), the
reserved `DICT_HINT` field mode (materialized-`__dict__` keys hint), and jump
or exception-type caches. See [TODO.md](../TODO.md) (*Opcode caching: further
families*).


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
| `kos_resolve_named_attr` | Method/callable lookup; cache management; returns resolved object (eval.c) |
| `kos_resolve_attr_location` | Attribute *location* lookup; returns `(mode, index)` (eval/eval_cache.c) |
| CALL_ATTR* opcodes | Stack layout; call `kos_resolve_named_attr`; invoke result |
| FAST/VAR_ATTR(_STORE) opcodes | Call `kos_resolve_attr_location`; direct read/write on SLOT / INSTANCE_VALUE, else generic |
| Cache slot | Tagged union: `kind` discriminant + per-kind specialized layout; `KOS_CACHE_EMPTY` (0) = unused |
| Method layout | `KOS_CACHE_METHOD`: type + type_version + mode + resolved (type and resolved owned) |
| Method modes | DATA_DESCRIPTOR, DIRECT_OBJECT_GUARDED, INSTANCE_DICT, GENERIC_LOOKUP, MEGAMORPHIC |
| Field layout | `KOS_CACHE_FIELD`: type (borrowed) + type_version + mode + misses + index; **ref-free** |
| Field modes | GENERIC, INSTANCE_VALUE (3.11+), SLOT (all versions), DICT_HINT (reserved), MEGAMORPHIC |
| Compat hooks | `kos_type_value_index` (shared scan, compat.h); `kos_type_has_inline_values` / `kos_inline_value_slots` (per-version, compat_3xx.h) |

The end.
