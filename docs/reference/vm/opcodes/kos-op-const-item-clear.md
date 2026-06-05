# KOS_OP_CONST_ITEM_CLEAR
- **Style**: CONST. Arg = constant pool index of key.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj]`. Delete item with constant key.
- **Invariants**: Key from pool.

**Example:** del obj["key"]. Pool: 0 = "key".

| index | opcode        | arg | arg view | comment |
|------:|---------------|-----|----------|---------|
| 0     | VAR           | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST_ITEM_CLEAR | 0 | 0 ('key')| del obj['key']; stack: [..., obj] |

