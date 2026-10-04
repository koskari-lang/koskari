# Bytecode Format

**OPEN (not settled)**: exact taxonomy of continuation-related exceptions.

**Per-opcode reference**: Authoritative opcode semantics, use cases, and
invariants are documented in [../OPCODES.md](../OPCODES.md), organized by
category.

## Koskari Bytecode (KosCode)

Koskari uses a custom bytecode format optimized for:
- A serialized instruction of (op, arg), resolved at load into a record
  that carries its operand as a pointer
- Delta-encoded position tracking for efficient source mapping
- Read-only memory protection after initialization
- Async-first design (all code is async by default)

### Instruction Format

Each instruction is a 32-byte record, two to a cache line. Its
serialized half is what `soften` emits and `.kio` stores; its derived
half is resolved at finalize and on every load, never serialized, and
re-derived when `pack_into` relocates the block.

```c
struct KosOp {
    KosThreadFn fn;      /* derived: kos_op_handlers[op] */
    uint16_t op;         /* KosOpCode */
    uint16_t arg;        /* index or immediate, by style; the target pc for LABEL */
    uint16_t cache;      /* per-site cache slot index, or KOS_CACHE_UNSET */
    uint16_t pc;         /* this record's own index */
    union {              /* derived; selected by style */
        PyObject *obj;   /* CONST, VAR: the pool object, borrowed */
        KosOp *target;   /* LABEL: the target record */
        KosExtra *row;   /* EXTRA: the operand row */
    } u;
    KosCacheSlot *slot;  /* derived: the site's cache slot, or NULL */
};
```

`pc` makes every pointer to an instruction self-describing: a jump lands
through `target` and reads the landing pc off it, and a handler's
straight-line step is `insn + 1`. Jump targets and `pc` are 16-bit, so a
code object holds at most 65,535 instructions and finalize enforces it.
`obj` and `slot` point out of the block and are stable; `target` and
`row` point into it and the fill pass re-derives them after a move.
The instruction array, the extras table, and arena placements are
32-aligned so no record straddles a line.

### Extended Operand Rows

An EXTRA opcode's `arg` names a row, one per EXTRA instruction and
never shared; the record reaches it through `u.row`. Each field is 8
bytes, read through the member the opcode's schema names:

```c
typedef union KosExtraField {
    uint32_t imm;        /* COUNT, FASTVAR, OPCOUNT, CMP, CMPSRC, STAR, OPERAND */
    PyObject *obj;       /* CONST, VAR: the pool object, borrowed */
    KosOp *target;       /* LABEL: the target record */
} KosExtraField;

typedef struct KosExtra {
    KosExtraField f[4];  /* 32 bytes */
} KosExtra;
```

Collect builds the row in index form (four 16-bit fields); emit
resolves it. A row keeps no index: `soften` and `extra_tuple` recover
a constant's index by identity against the pool and a label's pc off
the target, so the Python-facing view is unchanged. The design record
is [../archive/resolved-operands.md](../archive/resolved-operands.md).

### Position Table

Source positions are stored in a separate delta-encoded table. Only position changes are recorded:

```c
typedef struct KosPosition {
    uint16_t index;   /* instruction index where position changes */
    uint16_t line;    /* source line number */
    uint16_t col;     /* source column number */
} KosPosition;
```

Position lookup uses binary search to find the applicable entry for any instruction index.

### Code Object Structure

```c
struct KosCode {
    PyObject_VAR_HEAD     /* ob_size = count of trailing KosCacheSlot entries */

    PyObject *name;       /* function/expression name */
    PyObject *filename;   /* source filename */
    PyObject *nspec;      /* namespace specification */

    uint32_t flags;       /* KosCodeFlag bitfield */
    size_t allocation;    /* total memory allocation size */

    uint32_t code_len;    /* instruction count */
    KosOp *code;          /* instruction array (read-only) */

    uint32_t pos_len;     /* position entry count */
    KosPosition *pos;     /* positions table (read-only) */

    uint32_t cpool_len;   /* constant pool size */
    PyObject **cpool;     /* constant pool (read-only) */

    uint32_t extra_len;   /* extended operand row count */
    KosExtra *extra;      /* extended operand rows (read-only) */

    uint32_t extent_len;  /* extent descriptor count */
    KosExtentDesc *extents;     /* extent table, flat descriptors (read-only; NULL if none) */
    uint16_t *top_extent_id;    /* [code_len] innermost extent per pc (NULL if none) */

    PyCodeObject *faux;   /* faux code object for tracebacks */
    PyObject *extras;     /* extra metadata dict */

    PyObject *arena;      /* owning KosArena once packed; NULL while unpacked */
    PyObject *weakreflist;
};
```

The per-site cache slots follow the struct as a variable-length tail; they are
the one thing a Code owns that can take part in a reference cycle, and the
reason the type is GC-tracked.

The extent table is derived during Code construction from PUSH/POP pairs (TRY, LOOP, FINALLY, PROMPT, etc.). Each entry is a flat KosExtentDesc: kind, push_pc, pop_pc and parent_extent_id; handler and jump targets are derived on demand from the instruction at push_pc and from pop_pc. It enables O(1) lookup of block membership. See [archive/bytecode-metadata-tables.md](../archive/bytecode-metadata-tables.md).

### Memory Protection

After initialization, the bytecode storage (instructions, positions, constant pool) is marked read-only via `mprotect()`. This:
- Prevents heap overflow attacks from modifying instructions
- Ensures bytecode immutability during execution
- Provides defense-in-depth security

### Code Flags

```c
typedef enum KosCodeFlag {
    KOS_CODE_YIELDS = 0x01,     /* code contains yield -> async generator */
    KOS_CODE_USES_FAST = 0x02,  /* FAST-slot opcodes at root namespace scope */
    KOS_CODE_PROTECTED = 0x04,  /* unified block is mprotect(PROT_READ)-sealed */
    KOS_CODE_CACHE = 0x08,      /* per-site opcode caches enabled (pragma gate) */
} KosCodeFlag;
```

Since all Koskari code is async by default:
- No separate `CODE_AWAITS` flag is needed
- The `KOS_CODE_YIELDS` flag distinguishes:
  - Regular code -> returns `Coroutine`
  - Generator code -> returns `AsyncGenerator`

### Opcode Styles

Opcodes are categorized by their argument interpretation:

```c
typedef enum KosOpStyle {
    KOSTYLE_COMPILE,     /* compile-time only (LABEL, POSITION) */
    KOSTYLE_LABEL,       /* argument is jump target */
    KOSTYLE_NOARG,       /* argument unused */
    KOSTYLE_COUNT,       /* argument is stack element count */
    KOSTYLE_PAIR_COUNT,  /* argument is paired element count */
    KOSTYLE_STAR,        /* argument is star call bitflags */
    KOSTYLE_CONST,       /* argument is constant pool index */
    KOSTYLE_VAR,         /* argument is variable name (in cpool) */
    KOSTYLE_FASTVAR,     /* argument is fast local index */
    KOSTYLE_OPCOUNT,     /* operator selector (high byte) + reducing count (low byte) */
    KOSTYLE_EXTRA,       /* argument is an index into the extra table */
    KOSTYLE_CMP,         /* field is a bare comparison selector */
    KOSTYLE_CMPCOUNT,    /* comparison selector (high byte) + reducing count (low byte) */
    KOSTYLE_CMPSRC,      /* comparison selector + per-operand source-shape bits */
    KOSTYLE_OPERAND,     /* field is a bare operator selector */
} KosOpStyle;
```

The last six travel either as an instruction's own arg or as one field of an
EXTRA row; `opcodes.incl` declares each opcode's schema.

## Opcode Categories

For the full opcode reference (semantics, invariants, use cases, and example
disassembly), see [../OPCODES.md](../OPCODES.md). The summary below is for
quick lookup.

### Stack Operations
- `KOS_OP_NOOP`: No operation (runtime: advance PC only; no effect)
- `KOS_OP_LABEL`: Jump target (compile-time); at runtime no-op (advance PC only)
- `KOS_OP_POSITION`: Source position (compile-time); at runtime no-op (advance PC only)
- `KOS_OP_DUP`: Duplicate top N elements
- `KOS_OP_POP`: Pop N elements
- `KOS_OP_CONST`: Load constant from pool

### Control Flow
- `KOS_OP_AWAIT`: Await (arg = stack count: 1 = TOS awaitable, 0 = bare suspend)
- `KOS_OP_BREAK`: Break from loop (implemented; unwinds to LOOP/FOREACH region, jumps to break target)
- `KOS_OP_CONTINUE`: Continue loop (implemented; unwinds to LOOP/FOREACH region, jumps to continue target)
- `KOS_OP_RAISE`: Raise exception
- `KOS_OP_RETURN`: Return value
- `KOS_OP_YIELD`: Yield value (sets YIELDS flag)
- `KOS_OP_YIELD_FROM`: Yield from iterable (implemented; sets YIELDS flag; delegate to sub-iterator/gen; KOS_DYN_YIELD_FROM; handler in koskari/lib/opcode/YIELD_FROM.c)

### Jumps
- `KOS_OP_JUMP`: Unconditional jump
- `KOS_OP_JUMP_TRUE`: Jump if true
- `KOS_OP_JUMP_FALSE`: Jump if false
- `KOS_OP_JUMP_AND`: Short-circuit AND
- `KOS_OP_JUMP_OR`: Short-circuit OR

### Check Jumps
- `KOS_OP_CHECK_COMPARE`: Chain-compare N operands under a KosCompare selector (EQ, NE, LT, LE, GT, GE, IN, NOT_IN, IS, IS_NOT, IS_INSTANCE, IS_SUBCLASS); jump if the chain fails
- `KOS_OP_CHECK_COMPARE_VAR`, `KOS_OP_CHECK_COMPARE_FAST`: Arity-2 fused checks with operands from the namespace or fast slots (or the constant pool per the CMPSRC bits)
- `KOS_OP_CHECK_NIL`: Squashed NILP + JUMP_FALSE; jump if TOS is not an empty-list singleton
- `KOS_OP_CHECK_EXCEPTION`: Exception matching
- `KOS_OP_SWITCH_EQ` and the fused `SWITCH_EQ_CONST` / `_VAR` / `_FAST` / `_VAR_ATTR` / `_FAST_ATTR`: Equality dispatch for `switch` arms

### Variables (by name)
- `KOS_OP_VAR`: Load variable by name (constant index)
- `KOS_OP_VAR_BIND`: Bind reference to name (pop value, bind)
- `KOS_OP_VAR_CLEAR`: Delete name from namespace
- `KOS_OP_VAR_REF`: Get variable reference (for closure)
- `KOS_OP_VAR_STORE`: Pop TOS, store in frame namespace under name

### Fast Variables (index-based)
- `KOS_OP_FAST`: Load fast local by slot index
- `KOS_OP_FAST_BIND`: Bind fast local slot
- `KOS_OP_FAST_CLEAR`: Clear fast slot
- `KOS_OP_FAST_REF`: Get fast local reference
- `KOS_OP_FAST_STORE`: Store TOS to fast slot

### Attributes
- `KOS_OP_ATTR`: Get attribute
- `KOS_OP_ATTR_CLEAR`: Delete attribute
- `KOS_OP_ATTR_REF`: Get attribute reference
- `KOS_OP_ATTR_STORE`: Set attribute

### Items
- `KOS_OP_ITEM`: Get item (object and key on the stack)
- `KOS_OP_ITEM_CLEAR`: Delete item
- `KOS_OP_ITEM_REF`: Get item reference
- `KOS_OP_ITEM_STORE`: Set item
- `KOS_OP_ITEM_CONST`: Get item (constant key), and the rest of the
  item grid named by object, key, and value origin (see
  [../archive/item-opcode-matrix.md](../archive/item-opcode-matrix.md))

### Calls
- `KOS_OP_CALL`: Simple call
- `KOS_OP_CALL_NAMES`: Call with keyword names
- `KOS_OP_CALL_STAR`: Call with *args/**kwargs
- `KOS_OP_CALL_ATTR`: Method call
- `KOS_OP_CALL_ATTR_NAMES`, `KOS_OP_CALL_ATTR_STAR`
- `KOS_OP_TAIL_CALL`: Tail call (replaces frame)
- `KOS_OP_TAIL_CALL_NAMES`, `KOS_OP_TAIL_CALL_STAR`

### Block Control
- `KOS_OP_PUSH_FOREACH`: Push for-each loop (implemented); arg = break target; TOS = iterable for ITER_NEXT; pushes KOS_DYN_FOREACH and stack block
- `KOS_OP_PUSH_WITH`: Push with context (stub)
- `KOS_OP_PUSH_COLLECT`: Push collector scope (implemented); CONST operand = cpool index of NamespaceSpec; installs CollectorNamespace; KOS_DYN_COLLECT
- `KOS_OP_PUSH_FINALLY`: Push finally block (implemented); pushes KOS_DYN_FINALLY and KosSegment
- `KOS_OP_PUSH_LOOP`: Push while loop (implemented); arg = break target; pushes KOS_DYN_LOOP and KosSegment
- `KOS_OP_PUSH_NS`: Push namespace scope (implemented); EXTRA operand row: NSPEC and names (tuple or None) as cpool indices; opa=0: no stack values; opa>0: [val0, ..., val_{opa-1}] only; creates KosNamespace, pushes KosDynNs
- `KOS_OP_PUSH_TRY`: Push try block (implemented); pushes KOS_DYN_TRY and KosSegment
- `KOS_OP_CLEAR_FINALLY`, `KOS_OP_CLEAR_LOOP`, `KOS_OP_CLEAR_FOREACH`, `KOS_OP_CLEAR_TRY` (implemented)
- `KOS_OP_POP_WITH`, `KOS_OP_POP_FINALLY`, `KOS_OP_POP_LOOP`, `KOS_OP_POP_FOREACH`, `KOS_OP_POP_NS`, `KOS_OP_POP_COLLECT` (POP_NS, POP_LOOP, POP_FOREACH, POP_COLLECT implemented), etc.

### Delimited Continuations
- `KOS_OP_PUSH_PROMPT`: Push prompt delimiter; also pushes a COW stack block for the delimited region (arg = prompt_id constant index)
- `KOS_OP_POP_PROMPT`: Drop PROMPT's stack block, pop prompt, push result to outer stack (normal path); or unwind to invocation site (continuation-return path)
- `KOS_OP_PUSH_SHIFT`: Capture continuation, unwind to prompt, enter shift block with k on stack (arg = prompt_id constant index); SHIFT is a simple marker, no stack block
- `KOS_OP_POP_SHIFT`: Pop TOS as result; TRUNCATE_AND_INJECT unwinds through SHIFT+PROMPT, drops stack block, injects result onto outer stack

### Dynamic Wind
- `KOS_OP_PUSH_WIND`: Push wind extent (arg = COUNT: 1 = before only, 2 = both thunks)
- `KOS_OP_POP_WIND`: Pop wind from dynamic chain

### Continuation Invocation
A captured continuation is an ordinary callable (`DelimitedContinuation`, `Continuation`); invoking one is a `CALL` whose target is that object. There is no dedicated opcode.

### Object Protocol
- `KOS_OP_OBJECT_OPERAND`: One arithmetic or bitwise operator over N operands; the OPCOUNT arg packs the KosOperand selector with the count. The hottest opcode; its site elects a specialized body (see [electors.md](electors.md))
- `KOS_OP_OBJECT_INPLACE`: The same operator set through the `PyNumber_InPlace*` protocol; used by the inplace-item and inplace-attr forms
- `KOS_OP_OBJECT_COMPARE` and the fused `OBJECT_COMPARE_VAR` / `OBJECT_COMPARE_FAST`: Chain comparisons as values (CMPCOUNT)
- General: `KOS_OP_OBJECT_ITER`, `KOS_OP_OBJECT_LEN`, `KOS_OP_OBJECT_NOT`, `KOS_OP_OBJECT_TRUTH`, `KOS_OP_OBJECT_REPR`, `KOS_OP_OBJECT_STR`, `KOS_OP_OBJECT_FORMAT`, `KOS_OP_OBJECT_TYPE`, `KOS_OP_OBJECT_DIR`, `KOS_OP_OBJECT_VARS`, `KOS_OP_OBJECT_DIVMOD`, `KOS_OP_OBJECT_BIT_INVERT`
- `KOS_OP_NILP`: Empty-list test

### Iteration
- `KOS_OP_ITER_NEXT`: Get next from region iterator (must be inside PUSH_FOREACH); arg = else label; uses region->iter; auto-await
- `KOS_OP_OBJECT_NEXT`: Stack-based next; arg = count (1 or 2); 1-arg: TOS = iterator, StopIteration raises; 2-arg: TOS = default, below = iterator, StopIteration pushes default; auto-await
- `KOS_OP_VAR_NEXT`, `KOS_OP_FAST_NEXT`: Fused next on a named or fast-slot iterator

### Introspection
- `KOS_OP_GLOBALS`: Get globals dict
- `KOS_OP_LOCALS`: Get locals dict

### Exceptions
- `KOS_OP_EXCEPTION`: Load current exception
- `KOS_OP_EXCEPTION_CLEAR`: Clear exception
- `KOS_OP_EXCEPTION_MATCH`: Match exception type

### No runtime implementation
- `KOS_OP_THEEND`: End-of-code sentinel appended by code construction and rejected in user input. If execution reaches it, the handler reports the pc-bounds fault as a SystemError and returns the fatal result; it never does work.

### Compile-time semantics (runtime no-op)
- `KOS_OP_LABEL`: Jump target (compile-time); at runtime no-op (advance PC only). May be rewritten to NOOP during code build or left as LABEL.
- `KOS_OP_POSITION`: Source position (compile-time; stored in positions table); at runtime no-op (advance PC only). May be rewritten to NOOP during code build or left as POSITION.

## Opcode implementation status

See [OPCODES.md](../OPCODES.md) §1.6. That document is the authoritative
reference for which opcodes are implemented, stubs, or have no runtime.

## Creating and calling code (expressions and functions)

- **Expression**: `Code.expression(parent_ns)` returns an **Expression** (code + namespace). Calling it with `()` creates a **Coroutine** that runs that code in that namespace (no arguments).
- **Function**: `Code.function(formals, parent_ns)` returns a **Function** (code + formals + base namespace). Calling it with `(*args, **kwargs)` binds formals, builds a frame namespace, and creates a **Coroutine** that runs the code in that frame.
- **Execution**: Driving the Coroutine (e.g. Python `await` or `send`) invokes `koskari_eval()`. A Koskari **CALL** with a KosExpression or KosFunction replaces the current frame with the callee and continues in the same loop. See [evaluator.md](evaluator.md) and [FEATURES.md](../FEATURES.md) § "Creating and calling functions and expressions".

## Comparison with Previous Implementations

### From python-sibilant

python-sibilant compiled to CPython bytecode (`.pyc` files) with version-specific targets (3.5, 3.6, 3.7). This approach was abandoned due to:
- Version churn in CPython bytecode format
- Maintenance burden of multiple targets
- Limited ability to implement call/cc

### From sibilant-ceval

sibilant-ceval used a similar custom bytecode format but with wider instructions:
- Each instruction stored line/col inline (32 bytes per instruction)
- No memory protection
- Separate async/await flags

Koskari's improvements:
- 32-byte instruction records that carry the resolved handler and operand with the opcode (`KosOp` in `bytecode.h`), so dispatch does no table lookups
- Delta-encoded position table saves memory
- Read-only memory protection for security
- Simplified async model (all code is async)

## API

### Python Interface

```python
# Create bytecode
code = Code(namespace_spec, instructions, name="func", filename="test.ki")

# Properties
code.name          # function name
code.filename      # source filename
code.namespace_spec  # NamespaceSpec
code.yields        # bool: contains yield?
code.constants_pool  # tuple of constants

# Methods
code.length()      # instruction count
code.oparg(i)      # (opcode, arg) at index i
code.position(i)   # (line, col) at index i
code.constant(i)   # constant at index i
```

### C Interface

```c
/* Constructor */
PyObject *KosCode_New(PyObject *nspec, PyObject *instr,
                      PyObject *name, PyObject *filename);

/* Accessors */
PyObject *KosCode_GetConstant(PyObject *self, uint32_t index);
PyObject *KosCode_GetInstruction(PyObject *self, uint32_t index);
int KosCode_GetPosition(PyObject *self, uint32_t index,
                        uint16_t *out_line, uint16_t *out_col);
PyCodeObject *KosCode_Faux(PyObject *self);
KosOpStyle kos_opcode_style(KosOpCode op);

/* Location and extent lookup (metadata tables) */
int kos_loc_lookup(PyObject *code, uint32_t pc, uint16_t *line, uint16_t *col);
uint32_t kos_extent_top(PyObject *code, uint32_t pc);
uint32_t kos_extent_find(PyObject *code, uint32_t pc, KosExtentKind kind);
KosExtentDesc *kos_extent_desc(PyObject *code, uint32_t extent_id);
uint32_t kos_shift_ic_for(PyObject *code, uint32_t pc);

/* Macros */
KosCode_FLAGS(code)
KosCode_YIELDS(code)
KosCode_CODE_LEN(code)
KosCode_GET_OP(code, i)
KosCode_GET_ARG(code, i)
KosCode_CPOOL_LEN(code)
KosCode_GET_CONST(code, i)
KosCode_EXTENT_LEN(code)
KosCode_EXTENTS(code)
KosCode_TOP_EXTENT_ID(code)
```

## Extent table and metadata

Static metadata tables (location table, extent table, pc→extent lookup, JUMP locality validation) are implemented per **[archive/bytecode-metadata-tables.md](../archive/bytecode-metadata-tables.md)**. Each extent descriptor is flat (kind, push_pc, pop_pc, parent_extent_id); targets are derived from the push instruction and pop_pc, and `kos_shift_ic_for` resolves a shift's resume pc. Extents are derived during Code construction; malformed bytecode (unmatched PUSH/POP or non-local control-flow jumps) is rejected.

## Files

- `koskari/lib/bytecode.h`: Type definitions, enums, macros, function declarations
- `koskari/lib/bytecode.c`: Full implementation
- `koskari/lib/opcodes.incl`: Opcode definitions (included multiple times with different macros)
- `tests/native/test_bytecode.c`: Native unit tests
