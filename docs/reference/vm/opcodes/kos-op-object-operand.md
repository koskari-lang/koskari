# KOS_OP_OBJECT_OPERAND
- **Style**: OPCOUNT. Arg packs a KosOperand selector (high byte) with the
  reduce count (low byte).
- **Stack**: `[..., a1, ..., aN] → [..., result]`. Reduce the N operands
  left-to-right under the operator (`values[0]` is the seed), via
  `kos_operand_reduce` (`operator.c`) with its two-operand PyLong fast
  paths.
- **Compiler form:** Emitted by the arithmetic specials with an
  `(operator, count)` pair, merged to the packed uint16 at normalize.
  The consolidated replacement for the old per-operator `OBJECT_ADD` /
  `OBJECT_SUBTRACT` / ... family.
