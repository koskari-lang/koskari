# KOS_OP_BUILD_FUNCTION

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = number of keyword default values (0 when no keyword defaults).
- **Status**: Implemented.
- **Stack**: For arg = 0: `[..., fspec] → [..., function]`. For arg > 0: `[..., val_1, ..., val_N, names_tuple, fspec] → [..., function]`. The VM pops fspec, then (when arg > 0) pops the names tuple and N values via PopTailDictNames (TOS = names, below it the N default values in order); builds a defaults dict and creates the function from the spec. Build function from function spec (e.g. from constant pool).
- **Invariants**: TOS (after positionals) is function spec; when arg > 0, names_tuple must be a tuple of N names and the stack must have N values below it.

**Example:** Build function from spec in constant pool. Pool: 0 = function_spec. Covered by `tests/lib/test_ceval.py`: `test_build_function_returns_function`, `test_build_function_returned_function_is_callable`.

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 (fspec)| stack: [..., fspec] |
| 1     | BUILD_FUNCTION | 0 | 0        | stack: [..., function] |
| 2     | RETURN       | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
