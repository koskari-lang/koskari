# KOS_OP_CLEAR_LOOP
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No stack effect. Disables the top LOOP region (sets `enabled` to 0) and clears the loop segment. Used on normal exit from a while-loop body (before POP_LOOP), or on iterator exhaustion in patterns that only use LOOP extents.
- **Invariants**: Top of chain must be KOS_DYN_LOOP. Use CLEAR_FOREACH for FOREACH extents.
- **Example:** On `break` from a while loop: CLEAR_LOOP (and possibly CLEAR_TRY) then jump to break_pc; POP_LOOP at target.
- **Implementation**: `koskari/lib/opcode/CLEAR_LOOP.c`

