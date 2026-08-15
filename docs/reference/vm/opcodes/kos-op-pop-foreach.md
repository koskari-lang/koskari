# KOS_OP_POP_FOREACH
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Drop the region's segment (no promotion); pop FOREACH region; push loop result from the region (stored on the region, not from the block). When no BREAK or CONTINUE set a result, push `None`.
- **Invariants**: Top of chain must be KOS_DYN_FOREACH (paired with PUSH_FOREACH).
- **Implementation**: `koskari/lib/opcode/POP_FOREACH.c`
