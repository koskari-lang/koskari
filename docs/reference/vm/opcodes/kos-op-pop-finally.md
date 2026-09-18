# KOS_OP_POP_FINALLY
- **Style**: NOARG.
- **Stack**: Same structural behaviour as POP_TRY for the finally segment: promote one value, drop the segment, pop the region.
- **Invariants**: Top of chain must be KOS_DYN_FINALLY.

