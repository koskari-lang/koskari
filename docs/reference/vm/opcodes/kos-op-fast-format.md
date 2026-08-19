# KOS_OP_FAST_FORMAT
- **Style**: FASTVAR. Arg = slot index.
- **Status**: Implemented.
- **Stack**: As VAR_FORMAT, with the object loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_FORMAT when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_FORMAT.c`
