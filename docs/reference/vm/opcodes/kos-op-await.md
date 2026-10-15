# KOS_OP_AWAIT
- **Style**: COUNT (stack element count).
- **Arg**: `1` -- pop TOS (awaitable) and await it; the resolved value is pushed onto the operand stack, immediately or on resume. `0` -- bare cooperative suspend: pops nothing and suspends with `None` as the yielded value; on resume it pushes `None`, discarding any value sent in. `(await)` compiles to this form alone.

**Example:** Await a coroutine; TOS is the awaitable. On resume, result is pushed.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (coro)  | stack: [..., awaitable] |
| 1     | AWAIT  | 1   | 1        | pop one; suspend; on resume stack: [..., value] |
| 2     | RETURN | 1   | 1        | |
