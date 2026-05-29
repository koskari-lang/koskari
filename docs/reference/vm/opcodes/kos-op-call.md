# KOS_OP_CALL
- **Style**: COUNT. Arg = number of positional arguments.
- **Status**: Implemented.
- **Stack**: `[..., callable, arg_1, ..., arg_n] → [..., result]`. KosExpression/KosFunction: new frame pushed, in-loop execution. Python callable: may suspend on awaitable.
- **Execution model**: Only the **call result** is ever auto-awaited: when a Python call returns a coroutine/awaitable, the VM awaits it (flattened) and pushes the final value. All other opcodes (CONST, loads, getattr, subscript, binary ops, yield-from results, etc.) push their value with no automatic await; use the AWAIT opcode to await explicitly.
- **Invariants**: Stack has at least 1 + arg elements. See execution-invariants for in-loop flattening.

**Example:** Call with 1 argument. Pool: 0 = fn, 1 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | CONST  | 1   | 1 (42)   | stack: [..., callable, 42] |
| 2     | CALL   | 1   | 1        | pop callable + 1 arg; push result. stack: [..., result] |
| 3     | RETURN | 1   | 1        | |

