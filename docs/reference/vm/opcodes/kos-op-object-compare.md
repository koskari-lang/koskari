# KOS_OP_OBJECT_COMPARE
- **Style**: CMPCOUNT. Arg packs a KosCompare selector (high byte) with
  the reducing count (low byte).
- **Status**: Implemented.
- **Stack**: `[..., a1, ..., aN] → [..., result]`. Chain-compare `N`
  stack values under the selector and push the result (`N >= 2`; raises
  `TypeError` otherwise).
- **Semantics**: The richcompare six (LT/LE/EQ/NE/GT/GE, ordinals 0-5,
  matching CPython's Py_LT..Py_GE) chain left-to-right and push the last
  richcompare result. The extended selectors push a bool: IS / IS_NOT /
  IN / NOT_IN are strictly binary; IS_INSTANCE / IS_SUBCLASS treat
  operands past the first as the match (tupled when `N > 2`).
- **Compiler form:** Emitted by the comparison, predicate, and type-check
  specials with a `(selector, count)` pair, merged to the packed uint16
  at normalize. When immediately followed by `JUMP_FALSE`, `compress`
  fuses the pair into `CHECK_COMPARE`.

**Example:** `(instance? x int float)` -- value plus two types.

| index | opcode         | arg  | arg view           | comment |
|------:|----------------|------|--------------------|---------|
| 0     | FAST           | 0    | 0 (x)              | stack: [..., x] |
| 1     | FAST           | 1    | 1 (int)            | stack: [..., x, int] |
| 2     | FAST           | 2    | 2 (float)          | stack: [..., x, int, float] |
| 3     | OBJECT_COMPARE | 2563 | (IS_INSTANCE x3)   | stack: [..., bool] |
