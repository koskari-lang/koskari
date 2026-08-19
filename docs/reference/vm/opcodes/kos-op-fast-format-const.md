# KOS_OP_FAST_FORMAT_CONST
- **Style**: EXTRA (FASTVAR, CONST). Row = obj slot, rules string (pool).
- **Status**: Implemented.
- **Stack**: As VAR_FORMAT_CONST, with the object loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_FORMAT_CONST when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_FORMAT_CONST.c`
