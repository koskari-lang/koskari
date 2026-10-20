# Koskari Compiled Cache Format (.kio)

This document specifies the `.kio` file format, a **cached compiled** artifact
produced from a Koskari source file (`.ki` → `.kio`). Because `.kio` is a cache,
it may be safely rejected and regenerated from source at any time.

## 0. Two candidate serialization levels

### A. Hardened (post-finalize) serialization

Serialize the final `Code` object exactly as it is executed:

* `LABEL`/`POSITION` pseudo-ops stripped
* branch targets resolved to program counter (pc) indices
* `VAR` opcodes lowered to `FAST` where applicable
* position and extent tables fully derived

**Pros:** fastest load, smallest file.  
**Cons:** couples serialization to every optimizer detail and extent-derivation
invariant; harder to validate or upgrade across compiler versions.

### B. Softened (pre-finalize) serialization — recommended

Serialize a *stable intermediate form* close to `ByteCodeFragment` output — the
pre-`complete()` instruction stream:

* `LABEL` and `POSITION` pseudo-ops present
* branch targets refer to label IDs, not pc values
* `VAR`/`VAR_BIND`/etc. present; no `FAST`/`FAST_*` emitted
* constants still in a global pool (see §2), code arguments may reference pool
  indices or inline labels

On load, run the **existing `Compiler.complete()` pipeline** (`compress` →
`convert_fast` → `Code(...)`) to:

* resolve labels and strip `LABEL`/`POSITION` into position/extent tables
* perform jump-locality validation
* derive the extent table from `PUSH_*`/`POP_*` pairs
* optionally lower `VAR` → `FAST`
* produce a hardened, mmap-backed `Code`

**Pros:** reuses the existing compiler safety net; forward-compatible; less
brittle under opcode set changes.  
**Cons:** load is slightly slower; file is marginally larger.

## 1. Goals

* **Correctness-first for cache**: prefer safe validation over maximal load
  speed.
* **Deterministic**: stable bytes given stable inputs.
* **No arbitrary deserialization**: the constant pool is restricted to a
  well-defined set of types (see §3).
* **Reuse existing hardening**: `.kio` load feeds the same `complete()` path
  as the compiler; no parallel finalization logic is maintained.

## 2. Object graph model

### 2.1 One module-global constant pool

The serialized artifact provides a *single, module-global constant pool*. All
other objects reference it exclusively.

* `Code`, `NamespaceSpec`, and `FormalsSpec` are **composed objects** whose
  fields are only references (pool indices, table offsets) into the pool.
* Composite constants such as tuples are themselves pool entries; their
  elements are referenced by pool index.
* This ensures all constants bottom out in atomic primitives and that the
  entire constant graph is addressable from one table.

### 2.2 DAG constraint — no cycles

Because `Code` may reference constants and a `FUNCTION_SPEC` constant embeds
its own code body, the global reference graph is a **DAG**:

* Cycles are **forbidden** by construction and validated at load time.
* A pool entry may reference only entries written before it; tuples must
  not contain themselves directly or transitively. `CODE_REF` is a reserved
  kind that nothing writes or reads.

### 2.3 Canonicalization and interning

* Atom-like values (`symbol`, `keyword`) are interned on load via the runtime
  interning mechanism already provided by `koskari.lib`.
* Deduplication is by `pool_key`: `int`, `str`, `bytes`, `float` and
  `complex` values dedupe by value; every other value, composites included,
  dedupes by identity, so two equal tuples are two pool entries.
* Pool indices are assigned in a stable, deterministic order during finalize
  so identical compilations produce identical bytes.

## 3. Constant pool policy

### 3.1 Allowed constant kinds

Minimal set:

| Kind | Python type |
|------|-------------|
| `NIL` | `nil` singleton |
| `TRUE` / `FALSE` | `bool` |
| `NONE` | `None` |
| `INT` | Python `int` (bignum) |
| `FLOAT` | Python `float` |
| `COMPLEX` | Python `complex` |
| `STR` | `str` (UTF-8) |
| `BYTES` | `bytes` |
| `SYMBOL` | `symbol` (interned on load) |
| `KEYWORD` | `keyword` (interned on load) |
| `TUPLE` | tuple of allowed constants |
| `BRACKET_NIL` | the bracket-nil singleton |
| `ELLIPSIS` | `Ellipsis` |
| `FROZENSET` | frozenset of allowed constants |
| `SLICE` | `slice` of allowed constants |

Frozen VM structures (encoded as pool records; see `PoolKind` in `koskari.serialize.codex`):

| Kind | Python type |
|------|-------------|
| `CODE_REF` | reserved; nothing writes or reads it |
| `NAMESPACE_SPEC` | `NamespaceSpec` |
| `FORMALS_SPEC` | `FormalsSpec` |
| `FUNCTION_SPEC` | `FunctionSpec` (embeds a softened instruction blob) |

### 3.2 Tagged tuples for complex constants

**Problem:** a plain tuple such as `(:uuid "…")` is ambiguous — it might be
user data, not an encoded object.

**Decision:** introduce a **distinct constant kind**: `TaggedTuple`.

* A `TaggedTuple` is **not** a normal tuple.
* It carries an explicit `tag` (a `keyword` or `symbol`) and a tuple payload
  of fields.
* The loader detects `TaggedTuple` reliably without inspecting contents.

#### 3.2.1 On-disk representation

A pool entry of kind `TAGTUP` contains:

* `tag_index` (u32) — pool index of a `KEYWORD` (or `SYMBOL`) entry
* `payload_tuple_index` (u32) — pool index of a `TUPLE` entry holding the
  fields

`TUPLE` remains a plain tuple; `TAGTUP` wraps it and asserts "this is an
encoded domain value."

#### 3.2.2 Runtime behavior

`TaggedTuple` is immutable and tuple-like:

* iterable and indexable like a tuple
* exposes `.tag` and `.fields` (or `.payload`) as explicit attributes
* does **not** auto-materialize — the runtime value is the `TaggedTuple`
  itself unless an explicit materialization step is invoked

#### 3.2.3 Plugin-aware materialization (optional)

A separate, opt-in stage may map `TaggedTuple(tag, fields)` to richer runtime
objects via a plugin registry:

* Unknown tags are preserved as `TaggedTuple` (fail-soft).
* Known tags may materialize into domain objects (e.g. UUID, regex, path).
* Materialization is:
  * opt-in / policy-gated
  * deterministic (same inputs → same outputs)
  * not required for correct cache loading

This preserves cache portability and avoids making load depend on plugin
availability.

### 3.3 Disallowed

* Arbitrary Python objects
* `pickle` / `marshal`
* Anything requiring import-time code execution to rehydrate

## 4. File structure (binary)

A section-directory layout allows the format to evolve independently of any
single section.

### 4.1 Header

Fixed **40-byte** prefix. Layout: `struct.Struct('<8sIIIIIQI')`.

| Field | Type | Notes |
|-------|------|-------|
| `magic` | 8 bytes | `b"KIO\x00\x00\x00\x00\x00"` |
| `abi_major` | u32 | semantic bytecode ABI (see below) |
| `abi_minor` | u32 | |
| `abi_micro` | u32 | |
| `abi_patch` | u32 | |
| `flags` | u32 | reserved; writers emit `0` until individual bits are defined |
| `section_dir_offset` | u64 | byte offset to section directory |
| `section_dir_count` | u32 | number of section directory entries |

All integer fields use little-endian byte order (`<` in Python `struct`).

Compatibility is defined by the four ABI fields together. They are
`BYTECODE_ABI_MAJOR`, `BYTECODE_ABI_MINOR`, `BYTECODE_ABI_MICRO`, and
`BYTECODE_ABI_PATCH` in `koskari.serialize.constants`. Bump them when opcode
numbering or softened CODEP encoding changes.

### 4.2 Section directory

Each entry:

| Field | Type | Notes |
|-------|------|-------|
| `section_tag` | 4 bytes | ASCII tag, e.g. `b"POOL"`, `b"CODE"`, `b"META"` |
| `offset` | u64 | byte offset from start of file |
| `length` | u64 | byte length of section |

### 4.3 Required sections

| Tag | Contents |
|-----|----------|
| `POOL` | Constant pool records |
| `CODE` | Pre-finalize code bodies (softened form) |
| `META` | Optional cache metadata blob (length may be zero) |

`NamespaceSpec` and `FormalsSpec` values live in the pool (`NAMESPACE_SPEC` /
`FORMALS_SPEC` records), not in separate section bodies.

### 4.4 Optional sections

| Tag | Contents |
|-----|----------|
| `CODEH` | Post-finalize (hardened) code bodies |
| `INST` | Raw instruction blobs for `CODEH` |
| `POS` | Position tables for `CODEH` |
| `EXT` | Extent tables for `CODEH` |

## 5. Softened code representation (CODEP)

The `CODEP` section stores a pre-finalize instruction list compatible with
what `ByteCodeFragment.instructions` holds before `Compiler.complete()` is
called. Finalize is re-run at load time.

### 5.1 Instruction stream

Each instruction record:

| Field | Type | Notes |
|-------|------|-------|
| `opcode` | u16 | `KosOpCode` enum value |
| `arg_kind` | u8 | `ARG_NONE=0`, `ARG_U32=1`, `ARG_LABEL_ID=2`, `ARG_POOL_INDEX=3` |
| `_pad` | u8 | reserved, zero |
| `arg_value` | u32 | meaning depends on `arg_kind` |

`arg_kind` encoding:

* `ARG_NONE` — `arg_value` is zero and ignored (style `NOARG`)
* `ARG_U32` — `arg_value` is a direct unsigned integer (style `COUNT`,
  `PAIR_COUNT`, `STAR`)
* `ARG_LABEL_ID` — `arg_value` is a label identifier within this code body
  (style `LABEL`, `COMPILE`/`LABEL` pseudo-op)
* `ARG_POOL_INDEX` — `arg_value` is an index into the module-global constant
  pool (style `CONST`, `VAR`, `FASTVAR` not used here)

### 5.2 Labels and positions in CODEP

`LABEL` pseudo-ops appear inline in the stream:

```
opcode=KOS_OP_LABEL  arg_kind=ARG_LABEL_ID  arg_value=<label_id>
```

`POSITION` pseudo-ops appear inline:

```
opcode=KOS_OP_POSITION  arg_kind=ARG_U32  arg_value=<packed_linecol>
```

where `packed_linecol` is `(line << 16) | col` (both u16). Alternatively a
separate position pool section may be used; the packed form is preferred for
simplicity.

Finalize strips both kinds and derives the compact `KosPosition` delta table.

### 5.3 No FAST lowering in CODEP

`CODEP` uses only name-based variable forms:

* `VAR`, `VAR_BIND`, `VAR_CLEAR`, `VAR_REF`, `VAR_STORE`

`FAST` and `FAST_*` opcodes are **never** stored in `CODEP`. `convert_fast()`
in `Compiler.complete()` will produce them on load.

### 5.4 Code body record header

Each code body in `CODEP` begins with:

| Field | Type | Notes |
|-------|------|-------|
| `name_pool_idx` | u32 | pool index of the `STR` name, or of the `NONE` entry when unnamed |
| `filename_pool_idx` | u32 | pool index of the `STR` filename, or of the `NONE` entry |
| `nspec_pool_idx` | u32 | pool index of a `NamespaceSpec` (pool `NAMESPACE_SPEC` record) |
| `flags` | u32 | `YIELDS = 0x01`, `PROTECT = 0x02`, `CACHE = 0x04` (`CodeFlags`): how the Code was constructed, fed back to the constructor on load; a `FUNCTION_SPEC` record carries the same word |
| `instr_count` | u32 | number of instruction records following |

Then `instr_count` instruction records (§5.1).

### 5.5 Validation on load

`Compiler.complete()` enforces at load time:

* jump locality (branch targets resolve to valid label positions)
* extent derivation from matched `PUSH_*`/`POP_*` pairs
* consistent block tables
* stack-effect sanity checks

The only fallback to source runs before any decoding: `source_for_stale_kio`
yields to the sibling `.ki` when the META stamp is missing or does not match
the source's mtime and size, or when the header is unreadable or carries the
wrong ABI. Once a `.kio` is chosen, a decode or construction error propagates
as an import failure; nothing recompiles.

## 6. Cache invalidation metadata (META section)

Hard validation (magic and bytecode ABI) happens in the **file header** (§4.1)
only. The `META` body (`KioMeta`) leads with a flag byte: bit `0x01`
(`STAMPED`) says a source stamp follows, the source's `mtime_ns` (i64) and
size (u64) as the `.ki` was when compiled. Any bytes after that are
`soft_meta`, opaque to the runtime. A length of zero is valid: no stamp, no
soft metadata. The loader compares the stamp against a sibling `.ki` and
yields to the source when they differ or when no stamp was recorded (§5.5).

### 6.1 Soft invalidators — policy-controlled (not yet fixed layout)

| Field | Type | Notes |
|-------|------|-------|
| `source_digest` | 32 bytes | SHA-256 of source file bytes |
| `source_mtime` | i64 | source file mtime (nanoseconds) |
| `compiler_digest` | 32 bytes | hash of compiler version / config |
| `macro_dep_count` | u32 | number of macro dependency entries |
| `macro_dep_entries` | repeated | `(name_str_len u32, name utf-8, digest 32 bytes)` |

Soft invalidators trigger recompilation by policy but do not indicate a
corrupt or invalid file format.

## 7. Packing and unpacking

All binary encoding uses `struct` with explicit little-endian byte order
(`<`).

### 7.1 Fixed-width scalars

Standard `struct` types: `u8 = B`, `u16 = H`, `u32 = I`, `u64 = Q`,
`i64 = q`.

### 7.2 Variable-length integers (Python bignum)

```
nbytes  : u32
payload : nbytes bytes, two's-complement big-endian
          (int.to_bytes(nbytes, byteorder='big', signed=True))
```

### 7.3 Strings and bytes

```
strings : u32 length + UTF-8 bytes
bytes   : u32 length + raw bytes
```

### 7.4 Tuples

```
u32 arity + u32 pool_index[arity]
```

## 8. Loader algorithm (softened)

1. Read the header; verify `magic` and bytecode ABI (§4.1). Reject on any
   mismatch.
2. Read the section directory; locate `POOL`, `CODE`, and `META` offsets.
3. Apply soft invalidator policy on `META` when implemented (§6.1); reject if
   policy requires freshness.
4. Decode the `POOL` section (`koskari.serialize.codex.decode_pool_records`):
   multi-pass materialization of tuples, `NamespaceSpec`, `FormalsSpec`,
   embedded `FunctionSpec`, and related records.
5. Decode each record in the `CODE` section into softened instructions using
   the pool for operand resolution.
6. Run `Compiler.complete()` (or an equivalent finalize path) for each top-level
   code body to produce a hardened `Code` object. A failure here propagates as
   an import error (§5.5).
7. Optionally apply the plugin materialization stage for `TAGTUP` entries
   (§3.2.3), gated by caller policy.

## 9. Tradeoffs and guidance

* For `.kio` (source cache): use softened serialization. It is safer, more
  evolvable, and reuses the existing finalize safety net.
* For `.kbo` (built artifact, future): hardened serialization makes sense —
  fast load, minimal transform, stable deployment artifact.
* Both can coexist: store `CODEP` always; add `CODEH`/`INST`/`POS`/`EXT` as
  an optimization to skip re-finalization when the pipeline version matches.

## 10. Relationship to existing code

| Concept | Current location |
|---------|-----------------|
| Instruction list | `ByteCodeFragment.instructions` (list of `(OpCode, arg)`) |
| Finalize pipeline | `Compiler.complete()` → `compress()` + `convert_fast()` + `Code(...)` |
| Opcode set | `koskari/lib/opcodes.incl` (X-macro table) |
| `Code` C type | `KosCode` in `koskari/lib/bytecode.h` |
| `NamespaceSpec` C type | `KosNamespaceSpec` in `koskari/lib/namespace.h` |
| `FormalsSpec` C type | `KosFormalsSpec` in `koskari/lib/formals.h` |
| Allowed constant types | `CONST_TYPES` in `koskari/types.py` |
| Symbol/keyword interning | `koskari.lib.symbol`, `koskari.lib.keyword` |

The header carries four ABI `u32` fields (`BYTECODE_ABI_MAJOR` through
`BYTECODE_ABI_PATCH` in `koskari.serialize.constants`). Bump them when
`KosOpCode` values or softened-instruction semantics change. The installed
package does not need to ship `opcodes.incl` for this check.
