# KOS_OP_POP_FOREACH
- **Style**: NOARG.
- **Stack**: Pop FOREACH region and drop its segment; push the loop result. An enabled region delivers the result stored on the region, or `None` when no CONTINUE set one; a region CLEAR_FOREACH disabled ran its else clause, and the clause's value, the top of the region's stack, is the result.
- **Invariants**: Top of chain must be KOS_DYN_FOREACH (paired with PUSH_FOREACH).
- **Implementation**: `koskari/lib/opcode/POP_FOREACH.c`
