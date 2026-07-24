# KOS_OP_FAST_ITER
- **Style**: FASTVAR. Arg = slot index.
- **Status**: Implemented.
- **Stack**: As VAR_ITER, with the iterable loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_ITER when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITER.c`
