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

Because `Code` may reference constants and constants (via `CODE_REF` entries)
may reference `Code`, the global reference graph is a **DAG**:

* Cycles are **forbidden** by construction and validated at load time.
* A `CODE_REF` pool entry may point only "downward" in topological order;
  tuples must not contain themselves directly or transitively.

### 2.3 Canonicalization and interning

* Atom-like values (`symbol`, `keyword`) are interned on load via the runtime
  interning mechanism already provided by `koskari.lib`.
* Composite constants (tuples of allowed constants) are **canonicalized** so
  identical tuples become one pool entry.
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

Frozen VM structures (also allowed, stored in dedicated sections):

| Kind | Python type |
|------|-------------|
| `CODE_REF` | `Code` (stored in `CODEP` section; pool entry is a forward reference) |
| `NSPC_REF` | `NamespaceSpec` (stored in `NSPC` section) |
| `FRMS_REF` | `FormalsSpec` (stored in `FRMS` section) |

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

| Field | Type | Notes |
|-------|------|-------|
| `magic` | 8 bytes | `b"KIO\x00\x00\x00\x00\x00"` |
| `format_version` | u16 | bump on incompatible layout changes |
| `bytecode_abi` | 16 bytes | four little-endian `u32`: major, minor, patch, reserved |
| `finalize_pipeline_version` | u32 | bump when `complete()` semantics change |
| `struct_layout_versions` | u32 | bitfield: `NamespaceSpec`/`FormalsSpec`/`Code` record layout |
| `flags` | u32 | `FLAG_SOFTENED = 0x01`, `FLAG_HAS_DEBUG = 0x02`, `FLAG_COMPRESSED = 0x04` |
| `section_dir_offset` | u64 | byte offset to section directory |
| `section_dir_count` | u32 | number of section directory entries |

All integer fields use little-endian byte order (`<` in Python `struct`).

The `bytecode_abi` words are `BYTECODE_ABI_MAJOR`, `BYTECODE_ABI_MINOR`,
`BYTECODE_ABI_PATCH`, and `0` (reserved). They are maintained in
`koskari.serialize.constants` and incremented when opcode numbering or
softened CODEP encoding changes.

### 4.2 Section directory

Each entry:

| Field | Type | Notes |
|-------|------|-------|
| `section_tag` | 4 bytes | ASCII tag, e.g. `b"POOL"`, `b"NSPC"` |
| `offset` | u64 | byte offset from start of file |
| `length` | u64 | byte length of section |

### 4.3 Minimum required sections

| Tag | Contents |
|-----|----------|
| `POOL` | Constant pool records |
| `NSPC` | `NamespaceSpec` bodies |
| `FRMS` | `FormalsSpec` bodies |
| `CODEP` | Pre-finalize code bodies (softened form) |
| `META` | Cache invalidation metadata |

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
| `name_pool_idx` | u32 | pool index of `STR` constant for name, or `0xFFFFFFFF` |
| `filename_pool_idx` | u32 | pool index of `STR` constant for filename, or `0xFFFFFFFF` |
| `nspec_pool_idx` | u32 | pool index of `NSPC_REF` constant |
| `flags` | u32 | `KOS_CODE_YIELDS = 0x01` |
| `instr_count` | u32 | number of instruction records following |
| `label_count` | u32 | number of distinct label IDs used |

Then `instr_count` instruction records (§5.1).

### 5.5 Validation on load

`Compiler.complete()` enforces at load time:

* jump locality (branch targets resolve to valid label positions)
* extent derivation from matched `PUSH_*`/`POP_*` pairs
* consistent block tables
* stack-effect sanity checks

A finalize failure on load is treated as a hard cache miss — the `.kio` file
is rejected and the source is recompiled.

## 6. Cache invalidation metadata (META section)

### 6.1 Hard invalidators — must reject on mismatch

| Field | Type | Notes |
|-------|------|-------|
| `format_version` | u16 | must match header |
| `bytecode_abi` | 16 bytes | must match interpreter (four `u32`: major, minor, patch, reserved) |
| `finalize_pipeline_version` | u32 | must match |
| `struct_layout_versions` | u32 | must match |

Any mismatch causes immediate rejection of the `.kio` file.

### 6.2 Soft invalidators — policy-controlled

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

1. Read the header; verify all hard invalidators (§6.1). Reject on any
   mismatch.
2. Apply soft invalidator policy (§6.2); reject if policy requires freshness.
3. Read `POOL` records sequentially. Do not fully materialize composite
   entries yet — record their field indices.
4. Materialize atom-like primitives: `NIL`, `TRUE`, `FALSE`, `NONE`, `INT`,
   `FLOAT`, `COMPLEX`, `STR`, `BYTES`, `SYMBOL` (interned), `KEYWORD`
   (interned).
5. Read `NSPC` section; build `NamespaceSpec` objects and insert them at
   their pool positions (`NSPC_REF` entries).
6. Read `FRMS` section; build `FormalsSpec` objects and insert them at their
   pool positions (`FRMS_REF` entries).
7. Read each code body from `CODEP`; reconstruct a `ByteCodeFragment`-
   equivalent instruction list.
8. Run `Compiler.complete()` (or an equivalent finalize path) for each code
   body to produce a hardened `Code` object. A finalize failure is a hard
   cache miss.
9. Insert each `Code` into its `CODE_REF` pool positions.
10. Materialize remaining composite constants: `TUPLE` and `TAGTUP` entries,
    now that all referenced pool entries exist.
11. Optionally apply the plugin materialization stage for `TAGTUP` entries
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
| Allowed constant types | `CONST_TYPES` in `koskari/compile/compiler.py` |
| Symbol/keyword interning | `koskari.lib.symbol`, `koskari.lib.keyword` |

The 16-byte `bytecode_abi` field in the header and META hard invalidators is
the packed form of `BYTECODE_ABI_MAJOR`, `BYTECODE_ABI_MINOR`,
`BYTECODE_ABI_PATCH`, and a reserved zero word (`struct.pack('<IIII', ...)`
in `koskari.serialize.constants`). Bump those integers when `KosOpCode`
values or softened-instruction semantics change. The installed package does
not need to ship `opcodes.incl` for this check.
