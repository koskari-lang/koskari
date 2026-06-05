# KOS_OP_POP_FINALLY
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Same structural behaviour as POP_TRY for the finally block: promote one value via PopBlock(1), drop block, pop region.
- **Invariants**: Top of chain must be KOS_DYN_FINALLY.

