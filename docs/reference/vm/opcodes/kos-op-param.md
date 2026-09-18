# KOS_OP_PARAM
- **Style**: EXTRA. Schema CONST (parameter name), COUNT (0 or 1 stacked defaults).
- **Stack**: `[]` → `[value]` at count 0; `[default]` → `[value]` at count 1.
- **Resolves:** the enclosing parameterize extent (the context's shadow table), then the root scope of the context's domain. Writes to neither, and creates no entry on a miss.
- **Invariants**: On a miss at count 0, raises NameError. At count 1 the stacked default answers a miss, and is dropped on a hit.
- **Example:** `(parameterq x)` compiles to PARAM `("x", 0)`; `(parameterq x (compute))` compiles the expression, then PARAM `("x", 1)`.

| index | opcode | arg | arg view      | comment                       |
|------:|--------|-----|---------------|-------------------------------|
| 0     | CONST  | 0   | "fallback"    | push the default              |
| 1     | PARAM  | 0   | ("x", 1)      | resolve x, else keep default  |

