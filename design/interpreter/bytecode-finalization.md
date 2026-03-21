# Bytecode Final Form Phase — Design Proposal

This document proposes an improved **finalization** pipeline that transforms the
compiler's pre-compile instruction stream into the sealed, read-only bytecode
format stored in KosCode. It addresses allocation sloppiness, introduces the
extra table, unifies storage into a single block, and uses a disciplined
multi-phase approach with precise sizing.

**Status:** Design proposal — not yet implemented.

**See also:** [bytecode-metadata-tables.md](bytecode-metadata-tables.md)
(extent table, layout), [extra-table-and-opcode-metadata.md](../api/extra-table-and-opcode-metadata.md)
(EXTRA opcodes, schema), [OPCODES.md](../OPCODES.md) §1.7 (pre-compile vs
post-compile).

---

## 1. Scope and Input

**Input:** The pre-compile instruction stream from `Compiler.complete()` (or
equivalent). A sequence of `(opcode, arg)` pairs where:

- Labels are string names
- Constants appear as literal Python values
- Variable names as strings or symbols
- LABEL and POSITION opcodes are present
- EXTRA opcodes (when present) have tuple arguments

**Output:** A KosCode with a single read-only allocation containing code,
positions, constant pool, extra table, extent table, and top_extent_id. All
indices resolved, all tables precisely sized, no slack.

**Out of scope:** The compiler proper (lowering, special forms, etc.). This
design covers only the **final form** phase: turning already-compiler-processed
opcodes into sealed bytecode.

---

## 2. Diagnosis of Current Pattern

### 2.1 Current flow (code_instr_init + process_instr)

1. **First pass (count):** Walk input instructions once. Count:
   - `const_count` = number of CONST or VAR style opcodes
   - `pos_count` = number of POSITION opcodes
   - No counting of LABEL (needed for output length), no EXTRA, no extent count

2. **Allocation:** Single block for `[code][positions][cpool]` using:
   - `code` size = input instruction count (oversized; output is smaller)
   - `pos` size = every POSITION (oversized; only position *changes* stored)
   - `cpool` size = every CONST/VAR use (oversized; de-duplication reduces usage)

3. **process_instr (single combined pass):**
   - Iterate input, write to `code[di]` (di advances only for non-squished ops)
   - LABEL: register in labels dict, overwrite slot with NOOP, do not advance di
   - POSITION: unpack (line,col), store only when changed, overwrite with NOOP
   - CONST: `collect_const(cpool, *const_l, b)` — identity-based dedup
   - VAR: stringify, then `collect_const`
   - LABEL-style: store (di, label_name) for second-pass resolution
   - Default: copy arg as integer
   - Second pass: resolve LABEL-style args to pcs via labels dict

4. **Post-process:** `count_const` to find first NULL → shrinks logical
   `const_count`; `*instr_l = di`, `*pos_l = pi` shrink logical lengths. **The
   allocation is not shrunk** — slack remains in all three tables.

5. **derive_extents:** Separate allocation for extent table. Fills extents and
   top_extent_id from finalized code.

### 2.2 Allocation sloppiness (current)

| Table    | Allocated for              | Actually used                  | Slack                            |
|----------|----------------------------|--------------------------------|----------------------------------|
| code     | input instruction count    | output count (after squish)    | LABEL + POSITION slots           |
| positions| every POSITION op          | only when (line,col) changes   | duplicate / unchanged positions  |
| cpool    | every CONST/VAR use        | unique constants (identity)    | duplicates, often substantial    |
| extents  | separate allocation        | N/A                            | N/A (separate block)             |

### 2.3 Constant pool deduplication constraint

**Critical:** De-duplication must use **object identity**, not hash/equality.

- `1 is True` is False, but `1 == True` is True. If we used a dict keyed by
  value, `1` and `True` would collide.
- `0` and `False` likewise.
- The constant pool must preserve distinct identity: `const[0]` and `const[1]`
  can be `1` and `True` respectively.

**Current approach:** `collect_const` uses a pre-allocated array and linear
scan. For each new constant: walk existing slots; if `cpool[i] == value`
(pointer equality in C → identity), return that index; else find first NULL,
store, return. Identity-safe.

**Rejected:** `PyDict` or any hash-based structure. Hash and equality would
merge `1`/`True` and `0`/`False`. Use a **linear structure** (list or array)
and walk it for identity match.

### 2.4 EXTRA opcodes and the constant pool

EXTRA opcodes have a tuple argument. Per the extra-table design, each field is
typed by the opcode's extra schema (NONE, CONST, VAR, FAST, COUNT, LABEL).

- **CONST** and **VAR** fields must be interned into the **same** constant pool
  as inline CONST/VAR. A CALL_FAST_ATTR_NAMES with `(slot, attr_name, kw_names,
  argc)` has CONST for `attr_name` and `kw_names` — both become cpool indices.
- When collecting constants for sizing, we must **also** walk EXTRA opcodes and
  their schema: any CONST or VAR field contributes to the constant pool.

---

## 3. Proposed Pattern: Collect → Size → Allocate → Emit → Finalize

### 3.1 Phase overview

| Phase    | Purpose                                             | Output                         |
|----------|-----------------------------------------------------|--------------------------------|
| Collect  | Build intermediate structures, intern constants      | Sizes, maps, temp cpool        |
| Allocate | Single unified block with exact sizes                | Raw block, sub-region ptrs     |
| Emit     | Write instructions and tables into block             | Filled block                   |
| Finalize | derive_extents_fill, validate, mprotect              | Sealed KosCode                 |

### 3.2 Phase 1: Collect

Walk the input instruction stream. Use **temporary structures** only; no final
block allocation.

#### 1.1 Simulate instruction squashing

Track which instructions survive (are not LABEL or POSITION):

- LABEL: register `(label_name → output_pc)`; output_pc is the current `di`
  (destination index). Do not advance di.
- POSITION: unpack (line,col). Record position change only when different from
  last. Do not advance di.
- All others: advance di.

Result: `output_instr_count` = di after full pass. Also: `label_count`,
`pos_entry_count` (only when position changed).

#### 1.2 Collect constants (identity-based)

Use a **growable list** (PyList or equivalent). For each constant candidate:

1. Walk the list; if `list[i] is value` (identity), return index i.
2. Else append value (Py_INCREF), return new index.

Constant candidates come from:

- **Inline CONST** style: arg is the value.
- **Inline VAR** style: arg is name; use `PyObject_Str(arg)` and treat that
  as the value to intern.
- **EXTRA** style: arg is tuple. For each schema field of kind CONST or VAR:
  - CONST: tuple element is the value.
  - VAR: stringify tuple element, use as value.
  Apply the same identity-based lookup/append.

Result: `cpool_list` (or equivalent) and `cpool_len` = number of unique
constants.

#### 1.3 Collect labels

As we simulate the squashed stream, record:

- **LABEL definition:** `label_name → di` (output pc where that label points).
- **LABEL reference:** For each LABEL-style opcode at output pc `di`, record
  that its arg (label name) will need to resolve to the target pc for that
  label.

After the pass, we have a complete label map: every referenced label maps to
its definition pc.

#### 1.4 Collect extra rows

For each EXTRA opcode:

1. Validate arg is a tuple of arity matching the opcode's schema.
2. For each schema field, validate type (e.g. COUNT is int, FAST is int,
   CONST/VAR are appropriate).
3. Build a preliminary KosExtra row:
   - CONST/VAR: store the **cpool index** (from the identity-based collect
     above; we must do constant collection before or interleaved with extra
     collection, so that when we process an EXTRA we can resolve CONST/VAR
     to indices).
   - LABEL: leave as placeholder (label name); resolve in a later step.
   - COUNT, FAST: store the integer value.
   - NONE: 0.
4. Append to a temporary list of extra rows. Deduplication of extra rows is
   optional (can intern identical rows) or omitted for simplicity; design
   allows either.

Result: `extra_rows` list, `extra_len`. If we deduplicate, we also build a map
from "row key" to index for reuse.

#### 1.5 Count extents

While simulating the instruction stream, count PUSH_* opcodes. Each PUSH_* that
survives (is not squished) becomes one extent. Result: `extent_count`.

#### 1.6 Order of collection

Constants must be collected before or during extra row collection, because
EXTRA CONST/VAR fields need cpool indices. Recommended order:

1. First pass: simulate squashing, collect labels (definitions), count position
   changes, count PUSH_*.
2. Second pass (or interleaved): for each surviving instruction, collect
   constants (inline CONST/VAR and EXTRA CONST/VAR), build extra rows. Use one
   growable cpool list and one extra-rows list.

Alternatively, a single pass that does everything: simulate squashing, and at
each instruction collect constants and extras as needed. The key invariant:
when we process an EXTRA opcode, we have already processed any constants it
needs, or we process them on-the-fly when handling that opcode.

#### 1.7 Derived sizes

After collect:

| Quantity           | Source                                      |
|--------------------|---------------------------------------------|
| code_len           | output_instr_count                          |
| pos_len            | pos_entry_count                             |
| cpool_len          | length of cpool list                        |
| extra_len          | length of extra rows list                   |
| extent_len         | extent_count                                |
| top_extent_id_len  | code_len (one uint16 per instruction)       |

All sizes are exact. No oversizing.

---

### 3.3 Phase 2: Allocate

Allocate a **single** contiguous block with layout:

```
[code array]           code_len * sizeof(KosOp)
[padding to alignment]
[positions array]      pos_len * sizeof(KosPosition)
[padding]
[cpool array]          cpool_len * sizeof(PyObject*)
[padding]
[extra array]          extra_len * sizeof(KosExtra)
[padding]
[extents array]        extent_len * sizeof(KosExtentDesc)
[top_extent_id array]  code_len * sizeof(uint16_t)
```

Round total to page size for mprotect. All regions are precisely sized.

---

### 3.4 Phase 3: Emit

Walk the input again and write into the final block.

#### 3.4.1 Emit instructions

For each input instruction:

- **LABEL:** Skip (no output); update label map if not already done.
- **POSITION:** Update (line,col) state; if changed, write next position entry;
  skip instruction output.
- **Other opcodes:** Write `code[di]`.

For each written instruction, set `op` and `arg`:

- **CONST:** arg = cpool index (from collect phase).
- **VAR:** arg = cpool index (from collect phase).
- **LABEL-style:** arg = resolved pc from label map.
- **EXTRA:** arg = index into extra table; the extra row was built in collect
  (with LABEL slots still unresolved if we deferred).
- **COUNT, PAIR_COUNT, STAR, FASTVAR, NOARG:** arg = value or 0.

#### 3.4.2 Resolve LABEL slots in extra rows

If any extra row has a LABEL field that was left as a placeholder, now resolve
it: label name → pc from label map. Write the pc into the KosExtra row.

#### 3.4.3 Emit positions

Position entries were built during the simulated pass. Copy them into the
positions region.

#### 3.4.4 Emit constant pool

Copy references from the cpool list into the cpool region. Adjust refcounts as
needed (the list held references; the final block will hold the canonical ones).

#### 3.4.5 Emit extra table

Copy the extra rows (with LABEL fields now resolved) into the extra region.

---

### 3.5 Phase 4: Finalize

1. **derive_extents_fill:** Walk the emitted code, fill extent descriptors and
   top_extent_id into their region. Same logic as current derive_extents, but
   writes into pre-allocated buffers.

2. **Validate:** Jump locality, ITER_NEXT in foreach, etc. Unchanged.

3. **mprotect:** Mark the entire block read-only.

4. **Attach to KosCode:** Set code, pos, cpool, extra, extents, top_extent_id
   pointers and lengths. Single allocation field.

---

## 4. Order of Operations (Squish and Resolution)

A precise sequence for the final squish:

1. **Drop LABEL:** LABEL opcodes produce no instruction; they only define
   label targets. When we see LABEL at output position di, we record
   `label_name → di` and do not emit an instruction.

2. **Drop POSITION:** POSITION opcodes produce no instruction; they only
   contribute to the position table. When position changes, we add an entry.

3. **Convert CONST/VAR to indices:** During collect, we intern values into the
   cpool. During emit, we write the index into the instruction's arg. Same for
   CONST/VAR fields inside EXTRA rows.

4. **Convert LABEL refs to pcs:** After we know the output layout (label defs
   map to output pcs), we resolve every LABEL-style arg and every LABEL field
   in extra rows to the target pc.

5. **Convert EXTRA tuples to rows:** During collect we build KosExtra rows with
   CONST/VAR as indices and LABEL as placeholder; during emit we resolve
   placeholders and write rows to the extra table.

6. **Derive extents:** After the instruction stream is finalized, we run
   derive_extents_fill.

No operation depends on an oversized allocation. Everything is sized by the
collect phase.

---

## 5. Constant Collection: Identity-Based Linear Scan

### 5.1 Algorithm

```
cpool_list = []   # growable, holds PyObject* (or PyObject* equivalent)

function intern_const(value):
    for i = 0 to len(cpool_list)-1:
        if cpool_list[i] is value:    # identity
            return i
    append value to cpool_list
    Py_INCREF(value)
    return len(cpool_list) - 1
```

### 5.2 VAR handling

For VAR, the "value" to intern is the string form: `name = PyObject_Str(arg)`.
We intern `name`. The caller must Py_DECREF(name) after the call if it was
newly created, depending on ownership.

### 5.3 EXTRA CONST/VAR

When processing an EXTRA opcode with schema `[FAST, CONST, CONST, COUNT]` and
tuple `(slot, attr, kw_names, argc)`:

- attr → intern_const(attr) → index for a1
- kw_names → intern_const(kw_names) → index for a2

These indices go into the KosExtra row and into the same cpool as any inline
CONST/VAR.

---

## 6. Integration with Extra Table Design

This finalization design incorporates the extra table:

- **Collect:** Process EXTRA opcodes; collect CONST/VAR from their tuple
  elements per schema; build extra rows with indices.
- **Size:** extra_len = number of extra rows (dedup optional).
- **Emit:** Write KosExtra rows into the extra region; instruction arg = index.
- **Layout:** Extra table sits after cpool, before extents, in the unified block.

The extra-table schema (kos_op_extra_schema) drives how we interpret each
tuple element. Field kinds NONE, COUNT, FAST, CONST, VAR, LABEL are handled as
in the extra-table design document.

---

## 7. Summary

| Current                          | Proposed                                    |
|----------------------------------|---------------------------------------------|
| Single pass, worst-case sizing   | Collect phase with temp structures, exact sizes |
| cpool over-allocated, identity dedup | Identity-based linear list, exact cpool_len   |
| code/pos over-allocated          | Simulate squashing first, exact code_len, pos_len |
| Extent block separate            | Extent region in unified block              |
| No extra table                   | Extra table between cpool and extents       |
| collect_const in pre-allocated array | intern_const in growable list, then emit      |

The result is a sealed, read-only bytecode block with no slack and a clear
separation between collection (analyze and measure), allocation (single
block), emission (write), and finalization (extents, validation, mprotect).
