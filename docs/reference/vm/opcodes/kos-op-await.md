# KOS_OP_AWAIT
- **Style**: COUNT (stack element count).
- **Arg**: `1` — pop TOS (awaitable), dispatch; may return `EVAL_AWAIT(val)` or push the resolved value; on resume from a Python awaitable, result is pushed onto the operand stack. `0` — bare cooperative suspend: no stack pop or push; returns `EVAL_AWAIT(Py_None)` (no `py_await_iter`); `revive_await` does not push the resume value onto the operand stack (e.g. next instruction may be `CONST None` for `(await)`).

**Example:** Await a coroutine; TOS is the awaitable. On resume, result is pushed.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (coro)  | stack: [..., awaitable] |
| 1     | AWAIT  | 1   | 1        | pop one; suspend; on resume stack: [..., value] |
| 2     | RETURN | 1   | 1        | |
