# KOS_OP_CLEAR_TRY
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No stack effect. Clears TRY region state on non-local exit; does not touch the stack block; POP_TRY drops the block at the target.
- **Invariants**: Used with structured exception and non-local control flow.

