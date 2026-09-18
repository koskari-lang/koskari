# KOS_OP_OBJECT_COMPARE_VAR
- **Style**: EXTRA (CMP, CONST, VAR, CONST). Row = bare selector, low
  bound (pool), middle operand name, high bound (pool). Schema-static
  slots: no source bits, the shape is the opcode.
- **Stack**: `[...] → [..., result]`. The ternary bounds idiom
  (`const < x < const`) with the middle operand resolved by name:
  chain-compare the three operands under the selector and push the
  chain result.
- **Invariants**: Selector is a valid KosCompare ordinal.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when the
  exact CONST / VAR / CONST feeder shape meets an arity-3 comparison.
  In value position the OBJECT_COMPARE quartet collapses to this row
  alone; in branch position the arity-3 CHECK_COMPARE splits into this
  row plus `JUMP_FALSE` (five fields cannot ride one row). Lowers to
  `OBJECT_COMPARE_FAST` through the generic `convert_fast` path. Cold
  handler.

**Example:** `(< 0 x 100)` by name.

| index | opcode             | arg | arg view                       | comment |
|------:|--------------------|-----|--------------------------------|---------|
| 0     | OBJECT_COMPARE_VAR | 0   | (LT, c#0 (0), v#1 ('x'), c#2 (100)) | stack: [..., bool] |
