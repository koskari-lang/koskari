# Bytecode Format

**OPEN (not settled)**: Bytecode word size and encoding details beyond tentative
(uint16 op, uint16 arg) format; exact taxonomy of continuation-related
exceptions.

**Per-opcode reference**: Authoritative opcode semantics, use cases, and
invariants are documented in [../OPCODES.md](../OPCODES.md), organized by
category.

## Koskari Bytecode (KosCode)

Koskari uses a custom bytecode format optimized for:
- Compact instruction representation (4 bytes per instruction)
- Delta-encoded position tracking for efficient source mapping
- Read-only memory protection after initialization
- Async-first design (all code is async by default)

### Instruction Format

Each instruction is 4 bytes:

```c
typedef struct KosOp {
    uint16_t op;    /* opcode (0-65535) */
    uint16_t arg;   /* argument (0-65535) */
} KosOp;
```

This compact format supports:
- Up to 65,536 unique opcodes
- Constants pool indices up to 65,535
- Jump targets up to 65,535 instructions
- Stack counts and other arguments up to 65,535

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
    PyObject_HEAD

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

    uint32_t extent_len;  /* extent descriptor count */
    KosExtentDesc *extents;     /* extent table, tagged-union descriptors (read-only; NULL if none) */
    uint16_t *top_extent_id;    /* [code_len] innermost extent per pc (NULL if none) */

    PyCodeObject *faux;   /* faux code object for tracebacks */
    PyObject *extras;     /* extra metadata dict */
};
```

The extent table is derived during Code construction from PUSH/POP pairs (TRY, LOOP, FINALLY, PROMPT, etc.). Each entry is a KosExtentDesc (tagged union: kind plus kind-specific targets in u.try_finally, u.loop, u.exit_only, or u.shift). It enables O(1) lookup of block membership and handler targets. See [bytecode-metadata-tables.md](bytecode-metadata-tables.md).

### Memory Protection

After initialization, the bytecode storage (instructions, positions, constant pool) is marked read-only via `mprotect()`. This:
- Prevents heap overflow attacks from modifying instructions
- Ensures bytecode immutability during execution
- Provides defense-in-depth security

### Code Flags

```c
typedef enum KosCodeFlag {
    KOS_CODE_YIELDS = 0x01,  /* code contains yield -> async generator */
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
} KosOpStyle;
```

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
- `KOS_OP_YIELD_FROM`: Yield from iterable (implemented; sets YIELDS flag; delegate to sub-iterator/gen; KOS_DYN_YIELD_FROM; handler in koskari/lib/opcode/yield_from.c)

### Jumps
- `KOS_OP_JUMP`: Unconditional jump
- `KOS_OP_JUMP_TRUE`: Jump if true
- `KOS_OP_JUMP_FALSE`: Jump if false
- `KOS_OP_JUMP_AND`: Short-circuit AND
- `KOS_OP_JUMP_OR`: Short-circuit OR

### Check Jumps (Pattern Matching)
- `KOS_OP_CHECK_EQ`, `KOS_OP_CHECK_NE`: Equality checks
- `KOS_OP_CHECK_LT`, `KOS_OP_CHECK_LE`, `KOS_OP_CHECK_GT`, `KOS_OP_CHECK_GE`: Comparisons
- `KOS_OP_CHECK_IN`, `KOS_OP_CHECK_NOT_IN`: Membership
- `KOS_OP_CHECK_IS`, `KOS_OP_CHECK_IS_NOT`: Identity
- `KOS_OP_CHECK_IS_INSTANCE`, `KOS_OP_CHECK_IS_SUBCLASS`: Type checks
- `KOS_OP_CHECK_EXCEPTION`: Exception matching

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
- `KOS_OP_ITEM`: Get item (computed key)
- `KOS_OP_ITEM_CLEAR`: Delete item
- `KOS_OP_ITEM_REF`: Get item reference
- `KOS_OP_ITEM_STORE`: Set item
- `KOS_OP_CONST_ITEM`: Get item (constant key)
- `KOS_OP_CONST_ITEM_CLEAR`, `KOS_OP_CONST_ITEM_REF`, `KOS_OP_CONST_ITEM_STORE`

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
- `KOS_OP_PUSH_FINALLY`: Push finally block (implemented); pushes KOS_DYN_FINALLY and KosStackBlock
- `KOS_OP_PUSH_LOOP`: Push while loop (implemented); arg = break target; pushes KOS_DYN_LOOP and KosStackBlock
- `KOS_OP_PUSH_NS`: Push namespace scope (implemented); EXTRA operand row: NSPEC and names (tuple or None) as cpool indices; opa=0: no stack values; opa>0: [val0, ..., val_{opa-1}] only; creates KosNamespace, pushes KosDynNs
- `KOS_OP_PUSH_TRY`: Push try block (implemented); pushes KOS_DYN_TRY and KosStackBlock
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
- `KOS_OP_END_WIND`: Sentinel that terminates hook block execution

### Continuation Invocation
- `KOS_OP_INVOKE_K`: Pop value then continuation from stack; invoke continuation with value
- `KOS_OP_INVOKE_K_VAR`: Pop value; look up name (constant index) in frame namespace; invoke that continuation with value. Used when continuation is bound by name (e.g. shift handler with PUSH_NS + VAR_STORE)

### Object Protocol
- Comparisons: `KOS_OP_OBJECT_EQ`, `KOS_OP_OBJECT_NE`, `KOS_OP_OBJECT_LT`, etc.
- Predicates: `KOS_OP_OBJECT_IN`, `KOS_OP_OBJECT_IS`, `KOS_OP_OBJECT_IS_INSTANCE`, etc.
- Arithmetic: `KOS_OP_OBJECT_ADD`, `KOS_OP_OBJECT_SUBTRACT`, `KOS_OP_OBJECT_MULTIPLY`, etc.
- General: `KOS_OP_OBJECT_ITER`, `KOS_OP_OBJECT_AITER`, `KOS_OP_OBJECT_LEN`, etc.

### Iteration
- `KOS_OP_ITER_NEXT`: Get next from region iterator (must be inside PUSH_FOREACH); arg = else label; uses region->iter; auto-await
- `KOS_OP_NEXT`: Stack-based next; arg = count (1 or 2); 1-arg: TOS = iterator, StopIteration raises; 2-arg: TOS = default, below = iterator, StopIteration pushes default; auto-await

### Introspection
- `KOS_OP_GLOBALS`: Get globals dict
- `KOS_OP_LOCALS`: Get locals dict

### Exceptions
- `KOS_OP_EXCEPTION`: Load current exception
- `KOS_OP_EXCEPTION_CLEAR`: Clear exception
- `KOS_OP_EXCEPTION_MATCH`: Match exception type

### No runtime implementation
- `KOS_OP_THEEND`: End-of-code sentinel. If execution reaches THEEND, the handler raises RuntimeError with a fixed message; THEEND has no runtime implementation and never will.

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
- 8x more compact instructions (4 bytes vs ~32 bytes)
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
int kos_try_handler_for(PyObject *code, uint32_t pc, KosTryHandler *out);
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

Static metadata tables (location table, extent table, pc→extent lookup, JUMP locality validation) are implemented per **[bytecode-metadata-tables.md](bytecode-metadata-tables.md)**. Each extent descriptor is a tagged union (use the union member that matches `kind`); prefer `kos_try_handler_for` and `kos_shift_ic_for` for handler/IC targets. Extents are derived during Code construction; malformed bytecode (unmatched PUSH/POP or non-local control-flow jumps) is rejected.

## Files

- `koskari/lib/bytecode.h`: Type definitions, enums, macros, function declarations
- `koskari/lib/bytecode.c`: Full implementation
- `koskari/lib/opcodes.incl`: Opcode definitions (included multiple times with different macros)
- `tests/native/test_bytecode.c`: Native unit tests
