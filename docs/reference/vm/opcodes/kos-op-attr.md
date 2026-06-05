# KOS_OP_ATTR
- **Style**: CONST. Arg = constant pool index of attribute name (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj, value]`. Get attribute; push value.
- **Invariants**: TOS is the object. Name in constant pool.
- **Compiler form:** Pre-compile arg = attribute name (string); post-compile arg = pool index.

**Example:** Get obj.name. Pool: 0 = "name".

| index | opcode | arg | arg view   | comment |
|------:|--------|-----|------------|---------|
| 0     | VAR    | 0   | 0 ('obj')  | stack: [..., obj] |
| 1     | ATTR   | 1   | 1 ('name') | stack: [..., obj, value] |
| 2     | RETURN | 1   | 1          | |

