# KOS_OP_FAST4
- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, FASTVAR). Row = four fast
  local slot indices.
- **Status**: Implemented.
- **Stack**: `[...] → [..., a, b, c, d]`. Load all four slots and push in
  row order. All slots resolve before any push, so an unbound slot errors
  with nothing pushed.
- **Invariants**: Slot indices in range for frame.
- **Compiler form:** Post-compile only: `convert_fast` lowering of VAR4 when
  all four names have fast slots.

**Example:** Load fast locals 0-3.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST4  | 0   | (fast 0 (a), fast 1 (b), fast 2 (c), fast 3 (d)) | stack: [..., a, b, c, d] |
| 1     | RETURN | 1   | 1        | |
