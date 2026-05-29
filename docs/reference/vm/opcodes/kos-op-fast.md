# KOS_OP_FAST
- **Style**: FASTVAR. Arg = fast local slot index.
- **Status**: Implemented.
- **Stack**: `[...] → [..., value]`. Load from slot.
- **Invariants**: Slot index in range for frame.
- **Compiler form:** Usually post-compile only: emitted when VAR is lowered to a
  fast local (arg = slot index). Compiler may emit FAST directly with slot index
  in both forms.

**Example:** Load fast local 0. Arg view: 0 (local name if known).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST   | 0   | 0 (a)    | stack: [..., value] |
| 1     | RETURN | 1   | 1        | |

