# KOS_OP_PUSH_PARAM
- **Style**: CONST. Arg = constant pool index of names tuple `(name_0, name_1, ...)`.
- **Status**: Implemented.
- **Stack**: `[..., val_0, val_1, ..., val_{n-1}]` → `[]`. Values consumed; names come from constant.
- **Pushes:** KOS_DYN_PARAM (extent kind KOS_EXT_PARAM). Builds dict from names and values, enters param extent.
- **Invariants**: Constant at arg must be a tuple of names; stack must have at least `len(names)` values.
- **Example:** `(parameterize ((x 1) (y 2)) body)` compiles to push values 1, 2, then PUSH_PARAM with names `("x", "y")`.

| index | opcode     | arg | arg view     | comment                    |
|------:|------------|-----|--------------|----------------------------|
| 0     | CONST      | 1   | 1 (1)        | push 1                     |
| 1     | CONST      | 2   | 2 (2)        | push 2                     |
| 2     | PUSH_PARAM | 0   | 0 (names)    | pop 2 values, push param   |
| 3     | …          | …   | …            | body                       |
| N     | POP_PARAM  | 0   | 0            | pop param extent           |

