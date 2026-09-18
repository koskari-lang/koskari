# KOS_OP_CLEAR_TRY
- **Style**: NOARG.
- **Stack**: No stack effect. Clears TRY region state on non-local exit; does not touch the segment; POP_TRY drops the segment at the target.
- **Invariants**: Used with structured exception and non-local control flow.

