# KOS_OP_FAST_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, FASTVAR, FASTVAR, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant, the object and key read from fast slots.
- **Cache**: one KOS_CACHE_ELECTOR slot; the site elects its operator's int or float body for the item and operand it first sees, finds the cell by an exact list or dict check inside the body, and otherwise runs the object protocol.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_INPLACE_CONST.c`
