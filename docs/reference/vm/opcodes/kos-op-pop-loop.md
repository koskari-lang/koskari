# KOS_OP_POP_LOOP
- **Style**: NOARG.
- **Stack**: Pop LOOP region and drop its segment; push the loop result. An enabled region delivers the result stored on the region; a region CLEAR_LOOP disabled ran its else clause, and the clause's value, the top of the region's stack, is the result.
- **Invariants**: Top of chain must be KOS_DYN_LOOP (paired with PUSH_LOOP).
- **Implementation**: `koskari/lib/opcode/POP_LOOP.c`

