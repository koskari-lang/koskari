# KOS_OP_POP_LOOP
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Drop the region's segment (no promotion); pop LOOP region; push loop result from the region (stored on the region, not from the block).
- **Invariants**: Top of chain must be KOS_DYN_LOOP (paired with PUSH_LOOP).
- **Implementation**: `koskari/lib/opcode/POP_LOOP.c`

