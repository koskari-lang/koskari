# KOS_OP_OBJECT_INPLACE
- **Style**: OPCOUNT. Arg packs a KosOperand selector (high byte) with the
  reduce count (low byte).
- **Status**: Implemented.
- **Stack**: `[..., a1, ..., aN] → [..., result]`. In-place reduce the N
  operands left-to-right (`values[0]` is the accumulator seed) via
  `kos_inplace_reduce` (`operator.c`), the `PyNumber_InPlace*` protocol
  family. The consolidated replacement for the old per-operator
  `OBJECT_INPLACE_*` family.
