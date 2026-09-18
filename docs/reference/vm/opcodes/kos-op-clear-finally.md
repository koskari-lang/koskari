# KOS_OP_CLEAR_FINALLY
- **Style**: NOARG.
- **Stack**: No stack effect. Clears FINALLY region state on non-local exit; does not pop the region (POP_FINALLY does at target). Does not touch the segment (only disables the region); the segment is dropped by POP_FINALLY or unwind.
- **Example:** Exception or unwind path before transferring to a finally handler.

