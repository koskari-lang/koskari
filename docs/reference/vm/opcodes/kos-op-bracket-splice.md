# KOS_OP_BRACKET_SPLICE
- **Style**: COUNT. Arg = number of segments on stack (bottom = first segment,
  top = last segment).
- **Stack**: `[..., seg_0, ..., seg_{count-1}] → [..., result]` where result is
  the concatenation of all segments, with a bracket pair as its leading cell.
  The twin of [PAIR_SPLICE](kos-op-pair-splice.md) for bracket lists; segments
  are accepted as PAIR_SPLICE accepts them. count 0 → push bracket-nil.
- **Invariants**: Only the leading cell is a bracket pair; the rest of the
  spine is plain pairs ending in nil. A bracket list prints by its leading
  cell, so the result prints as `[ ]`. If every segment is empty the result is
  bracket-nil.
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
| 3     | BRACKET_SPLICE | 3   | 3               | stack: [..., [a b_0 ... b_n c]] |
