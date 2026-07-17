# KOS_OP_FAST2
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG). Row = two fast local
  slot indices.
- **Status**: Implemented.
- **Stack**: `[...] → [..., a, b]`. Load both slots and push in row order.
  Both slots resolve before either push, so an unbound slot errors with
  nothing pushed.
- **Invariants**: Slot indices in range for frame.
- **Compiler form:** Post-compile only: `convert_fast` lowering of VAR2 when
  both names have fast slots.

**Example:** Load fast locals 0 and 1.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST2  | 0   | (fast 0 (a), fast 1 (b), 0, 0) | stack: [..., a, b] |
| 1     | RETURN | 1   | 1        | |
