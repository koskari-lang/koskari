# KOS_OP_PAIR_SPLICE
- **Style**: COUNT. Arg = number of segments on stack (bottom = first segment,
  top = last segment).
- **Stack**: `[..., seg_0, ..., seg_{count-1}] → [..., result]` where result is
  the concatenation of all segments as a single proper list. Each segment may be:
  nil (contributes nothing), a pair (proper or improper; its elements are
  appended), a Python tuple (its elements are appended), or any iterable.
  count 0 → push nil.
- **Invariants**: All segments must be iterable; non-iterable values raise
  TypeError. The result is always a proper pair list (or nil if all segments are
  empty). Dotted-pair segments are walked until nil; the implicit nil tail of
  the last segment is not included.
- **Use-case**: Emitted by the quasiquote compiler when at least one
  `unquote-splicing` site is present in a list. Each constant-only segment is
  folded into a single `CONST tuple` (which PAIR_SPLICE accepts natively),
  reducing instruction count significantly for mostly-literal lists.

**Example:** Splice a variable list `b` between constants.
`` `(a ,@b c) `` compiles to roughly:

| index | opcode      | arg | arg view        | comment |
|------:|-------------|-----|-----------------|---------|
| 0     | CONST       | 0   | 0 ((sym a,))    | segment: tuple `(a,)` |
| 1     | VAR         | 1   | 1 (b)           | splice value |
| 2     | CONST       | 2   | 2 ((sym c,))    | segment: tuple `(c,)` |
| 3     | PAIR_SPLICE | 3   | 3               | stack: [..., (a . (b_0 ... b_n . (c)))] |


## 6. Mode control

