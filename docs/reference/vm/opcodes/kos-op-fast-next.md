# KOS_OP_FAST_NEXT
- **Style**: EXTRA (FASTVAR, COUNT). Row = iterator slot, n (1 or 2).
- **Stack**: As VAR_NEXT, with the iterator loaded from its fast slot.
- **Compiler form:** Post-compile only: the generic `convert_fast` lowering of VAR_NEXT when the iterator name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_NEXT.c` (cold handler)
