# KOS_OP_ATTR_STORE
- **Style**: CONST. Arg = constant pool index of attribute name.
- **Stack**: `[..., obj, value] → [...]`. Set attribute on obj; pop both.
- **Invariants**: Below TOS is object; name in pool.

**Example:** Set obj.x = TOS. Pool: 0 = "x".

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST      | 1   | 1 (10)   | stack: [..., obj, 10] |
| 2     | ATTR_STORE | 2   | 2 ('x')  | obj.x = 10; stack: [...] |

