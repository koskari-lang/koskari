# KOS_OP_FAST3
- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, NOARG). Row = three fast local slot indices.
- **Status**: Implemented.
- **Stack**: `[...] → [..., a, b, c]`. Load the three slots and push in row order. Every
  slot resolves before any push, so an unbound slot errors with nothing
  pushed.
- **Invariants**: Slot indices in range for frame.
- **Compiler form:** Post-compile only: `convert_fast` lowering of VAR3
  when all three names have fast slots, or `ByteCodeProcessor.pack_runs` on a
  run of three bare FAST loads.

**Example:** Load fast locals 0-2.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST3  | 0   | (fast 0 (a), fast 1 (b), fast 2 (c), 0) | stack: [..., a, b, c] |
| 1     | RETURN | 1   | 1        | |
