# KOS_OP_CONST_ITEM_STORE
- **Style**: CONST. Arg = constant pool index of key.
- **Status**: Implemented.
- **Stack**: `[..., obj, value] → [..., obj]`. Set item with constant key.
- **Invariants**: Key from pool.

**Example:** obj["key"] = value. Pool: 0 = "key".

| index | opcode        | arg | arg view | comment |
|------:|---------------|-----|----------|---------|
| 0     | VAR           | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST         | 1   | 1 (42)   | stack: [..., obj, 42] |
| 2     | CONST_ITEM_STORE | 0 | 0 ('key')| obj['key'] = 42; stack: [..., obj] |

