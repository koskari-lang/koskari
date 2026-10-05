# KOS_OP_BRACKET_CONS
- **Style**: COUNT. Arg = number of values on stack (bottom = first element,
  top = tail).
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result is
  the bracket pair chain `[e_0 . [e_1 . ... [e_{count-1}] ...]]`. The twin of
  [CONS](kos-op-cons.md) over bracket pairs, built with
  KosBracketPair_ConsFromPtr. count 0 → push bracket-nil.
- **Invariants**: Each `e_i` may be any Python object. The tail may be any
  object; a bracket-nil tail makes the chain a proper bracket list.
- **Use-case**: Emitted by `compile/bracket-cons`, and by the quasiquote
  compiler for a `[ ]` form with an explicit tail, `` `[a . ,x] ``.

**Example:** Build the bracket pair `[a . 7]` from a constant and a variable.

| index | opcode       | arg | arg view   | comment |
|------:|--------------|-----|------------|---------|
| 0     | CONST        | 0   | 0 (sym a)  | stack: [..., a] |
| 1     | VAR          | 1   | 1 (x)      | stack: [..., a, 7] |
| 2     | BRACKET_CONS | 2   | 2          | stack: [..., [a . 7]] |
