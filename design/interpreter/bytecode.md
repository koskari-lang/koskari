# Bytecode Format

**OPEN (not settled)**: Full opcode set and per-opcode semantics; bytecode word
size and encoding details beyond tentative (uint16 op, uint16 arg) format; full
opcode inventory and formal operand conventions; precise opcode-level semantics
for local-only jumps; exact taxonomy of continuation-related exceptions.

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
    KosExtentDesc *extents;     /* extent table (read-only; NULL if none) */
    uint16_t *top_extent_id;    /* [code_len] innermost extent per pc (NULL if none) */

    PyCodeObject *faux;   /* faux code object for tracebacks */
    PyObject *extras;     /* extra metadata dict */
};
```

The extent table is derived during Code construction from PUSH/POP pairs (TRY, LOOP, FINALLY, PROMPT, etc.). It enables O(1) lookup of block membership and handler targets. See [bytecode-metadata-tables.md](bytecode-metadata-tables.md).

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

### Stack Operations
- `KOS_OP_NOOP`: No operation
- `KOS_OP_DUP`: Duplicate top N elements
- `KOS_OP_POP`: Pop N elements
- `KOS_OP_CONST`: Load constant from pool

### Control Flow
- `KOS_OP_AWAIT`: Await expression
- `KOS_OP_BREAK`: Break from loop
- `KOS_OP_CONTINUE`: Continue loop
- `KOS_OP_RAISE`: Raise exception
- `KOS_OP_RETURN`: Return value
- `KOS_OP_YIELD`: Yield value (sets YIELDS flag)
- `KOS_OP_YIELD_FROM`: Yield from iterable (sets YIELDS flag)

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
- `KOS_OP_RECUR`: Explicit recursion
- `KOS_OP_RECUR_NAMES`, `KOS_OP_RECUR_STAR`
- `KOS_OP_TAIL_CALL`: Tail call (replaces frame)
- `KOS_OP_TAIL_CALL_NAMES`, `KOS_OP_TAIL_CALL_STAR`
- `KOS_OP_TAIL_RECUR`: Tail recursion
- `KOS_OP_TAIL_RECUR_NAMES`, `KOS_OP_TAIL_RECUR_STAR`

### Block Control
- `KOS_OP_PUSH_FOREACH`: Push for-each loop (stub)
- `KOS_OP_PUSH_WITH`: Push with context (stub)
- `KOS_OP_PUSH_BEGIN`: Push begin block (stub)
- `KOS_OP_PUSH_CLASS`: Push class definition (stub)
- `KOS_OP_PUSH_FINALLY`: Push finally block (implemented)
- `KOS_OP_PUSH_LOOP`: Push while loop (stub)
- `KOS_OP_PUSH_NS`: Push namespace scope; arg = count of values to bind; opa=0: [NSPEC]; opa>0: [NSPEC, val0, ..., NAMES]; creates KosNamespace, pushes KosDynNs
- `KOS_OP_PUSH_TRY`: Push try block (implemented)
- `KOS_OP_CLEAR_FINALLY`, `KOS_OP_CLEAR_LOOP`, `KOS_OP_CLEAR_TRY`
- `KOS_OP_POP_WITH`, `KOS_OP_POP_BEGIN`, `KOS_OP_POP_FINALLY`, `KOS_OP_POP_LOOP`, `KOS_OP_POP_NS` (POP_NS implemented), etc.

### Delimited Continuations
- `KOS_OP_PUSH_PROMPT`: Push prompt delimiter (arg = prompt_id constant index)
- `KOS_OP_SET_PROMPT_HANDLER`: Set handler for prompt (arg = handler pc label)
- `KOS_OP_POP_PROMPT`: Pop prompt from dynamic chain
- `KOS_OP_SHIFT`: Capture continuation, abort to prompt (arg = prompt_id constant index)

### Dynamic Wind
- `KOS_OP_PUSH_WIND`: Push wind extent (arg = after_block label)
- `KOS_OP_POP_WIND`: Pop wind from dynamic chain
- `KOS_OP_END_WIND`: Sentinel that terminates hook block execution

### Continuation Invocation
- `KOS_OP_INVOKE_K`: Pop value then continuation from stack; invoke continuation with value
- `KOS_OP_INVOKE_K_VAR`: Pop value; look up name (constant index) in frame namespace; invoke that continuation with value. Used when continuation is bound by name (e.g. shift handler with PUSH_NS + VAR_STORE)

### Region Control
- `KOS_OP_PUSH_REGION`: Push region for block-local control (arg = handler label)
- `KOS_OP_POP_REGION`: Pop region from dynamic chain

### Object Protocol
- Comparisons: `KOS_OP_OBJECT_EQ`, `KOS_OP_OBJECT_NE`, `KOS_OP_OBJECT_LT`, etc.
- Predicates: `KOS_OP_OBJECT_IN`, `KOS_OP_OBJECT_IS`, `KOS_OP_OBJECT_IS_INSTANCE`, etc.
- Arithmetic: `KOS_OP_OBJECT_ADD`, `KOS_OP_OBJECT_SUBTRACT`, `KOS_OP_OBJECT_MULTIPLY`, etc.
- General: `KOS_OP_OBJECT_ITER`, `KOS_OP_OBJECT_AITER`, `KOS_OP_OBJECT_LEN`, etc.

### Iteration
- `KOS_OP_ITER_NEXT`: Get next from iterator
- `KOS_OP_AWAIT_NEXT`: Get next from async iterator

### Introspection
- `KOS_OP_GLOBALS`: Get globals dict
- `KOS_OP_LOCALS`: Get locals dict

### Exceptions
- `KOS_OP_EXCEPTION`: Load current exception
- `KOS_OP_EXCEPTION_CLEAR`: Clear exception
- `KOS_OP_EXCEPTION_MATCH`: Match exception type

### Compile-Time Only
- `KOS_OP_LABEL`: Jump target (not emitted)
- `KOS_OP_POSITION`: Source position (not emitted, stored in positions table)

## Opcode implementation status

Handlers live in `koskari/lib/eval.c`. Each opcode has a handler; it is either **fully implemented** or a **stub** that calls `op_unimplemented_stub` (runtime error if executed). There are no partially implemented opcodes.

- **Fully implemented**: Stack (NOOP, LABEL, POSITION, DUP, POP, CONST); RETURN, AWAIT; CALL (simple call); GLOBALS, LOCALS; variables (VAR, VAR_BIND, VAR_CLEAR, VAR_REF, VAR_STORE); fast locals (FAST, FAST_BIND, FAST_REF, FAST_STORE); namespace scope (PUSH_NS, POP_NS); exception ops (EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH, RAISE, CHECK_EXCEPTION); try/finally (PUSH_TRY, PUSH_FINALLY, POP_TRY, POP_FINALLY, CLEAR_TRY, CLEAR_FINALLY); delimited continuations (PUSH_PROMPT, SET_PROMPT_HANDLER, POP_PROMPT, SHIFT); INVOKE_K, INVOKE_K_VAR; all OBJECT_* opcodes.
- **Stubs (unimplemented)**: THEEND, BREAK, CONTINUE, YIELD, YIELD_FROM; ITER_NEXT, AWAIT_NEXT; all jumps and check-jumps except CHECK_EXCEPTION; FAST_CLEAR; all ATTR*, ITEM*, CONST_ITEM*; argument-building (BUILD_*, COMBINE_*); CALL_NAMES, CALL_STAR, CALL_ATTR*, RECUR*, TAIL_*; block control (PUSH_*/POP_*/CLEAR_* for foreach/with/begin/class/loop); PUSH_WIND, POP_WIND, END_WIND; PUSH_REGION, POP_REGION.

For the full categorized list, see [FEATURES.md](../FEATURES.md) § "Opcodes: implementation status".

## Creating and calling code (expressions and functions)

- **Expression**: `Code.expression(parent_ns)` returns an **Expression** (code + namespace). Calling it with `()` creates a **Coroutine** that runs that code in that namespace (no arguments).
- **Function**: `Code.function(formals, parent_ns)` returns a **Function** (code + formals + base namespace). Calling it with `(*args, **kwargs)` binds formals, builds a frame namespace, and creates a **Coroutine** that runs the code in that frame.
- **Execution**: Driving the Coroutine (e.g. Python `await` or `send`) invokes `koskari_eval()`. A Koskari **CALL** with a KosExpression or KosFunction pushes a new frame via `kos_koscall` and continues in the same loop. See [evaluator.md](evaluator.md) and [FEATURES.md](../FEATURES.md) § "Creating and calling functions and expressions".

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

Static metadata tables (location table, extent table, pc→extent lookup, JUMP locality validation) are implemented per **[bytecode-metadata-tables.md](bytecode-metadata-tables.md)**. Extents are derived during Code construction; malformed bytecode (unmatched PUSH/POP or non-local control-flow jumps) is rejected.

## Files

- `koskari/lib/bytecode.h`: Type definitions, enums, macros, function declarations
- `koskari/lib/bytecode.c`: Full implementation
- `koskari/lib/opcodes.incl`: Opcode definitions (included multiple times with different macros)
- `tests/native/test_bytecode.c`: Native unit tests
