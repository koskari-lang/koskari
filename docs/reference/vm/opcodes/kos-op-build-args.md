# KOS_OP_BUILD_ARGS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = number of positional arguments.
- **Status**: Implemented.
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., tuple]`. Pop n elements and push a tuple of them.
- **Example:** Build empty args. Covered by `tests/lib/opcode/test_arg_opcodes.py`.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | BUILD_ARGS| 0   | 0        | stack: [..., ()] |
| 1     | RETURN    | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
