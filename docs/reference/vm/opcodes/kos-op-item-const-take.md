# KOS_OP_ITEM_CONST_TAKE
- **Style**: CONST (the key).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by the compress pipeline from `CONST key, ITEM_TAKE`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_TAKE.c`
