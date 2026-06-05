# KOS_OP_PARAM
- **Style**: CONST. Arg = constant pool index of name (string) for ParamRef key.
- **Status**: Implemented.
- **Stack**: `[]` → `[ParamRef]`. Pushes ParamRef for the given key (e.g. for `(use-param X)`).
- **Invariants**: Constant at arg must be hashable (typically PyUnicode).
- **Example:** `(use-param x)` compiles to PARAM with constant `"x"`, then binds the result.

