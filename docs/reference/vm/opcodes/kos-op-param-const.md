# KOS_OP_PARAM_CONST
- **Style**: EXTRA. Schema CONST (parameter name), CONST (default value).
- **Status**: Implemented.
- **Stack**: `[]` → `[value]`.
- **Resolves:** as PARAM, with the default coming from the constant pool rather than the stack. Never raises for an unbound name.
- **Invariants**: Both constants must be present in the pool.
- **Example:** `(parameterq x 42)` compiles to PARAM_CONST `("x", 42)`.

