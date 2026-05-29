# KOS_OP_VAR_ATTR
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get attribute on object held in namespace by name. Equivalent to ATTR but the object is loaded from the frame namespace, not from TOS.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR.c`

