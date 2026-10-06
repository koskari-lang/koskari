# KOS_OP_CLEAR_FOREACH
- **Style**: NOARG.
- **Stack**: Clears the loop segment. Disables the top FOREACH region (sets `enabled` to 0): a BREAK or CONTINUE issued while it is disabled passes through it to the enclosing loop, and POP_FOREACH takes the loop result from the region's stack. Emitted when a for-each iterator is exhausted naturally, ahead of an else clause.
- **Invariants**: Top of chain must be KOS_DYN_FOREACH. Use CLEAR_LOOP for LOOP extents.
- **Implementation**: `koskari/lib/opcode/CLEAR_FOREACH.c`

