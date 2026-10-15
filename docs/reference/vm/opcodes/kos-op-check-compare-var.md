# KOS_OP_CHECK_COMPARE_VAR
- **Style**: EXTRA (CMPSRC, VAR, VAR, LABEL). Row = packed selector +
  source bits, two operand slots, target pc; the jump-held bit turns the exits around. A set source bit overrides
  that operand slot from its declared VAR kind to CONST (pool index).
- **Stack**: No stack effect. Arity-2 fused comparison branch: load both
  operands per the source bits (frame namespace name lookup, or constant
  pool), compare under the selector, jump to LABEL on failure, or on success when the jump-held bit is set.
- **Invariants**: Selector is a valid KosCompare ordinal; both-CONST rows
  are never emitted (that is a folding case).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` (late
  `compress` sweep) absorbing two bare VAR/CONST feeder rows into an
  arity-2 `CHECK_COMPARE`. Runs before `pack_var_runs` so this fusion
  gets first claim on the feeders. Lowers to `CHECK_COMPARE_FAST` via
  the CMPSRC-aware `convert_fast` case (all-or-nothing on the name
  slots). Cold handler: survives lowering only in one-shot
  expression-level code.

**Example:** Loop condition `i < n` by name.

| index | opcode            | arg | arg view                 | comment |
|------:|-------------------|-----|--------------------------|---------|
| 3     | CHECK_COMPARE_VAR | 2   | (LT, v#1 ('i'), v#0 ('n'), -> 9) | jump to 9 unless i < n |
