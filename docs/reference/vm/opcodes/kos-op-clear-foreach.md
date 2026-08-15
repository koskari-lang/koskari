# KOS_OP_CLEAR_FOREACH
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No stack effect. Disables the top FOREACH region (sets `enabled` to 0) and clears the loop segment. Used when a for-each iterator is exhausted naturally (before POP_FOREACH).
- **Invariants**: Top of chain must be KOS_DYN_FOREACH. Use CLEAR_LOOP for LOOP extents.
- **Implementation**: `koskari/lib/opcode/CLEAR_FOREACH.c`

