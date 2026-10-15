# KOS_OP_PAIR_SPLICE
- **Style**: EXTRA (COUNT, COUNT). Row = count of stacked values (bottom =
  first), tail flag (0 or 1).
- **Stack**: tail 0: `[..., seg_0, ..., seg_{count-1}] → [..., result]` where
  result is the concatenation of all segments as a single proper list.
  tail 1: `[..., seg_0, ..., seg_{count-2}, tail] → [..., result]` where the
  top value is the tail and the count minus one values below it are the
  segments; the last cell's cdr is the tail. Each segment may be: nil
  (contributes nothing), a pair (proper or improper; its elements are
  appended), a Python tuple (its elements are appended), or any iterable.
  count 0 → push nil.
- **Invariants**: All segments must be iterable; non-iterable values raise
  TypeError. With tail 0 the result is a proper pair list, or nil if all
  segments are empty. With tail 1 the result ends in the tail, or is the
  tail itself if all segments are empty. Dotted-pair segments are walked
  until nil; the implicit nil tail of the last segment is not included. A
  tail flag other than 0 or 1, or tail 1 with count 0, raises ValueError.
- **Use-case**: Emitted by the quasiquote compiler when at least one
  `unquote-splicing` site is present in a `( )` list; a `[ ]` list uses
  [BRACKET_SPLICE](kos-op-bracket-splice.md). Each constant-only segment is
  folded into a single `CONST tuple` (which PAIR_SPLICE accepts natively),
  reducing instruction count significantly for mostly-literal lists. A
  dotted tail after a splice, `` `(a ,@xs . b) ``, sets the tail flag and
  gives `(cons a (append xs b))`.

**Example:** Splice a variable list `b` between constants.
`` `(a ,@b c) `` compiles to roughly:

| index | opcode      | arg | arg view        | comment |
|------:|-------------|-----|-----------------|---------|
| 0     | CONST       | 0   | 0 ((sym a,))    | segment: tuple `(a,)` |
| 1     | VAR         | 1   | 1 (b)           | splice value |
| 2     | CONST       | 2   | 2 ((sym c,))    | segment: tuple `(c,)` |
| 3     | PAIR_SPLICE | 0   | 0 (3, 0)        | stack: [..., (a . (b_0 ... b_n . (c)))] |

**Example:** A splice before a dotted tail. `` `(a ,@b . c) `` compiles to
roughly:

| index | opcode      | arg | arg view        | comment |
|------:|-------------|-----|-----------------|---------|
| 0     | CONST       | 0   | 0 ((sym a,))    | segment: tuple `(a,)` |
| 1     | VAR         | 1   | 1 (b)           | splice value |
| 2     | CONST       | 2   | 2 (sym c)       | tail |
| 3     | PAIR_SPLICE | 0   | 0 (3, 1)        | stack: [..., (a b_0 ... b_n . c)] |


## 6. Mode control

