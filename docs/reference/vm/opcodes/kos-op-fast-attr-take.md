# KOS_OP_FAST_ATTR_TAKE

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get attribute on object in fast slot,
  delete it, push value.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_TAKE.c`
