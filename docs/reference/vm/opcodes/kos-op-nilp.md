# KOS_OP_NILP

- **Style**: NOARG.
- **Stack**: `[..., x] → [..., bool]`. Pop TOS; push `True` when it is an
  empty-list singleton (`nil` or bracket-nil), else `False`.
- **Compiler form:** `Compiler.compress` lowers NILP followed by JUMP_FALSE
  into one [CHECK_NIL](kos-op-check-nil.md).
- **Implementation**: `koskari/lib/opcode/NILP.c`
