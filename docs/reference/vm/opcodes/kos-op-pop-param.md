# KOS_OP_POP_PARAM
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No change.
- **Pops:** KOS_DYN_PARAM from dynamic chain. Restores the cells the extent displaced in the context's shadow table.
- **Invariants**: Dyn top must be KOS_DYN_PARAM; otherwise RuntimeError.
- **Example:** See PUSH_PARAM table.

