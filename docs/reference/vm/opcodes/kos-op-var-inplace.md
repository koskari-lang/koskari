# KOS_OP_VAR_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR). Row a0 packs the KosOperand selector
  with the operand count (1 in all compiler emission); a1 = constant pool
  index of the variable name.
- **Status**: Implemented.
- **Stack**: `[..., rhs] → [...]`. Augmented assignment through the frame
  namespace: obj = deref(name); obj OP= rhs; store back. No value pushed.
- **Compiler form:** Emitted by the inplace specials (`set!` with an
  arithmetic rhs, `incr!`, ...). Lowers to FAST_INPLACE when the
  name has a fast slot. With a constant rhs, `compress` fuses the CONST
  into VAR_INPLACE_CONST instead.
