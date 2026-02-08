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

    PyCodeObject *faux;   /* faux code object for tracebacks */
    PyObject *extras;     /* extra metadata dict */
};
```

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

### Variables
- `KOS_OP_VAR`: Load variable by name
- `KOS_OP_VAR_BIND`: Bind reference
- `KOS_OP_VAR_CLEAR`: Clear variable
- `KOS_OP_VAR_REF`: Get variable reference
- `KOS_OP_VAR_STORE`: Store to variable

### Fast Variables (Index-Based)
- `KOS_OP_FAST`: Load fast local
- `KOS_OP_FAST_BIND`: Bind fast local reference
- `KOS_OP_FAST_CLEAR`: Clear fast local
- `KOS_OP_FAST_REF`: Get fast local reference
- `KOS_OP_FAST_STORE`: Store to fast local

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
- `KOS_OP_PUSH_FOREACH`: Push for-each loop
- `KOS_OP_PUSH_WITH`: Push with context
- `KOS_OP_PUSH_BEGIN`: Push begin block
- `KOS_OP_PUSH_CLASS`: Push class definition
- `KOS_OP_PUSH_FINALLY`: Push finally block
- `KOS_OP_PUSH_LOOP`: Push while loop
- `KOS_OP_PUSH_NS`: Push namespace
- `KOS_OP_PUSH_TRY`: Push try block
- `KOS_OP_CLEAR_FINALLY`, `KOS_OP_CLEAR_LOOP`, `KOS_OP_CLEAR_TRY`
- `KOS_OP_POP_WITH`, `KOS_OP_POP_BEGIN`, `KOS_OP_POP_FINALLY`, etc.

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
- `KOS_OP_INVOKE_K`: Invoke continuation from TOS

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

/* Macros */
KosCode_FLAGS(code)
KosCode_YIELDS(code)
KosCode_CODE_LEN(code)
KosCode_GET_OP(code, i)
KosCode_GET_ARG(code, i)
KosCode_CPOOL_LEN(code)
KosCode_GET_CONST(code, i)
```

## Files

- `koskari/lib/bytecode.h`: Type definitions, enums, macros, function declarations
- `koskari/lib/bytecode.c`: Full implementation
- `koskari/lib/opcodes.incl`: Opcode definitions (included multiple times with different macros)
- `tests/native/test_bytecode.c`: Native unit tests
