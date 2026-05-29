# KOS_OP_CLEAR_FINALLY
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No stack effect. Clears FINALLY region state on non-local exit; does not pop the region (POP_FINALLY does at target). Does not touch the stack block (only disables the region); the block is dropped by POP_FINALLY or unwind.
- **Example:** Exception or unwind path before transferring to a finally handler.

