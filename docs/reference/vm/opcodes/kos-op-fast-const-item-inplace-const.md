# KOS_OP_FAST_CONST_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, FASTVAR, CONST, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant, the object read from a fast slot and the key and operand pool constants.
- **Cache**: one KOS_CACHE_ELECTOR slot; the site elects its operator's int or float body for the item and operand it first sees, finds the cell by an exact list or dict check inside the body, and otherwise runs the object protocol.
- **Implementation**: `koskari/lib/opcode/FAST_CONST_ITEM_INPLACE_CONST.c`
