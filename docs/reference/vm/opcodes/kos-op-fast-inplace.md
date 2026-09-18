# KOS_OP_FAST_INPLACE
- **Style**: EXTRA (OPCOUNT, FASTVAR). Row a0 packs the KosOperand
  selector with the operand count (1 in all compiler emission); a1 = fast
  slot index.
- **Stack**: `[..., rhs] → [...]`. Augmented assignment on a fast slot:
  obj = deref(slot); obj OP= rhs; store back. No value pushed.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  VAR_INPLACE.
