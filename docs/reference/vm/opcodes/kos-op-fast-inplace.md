# KOS_OP_FAST_INPLACE
- **Style**: EXTRA (OPERAND, FASTVAR). Row a0 = the bare KosOperand
  selector; a1 = fast slot index.
- **Stack**: `[..., rhs] → [...]`. Augmented assignment on a fast slot:
  obj = deref(slot); obj OP= rhs; store back. No value pushed.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  VAR_INPLACE.
