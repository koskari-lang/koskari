# KOS_OP_OBJECT_COMPARE_FAST
- **Style**: EXTRA (CMP, CONST, FASTVAR, CONST). Row = bare selector,
  low bound (pool), middle operand fast slot, high bound (pool).
  Schema-static slots: no source bits, the shape is the opcode.
- **Stack**: `[...] → [..., result]`. The ternary bounds idiom
  (`const < x < const`): chain-compare the three operands under the
  selector and push the chain result (for JUMP_FALSE / JUMP_TRUE or
  value use).
- **Invariants**: Selector is a valid KosCompare ordinal.
- **Compiler form:** Post-compile only: the generic `convert_fast`
  lowering of `OBJECT_COMPARE_VAR` when the middle name has a fast
  slot. A bounds check in value position is one dispatch; in branch
  position, two (this row plus the jump), down from five before
  fusion.

**Example:** `(< 0 x 100)` on fast slot 0.

| index | opcode              | arg | arg view                        | comment |
|------:|---------------------|-----|---------------------------------|---------|
| 0     | OBJECT_COMPARE_FAST | 0   | (LT, c#0 (0), fast 0 ("x"), c#1 (100)) | stack: [..., bool] |
