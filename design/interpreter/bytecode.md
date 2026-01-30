# Bytecode Format

## From python-sibilant

### CPython Bytecode Target

- Compiles to CPython bytecode (`.pyc` files)
- Version-specific targets (3.5, 3.6, 3.7)
- Uses Python's `dis` module for bytecode generation
- Line number table support for tracebacks

### Pseudops (Pseudo Operations)

Intermediate representation before bytecode generation:
- `pseudop_const(value)`: Load constant
- `pseudop_get_var(sym)`: Load variable
- `pseudop_set_var(sym)`: Store variable
- `pseudop_get_attr(sym)`: Get attribute
- `pseudop_call(nargs)`: Function call
- `pseudop_return()`: Return
- `pseudop_yield()`: Yield
- `pseudop_position(line, col)`: Set position
- And many more...

## From sibilant-ceval

### Custom Bytecode Format

**OpCode Enum**: Custom opcodes for koskari operations
- `OP_CONST`: Load constant
- `OP_VAR` / `OP_FAST`: Load variable (slow/fast path)
- `OP_VAR_STORE` / `OP_FAST_STORE`: Store variable
- `OP_ATTR`: Get attribute
- `OP_ATTR_STORE`: Set attribute
- `OP_ATTR_REF`: Get attribute reference
- `OP_ITEM`: Get item
- `OP_ITEM_STORE`: Set item
- `OP_ITEM_REF`: Get item reference
- `OP_CALL`: Function call
- `OP_CALL_NAMES`: Call with keyword names
- `OP_CALL_STAR`: Call with *args/**kwargs
- `OP_TAIL_CALL`: Tail call
- `OP_RECUR`: Recursive tail call
- `OP_RETURN`: Return
- `OP_YIELD`: Yield
- `OP_YIELD_FROM`: Yield from
- `OP_AWAIT`: Await
- `OP_AWAIT_NEXT`: Await next (async iteration)
- `OP_BUILD_FUNCTION`: Build function object
- `OP_BUILD_ARGS`: Build argument tuple
- `OP_BUILD_KWDS_NAMES`: Build keyword dict
- `OP_PUSH_NS`: Push namespace
- `OP_POP_NS`: Pop namespace
- `OP_PUSH_TRY`: Push try block
- `OP_POP_TRY`: Pop try block
- `OP_PUSH_FOREACH`: Push foreach loop
- `OP_POP_LOOP`: Pop loop
- `OP_PUSH_WITH`: Push with block
- `OP_POP_WITH`: Pop with block
- `OP_PUSH_AWAIT_FOREACH`: Push async foreach
- `OP_PUSH_AWAIT_WITH`: Push async with
- `OP_POP_AWAIT_WITH`: Pop async with
- `OP_JUMP`: Unconditional jump
- `OP_JUMP_TRUE`: Jump if true
- `OP_JUMP_FALSE`: Jump if false
- `OP_LABEL`: Label target
- `OP_POSITION`: Set source position
- `OP_RAISE`: Raise exception
- `OP_EXCEPTION`: Load exception
- `OP_EXCEPTION_MATCH`: Match exception type
- `OP_EXCEPTION_CLEAR`: Clear exception
- `OP_OBJECT_*`: Object method calls (add, sub, etc.)
- And many more...

### Bytecode Structure

**ByteCode**:
- `NamespaceSpec`: Namespace specification
- Instructions: List of `(OpCode, argument)` pairs
- Name: Function/expression name
- Filename: Source filename

**NamespaceSpec**:
- Name: Namespace name
- Local symbols: Tuple of local variable names
- Closure symbols: Tuple of closure variable names

**FunctionSpec**:
- Name: Function name
- FormalsSpec: Parameter specification
- ByteCode: Compiled bytecode

### Wide Bytecode (Proposed)

From `todo.md` ideas:
- Opcode with argument, line, col
- Block type, jumpA, jumpB per opcode
- Namespace as fast index offset
- Stack index per opcode
- Discard flag per opcode

## Migration Notes

### To Migrate

- Opcode definitions (from sibilant-ceval)
- Bytecode structure (NamespaceSpec, FunctionSpec)
- Position tracking
- Fast/slow variable access paths

### To Reconsider

- CPython bytecode target (abandoned due to version churn)
- Wide bytecode format (performance vs. complexity)
- Opcode compression/optimization
- Bytecode validation

### To Design

- Stable bytecode format (version-independent)
- Bytecode serialization format
- JIT compilation strategy
- Integration with `call/cc` (continuation representation in bytecode)
