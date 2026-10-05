# KOS_OP_OBJECT_COMPARE
- **Style**: CMPCOUNT. Arg packs a KosCompare selector (high byte) with
  the reducing count (low byte).
- **Stack**: `[..., a1, ..., aN] → [..., result]`. Chain-compare `N`
  stack values under the selector and push the result (`N >= 2`; raises
  `TypeError` otherwise).
- **Semantics**: The richcompare six (LT/LE/EQ/NE/GT/GE, ordinals 0-5,
  matching CPython's Py_LT..Py_GE) chain left-to-right and push the last
  richcompare result. The extended selectors push a bool and are
  strictly binary: IS / IS_NOT / IN / NOT_IN, and IS_INSTANCE /
  IS_SUBCLASS, whose second operand is one type or a tuple of types.
- **Compiler form:** Emitted by the comparison, predicate, and type-check
  specials with a `(selector, count)` pair, merged to the packed uint16
  at normalize. When immediately followed by `JUMP_FALSE`, `compress`
  fuses the pair into `CHECK_COMPARE`.

**Example:** `(instance? x #(int float))` -- value plus a tuple of types.

| index | opcode         | arg  | arg view           | comment |
|------:|----------------|------|--------------------|---------|
| 0     | FAST           | 0    | 0 (x)              | stack: [..., x] |
| 1     | FAST           | 1    | 1 (int)            | stack: [..., x, int] |
| 2     | FAST           | 2    | 2 (float)          | stack: [..., x, int, float] |
| 3     | BUILD_ARGS     | 2    | 2                  | stack: [..., x, (int, float)] |
| 4     | OBJECT_COMPARE | 2562 | (IS_INSTANCE x2)   | stack: [..., bool] |
