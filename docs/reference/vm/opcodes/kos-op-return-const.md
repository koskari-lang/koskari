# KOS_OP_RETURN_CONST
- **Style**: CONST. Arg = pool index of the constant to return.
- **Status**: Implemented.
- **Stack**: `[...] → [...]` (no stack effect). Returns the constant exactly as **`KOS_OP_RETURN`** **1** returns a popped value: the same plain, region and resume deliveries, else the RETURN signal to the trampoline.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when **`KOS_OP_RETURN`** **1** is fed by a bare **`KOS_OP_CONST`**. A `None` constant takes **`KOS_OP_RETURN`** **0** in the earlier squish pass instead.
- **See also**: [KOS_OP_RETURN](kos-op-return.md); [compile/return](../../specials/compile-return.md); **`koskari/lib/opcode/RETURN_CONST.c`**.
- **Covered by**: `tests/lib/opcode/test_control_operand.py`, `tests/compile/test_fuse_feeders.py`

**Example:** `(return 42)` after compress. Pool: 0 = 42.

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | RETURN_CONST | 0   | 0 (42)   | return 42; nothing pushed first |
