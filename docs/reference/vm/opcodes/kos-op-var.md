# KOS_OP_VAR
- **Style**: VAR. Arg = constant pool index of variable name (post-compile).
- **Stack**: `[...] → [..., value]`. Load value bound to name in frame namespace.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name (string/symbol); finalize stores name in pool and rewrites arg to pool index. May be lowered to FAST with slot index for locals.

**Example:** Load variable "x". Pool: 0 = "x".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('x')  | stack: [..., value] |
| 1     | RETURN | 1   | 1        | |

