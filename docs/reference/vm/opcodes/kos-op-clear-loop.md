# KOS_OP_CLEAR_LOOP
- **Style**: NOARG.
- **Stack**: Clears the loop segment. Disables the top LOOP region (sets `enabled` to 0): a BREAK or CONTINUE issued while it is disabled passes through it to the enclosing loop, and POP_LOOP takes the loop result from the region's stack. Emitted on normal exit from a while-loop body ahead of an else clause.
- **Invariants**: Top of chain must be KOS_DYN_LOOP. Use CLEAR_FOREACH for FOREACH extents.
- **Implementation**: `koskari/lib/opcode/CLEAR_LOOP.c`

