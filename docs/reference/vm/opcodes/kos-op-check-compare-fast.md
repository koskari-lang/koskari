# KOS_OP_CHECK_COMPARE_FAST
- **Style**: EXTRA (CMPSRC, FASTVAR, FASTVAR, LABEL). Row = packed
  selector + source bits, two operand slots, target pc; the negate bit makes the check test the negated comparison. A set source bit
  overrides that operand slot from its declared FASTVAR kind to CONST
  (pool index).
- **Stack**: No stack effect. Arity-2 fused comparison branch: load both
  operands per the source bits (fast local slot, or constant pool),
  compare under the selector, jump to LABEL when the comparison, negated under a set negate bit, fails.
- **Invariants**: Selector is a valid KosCompare ordinal; both-CONST rows
  are never emitted (that is a folding case).
- **Compiler form:** Post-compile only: the CMPSRC-aware `convert_fast`
  lowering of `CHECK_COMPARE_VAR` when every name-sourced slot has a
  fast slot. Constant-bit slots pass through untouched. This is the hot
  loop-condition form: a `while (< i n)` head is one dispatch.

**Example:** Loop condition `i < n` on fast slots 1 and 0.

| index | opcode             | arg | arg view                        | comment |
|------:|--------------------|-----|---------------------------------|---------|
| 3     | CHECK_COMPARE_FAST | 2   | (LT, fast 1 ("i"), fast 0 ("n"), -> 9) | jump to 9 unless i < n |
