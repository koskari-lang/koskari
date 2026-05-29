# KOS_OP_AWAIT

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT (stack element count).
- **Status**: Implemented.
- **Arg**: `1` — pop TOS (awaitable), dispatch; may suspend with `KOS_EV_AWAIT` or push the resolved value; on resume from a Python awaitable, result is pushed onto the operand stack. `0` — bare cooperative suspend: no stack pop or push; suspends with `KOS_EV_AWAIT` and `Py_None` as the event payload (no `py_await_iter`); `revive_await` does not push the resume value onto the operand stack (e.g. next instruction may be `CONST None` for `(compile/await)`).

**Example:** Await a coroutine; TOS is the awaitable. On resume, result is pushed.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (coro)  | stack: [..., awaitable] |
| 1     | AWAIT  | 1   | 1        | pop one; suspend; on resume stack: [..., value] |
| 2     | RETURN | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
