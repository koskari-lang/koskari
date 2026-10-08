# KOS_OP_ATTR
- **Style**: CONST. Arg = constant pool index of attribute name (post-compile).
- **Stack**: `[..., obj] → [..., value]`. Get attribute; the value replaces the object.
- **Invariants**: TOS is the object. Name in constant pool.
- **Compiler form:** Pre-compile arg = attribute name (string); post-compile arg = pool index.

**Example:** Get obj.name. Pool: 0 = "name".

| index | opcode | arg | arg view   | comment |
|------:|--------|-----|------------|---------|
| 0     | VAR    | 0   | 0 ('obj')  | stack: [..., obj] |
| 1     | ATTR   | 1   | 1 ('name') | stack: [..., value] |
| 2     | RETURN | 1   | 1          | |

