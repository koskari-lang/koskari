# KOS_OP_CHECK_COMPARE
- **Style**: EXTRA (CMP, COUNT, LABEL, COUNT). Row = KosCompare selector,
  operand count, target pc, negate flag.
- **Stack**: `[..., a1, ..., aN] → [...]`. Chain-compare the top COUNT
  operands under the selector, negated when the negate flag is set;
  jump to LABEL when the chain fails. The operands are dropped
  either way. Raises `TypeError` when `COUNT < 2`.
- **Invariants**: Selector is a valid KosCompare ordinal. The richcompare
  six (LT/LE/EQ/NE/GT/GE, ordinals 0-5, matching CPython's Py_LT..Py_GE)
  chain over any COUNT; the extended selectors (IS, IS_NOT, IN, NOT_IN,
  IS_INSTANCE, IS_SUBCLASS) require COUNT == 2.
- **Compiler form:** Produced by `Compiler.compress` fusing
  `OBJECT_COMPARE` + `JUMP_FALSE` into one instruction, and
  `OBJECT_COMPARE` + `JUMP_TRUE` into the same with the flag set. Never
  emitted directly by
  specials.

**Example:** Branch unless `1 < x < 100`. Selector LT = 0.

| index | opcode        | arg | arg view          | comment |
|------:|---------------|-----|-------------------|---------|
| 0     | CONST         | 0   | 0 (1)             | stack: [..., 1] |
| 1     | FAST          | 0   | 0 (x)             | stack: [..., 1, x] |
| 2     | CONST         | 1   | 1 (100)           | stack: [..., 1, x, 100] |
| 3     | CHECK_COMPARE | 0   | (LT, 3, -> 6, 0)  | jump to 6 unless 1 < x < 100 |
