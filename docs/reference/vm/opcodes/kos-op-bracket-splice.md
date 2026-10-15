# KOS_OP_BRACKET_SPLICE
- **Style**: EXTRA (COUNT, COUNT). Row = count of stacked values (bottom =
  first), tail flag (0 or 1).
- **Stack**: tail 0: `[..., seg_0, ..., seg_{count-1}] → [..., result]` where
  result is the concatenation of all segments, with a bracket pair as its
  leading cell. tail 1: `[..., seg_0, ..., seg_{count-2}, tail] → [...,
  result]` where the top value is the tail and the last cell's cdr is that
  tail. The twin of [PAIR_SPLICE](kos-op-pair-splice.md) for bracket lists;
  segments and the tail flag are accepted as PAIR_SPLICE accepts them. count
  0 → push bracket-nil.
- **Invariants**: Only the leading cell is a bracket pair; the rest of the
  spine is plain pairs ending in nil, or in the tail with tail 1. A bracket
  list prints by its leading cell, so the result prints as `[ ]`. If every
  segment is empty the result is bracket-nil with tail 0, or the tail itself
  with tail 1.
- **Use-case**: Emitted by the quasiquote compiler for a `[ ]` list holding at
  least one `unquote-splicing` site. No name is looked up, so a local or global
  binding of any name leaves the result unchanged.

**Example:** Splice a variable list `b` between constants in a bracket list.
`` `[a ,@b c] `` compiles to roughly:

| index | opcode         | arg | arg view        | comment |
|------:|----------------|-----|-----------------|---------|
| 0     | CONST          | 0   | 0 ((sym a,))    | segment: tuple `(a,)` |
| 1     | VAR            | 1   | 1 (b)           | splice value |
| 2     | CONST          | 2   | 2 ((sym c,))    | segment: tuple `(c,)` |
| 3     | BRACKET_SPLICE | 0   | 0 (3, 0)        | stack: [..., [a b_0 ... b_n c]] |
