# Extra Table and Opcode Metadata Design

This document describes the design for adding an **extra operand table** to
Koskari's finalized bytecode in-memory structure, and augmenting the opcode
style system with a new **EXTRA** style. This enables selected opcodes to store
up to four structured operands rather than a single inline `arg`, without
introducing variable-length instructions or globally widening every opcode.

**Status:** Design — not yet implemented.

**See also:** [bytecode-metadata-tables.md](../interpreter/bytecode-metadata-tables.md)
(constant pool, extents, code object layout), [bytecode-finalization.md](../interpreter/bytecode-finalization.md)
(collect/size/allocate/emit pipeline, cpool from EXTRA CONST/VAR), [OPCODES.md](../OPCODES.md)
(opcode reference), [call-fast-opcodes.md](../archive/call-fast-opcodes.md)
(archived: superseded by EXTRA-based CALL_VAR\* / CALL_FAST\* opcodes).

---

## 1. Goal

Preserve a fixed-width opcode stream while allowing selected opcodes to carry
richer operand metadata than can fit in the existing inline `arg` field.

This avoids:

- Variable-length instructions
- Globally widening every opcode

While enabling:

- Fused or metadata-heavy operations (e.g. specialized call forms such as
  `CALL_FAST_ATTR_NAMES` that combine fast-slot index, attribute constant,
  keyword-names constant, and argument count)

---

## 2. Extra Table Concept

### 2.1 Base instruction format unchanged

The finalized opcode stream remains:

```c
typedef struct KosOp {
  uint16_t op;
  uint16_t arg;
} KosOp;
```

### 2.2 New extra operand table

Add a second finalized table for extended operands:

```c
typedef struct KosExtra {
  uint16_t a0;
  uint16_t a1;
  uint16_t a2;
  uint16_t a3;
} KosExtra;
```

### 2.3 Semantics

- **Ordinary opcodes:** `arg` retains its current meaning (count, const index,
  var index, label-resolved pc, etc.).
- **EXTRA opcodes:** `arg` becomes an index into `KosExtra[]`. The opcode
  definition determines how `a0..a3` are interpreted. Unused fields are zero.

### 2.4 Benefits

- Fixed-width code stream remains intact.
- Common opcodes stay compact.
- Only selected opcodes pay the cost of expanded operands.
- Enables fused operations that need 2 to 4 internalized fields.
- Compiler/finalizer remains table-driven instead of per-op handwritten.

---

## 3. `opcodes.incl` Changes

### 3.1 OP macro usage

The existing model remains structurally the same:

```c
OP(NAME, STYLE)
```

The change is that `STYLE` is expanded to cover both ordinary inline styles and
encoded extra schemas.

### 3.2 New convention

Keep the `OP` macro at two arguments. Examples:

```c
OP(ATTR, CONST)
OP(ATTR_CLEAR, CONST)
OP(ITEM, NOARG)
OP(CONST_ITEM, CONST)
OP(CALL_FAST_ATTR_NAMES, EXTRA_FAST_CONST_CONST_COUNT)
```

### 3.3 Meaning

- **Ordinary styles** remain as they are: `NOARG`, `CONST`, `VAR`, `COUNT`,
  `LABEL`, `PAIR_COUNT`, `STAR`, `FASTVAR`, `COMPILE`.
- **Extended forms** are represented by style names beginning with `EXTRA_`.
- Those `EXTRA_*` names encode both:
  1. That the opcode is an `EXTRA` opcode.
  2. The four-field expansion schema used to internalize the incoming tuple
     argument.

At runtime, during module initialization / metadata table creation in C, the
style token is interpreted once:

- Ordinary styles map directly to their existing inline style handling.
- `EXTRA_*` styles are decoded into:
  - Primary style = `EXTRA`
  - Extra-field schema = the four field kinds encoded in the name

This preserves the existing two-argument `OP(...)` declaration shape while
allowing per-op extra metadata.

### 3.4 Why this fits the current system

Because opcode metadata is generated once during runtime C initialization, it
is acceptable to parse or decode the `EXTRA_*` symbolic style into the base
style classification and the extra tuple schema metadata. That avoids changing
the macro signature while preserving a single-source declarative opcode table.

---

## 4. EXTRA as a Style

Add a new opcode style:

```c
KOSTYLE_EXTRA
```

### 4.1 Finalization rule

When an opcode's style is `EXTRA`:

1. The incoming Python-side opcode argument is expected to be a **tuple**.
2. That tuple is internalized field-by-field according to the opcode's
   declared extra schema.
3. The resulting `KosExtra` row is appended (or interned) in the code
   object's extra table.
4. The finalized opcode's inline `arg` becomes the index of that
   `KosExtra` row.

### 4.2 Example

**Python-side pre-final form:**

```python
(OpCode.CALL_FAST_ATTR_NAMES,
 (obj_fast, attr_name, kw_names_tuple, argc))
```

**Finalized result:**

- opcode = `CALL_FAST_ATTR_NAMES`
- arg = index into `co->extra[]`
- `co->extra[arg] = { fast_index, attr_const, names_const, count }`

### 4.3 Intended use

`EXTRA` should be used selectively for opcodes that genuinely benefit from
multiple structural operands, especially fused call and attribute-access forms.
It should not become a generic escape hatch for arbitrary complicated argument
handling.

---

## 5. Extra Field Expansion Metadata

The current opcode metadata only tells finalization how to interpret the main
argument. For `EXTRA`, that is not enough. The finalizer must also know how to
interpret each field of the incoming tuple.

### 5.1 Proposed addition

Associate each opcode with an additional derived metadata value representing its
extra expansion schema.

Conceptually, each opcode still declares only one `STYLE` token in
`opcodes.incl`, but during metadata construction that token is split into:

- Primary style classification
- Optional extra tuple schema

For ordinary opcodes:

- `STYLE` is an ordinary style (e.g. `CONST`, `NOARG`, `COUNT`).
- No extra schema is attached.

For extended opcodes:

- `STYLE` is an `EXTRA_*` symbolic style.
- Metadata construction derives:
  - Primary style = `EXTRA`
  - Extra schema = the four-field expansion encoded by that symbolic style

### 5.2 Schema examples

```c
NONE
FAST_CONST_CONST_COUNT
CONST_COUNT
FAST_CONST_COUNT
LABEL_COUNT
```

Each schema is interpreted as four field encodings. For example:

```c
FAST_CONST_CONST_COUNT  =>  { FAST, CONST, CONST, COUNT }
```

Unused slots are padded with `NONE`.

### 5.3 Field encoding types

The per-slot field kinds reuse the same internalization logic already used by
inline styles:

| Kind   | Internalization behavior                          |
|--------|---------------------------------------------------|
| NONE   | Unused; value 0                                   |
| COUNT  | Integer, range-validated                          |
| CONST  | Intern into constant pool, store index            |
| VAR    | Stringify, intern into constant pool, store index |
| FAST   | Validate as fast-local index                      |
| LABEL  | Resolve to pc in second pass                      |

This means the extra-tuple internalizer can remain table-driven and reuse
existing conversion logic (e.g. `collect_const`, label resolution).

---

## 6. Resulting Metadata Model

The opcode declaration file (`opcodes.incl`) remains the single source of truth
for:

- Opcode enum generation
- Dispatch table generation
- Style table generation
- Derived extra-schema table generation

Example pattern:

```c
OP(ATTR, CONST)
OP(ITEM, NOARG)
OP(CALL_FAST_ATTR_NAMES, EXTRA_FAST_CONST_CONST_COUNT)
```

This can generate or derive:

- `enum KosOpCode`
- `kos_op_style[]` (or `opcode_styles[]`)
- `kos_op_extra_schema[]`
- Optional printable name tables

Where `kos_op_extra_schema[]` is computed from the symbolic style:

- Ordinary styles produce no extra schema (or a sentinel indicating none).
- `EXTRA_*` styles produce the associated four-field schema.

---

## 7. Finalization Model

### 7.1 Non-EXTRA opcodes

Finalize exactly as today based on `STYLE`:

- `LABEL` resolves to pc.
- `CONST` interns into constant pool.
- `VAR` validates string and interns into variable table.
- `COUNT` validates integer range.
- `FASTVAR` validates fast-local index.
- etc.

### 7.2 EXTRA opcodes

Finalize by:

1. Reading the opcode's declared extra schema.
2. Validating the Python-side argument is a tuple of matching arity.
3. Internalizing each tuple element according to the schema field kinds
   (reusing existing logic: `collect_const` for CONST/VAR, label dict for
   LABEL, range check for COUNT/FAST).
4. Writing or interning a `KosExtra` row.
5. Storing the extra-table index into the opcode's inline `arg`.

### 7.3 Two-pass consideration

If the schema includes `LABEL`, label resolution must occur in the same
second-pass as other label-style opcodes. The extra table row is populated in
the first pass with a placeholder for the label slot; the second pass rewrites
that slot with the resolved pc.

---

## 8. Code Object Layout

### 8.1 KosCode additions

The `KosCode` structure (or equivalent code object) gains:

- `uint32_t extra_len` — number of `KosExtra` rows.
- `KosExtra *extra` — pointer to the extra table (or NULL if none).

### 8.2 Storage allocation

The extra table is analogous to the constant pool and extent table. Allocation
options:

- **Option A:** Include `KosExtra` rows in the same contiguous allocation as
  code, positions, and constant pool. The layout becomes:
  `[code][padding][positions][padding][cpool][padding][extra]`.
- **Option B:** Allocate the extra table in the extent block when both exist, or
  as a separate small allocation when the code has EXTRA opcodes but no
  extents.
- **Option C:** Separate allocation for `extra` when `extra_len > 0`.

The choice affects `code_alloc_storage` (or equivalent) and read-only
protection (`mprotect`). The constant pool and extent table are already in
separate allocation paths; the extra table should follow the same policy for
consistency.

### 8.3 Accessor

An accessor macro or function is needed for opcode handlers:

```c
#define KosCode_GetExtra(c, idx)  ((KosExtra *) (c)->extra)[idx]
```

Or a function that returns a pointer to the `KosExtra` row for bounds checking.

---

## 9. Disassembly and Introspection

### 9.1 Disassembler

The disassembler (`koskari.dis`, or C equivalent) must handle `KOSTYLE_EXTRA`:

- When style is `EXTRA`, `arg` is an index into the extra table.
- Load `co->extra[arg]` and format `a0..a3` according to the opcode's extra
  schema (e.g. show constant pool repr for CONST slots, pc for LABEL slots).

### 9.2 Code.constant / oparg API

If the Python `Code` type exposes `oparg(i)` or similar, EXTRA opcodes may need
to expose the extra row as a tuple of decoded values for consistency with
pre-finalize representation. Alternatively, the raw `(op, arg)` pair can be
returned with `arg` as the extra index, and a separate accessor can return the
decoded extra row when applicable.

---

## 10. KIO / Serialization

If the `.kio` cache format or any bytecode serialization includes a hardened
finalized instruction stream, it must also persist the extra table:

- `extra_len` and `extra[]` for each code body.
- Cache invalidation should include a version bump if the extra table format or
  schema semantics change.

The CODEP (softened) representation may use a tuple as the argument for
EXTRA opcodes, with `arg_kind` extended (e.g. `ARG_EXTRA_TUPLE`) or with a
convention that certain opcodes expect a tuple in an existing arg slot.

---

## 11. Design Constraints and Invariants

- Base opcode stream remains fixed width.
- `KosExtra` rows are fixed width: always four `uint16_t`.
- Unused `KosExtra` fields are zero.
- Extended opcodes must use an `EXTRA_*` style token.
- Ordinary opcodes must use a non-`EXTRA_*` ordinary style token.
- Finalized opcode `arg` for EXTRA must be a valid `KosExtra` index for that
  code object.
- Incoming Python tuple for EXTRA must match the declared schema arity and
  shape (e.g. CONST expects a value to intern, LABEL expects a label string).
- The extra table may be empty (`extra_len == 0`, `extra == NULL`) for code
  that has no EXTRA opcodes.

---

## 12. Summary

This design extends the current table-driven opcode system without changing the
fixed-width instruction stream. The key changes are:

1. Add a per-code-object extra operand table of fixed four-`uint16_t` rows.
2. Keep `OP(...)` in `opcodes.incl` at two fields: name and style.
3. Add `EXTRA_*` symbolic styles to represent extra-backed opcode forms.
4. During runtime C metadata construction, decode each `EXTRA_*` style into:
   - Base style `EXTRA`
   - A four-field extra expansion schema
5. Internalize Python tuple arguments for EXTRA opcodes into `KosExtra` rows
   using that derived schema.

This gives Koskari a disciplined way to support richer fused opcodes while
preserving simple decoding and compact ordinary instructions.
