# KOS_OP_YIELD_CONST
- **Style**: CONST. Arg = pool index of the constant to yield.
- **Status**: Implemented.
- **Stack**: `[...] → [...]` (no stack effect). Yields the constant exactly as **`KOS_OP_YIELD`** **1** yields a popped value: a flattened generator delivers to its consumer and threads into it, any other returns the YIELD signal. Marks the code as a generator, as **`KOS_OP_YIELD`** does.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when **`KOS_OP_YIELD`** **1** is fed by a bare **`KOS_OP_CONST`**. A `None` constant takes **`KOS_OP_YIELD`** **0** in the earlier squish pass instead.
- **See also**: [KOS_OP_YIELD](kos-op-yield.md); [compile/yield](../../specials/compile-yield.md); **`koskari/lib/opcode/YIELD_CONST.c`**.
- **Covered by**: `tests/lib/opcode/test_control_operand.py`, `tests/compile/test_fuse_feeders.py`

**Example:** `(yield 100)` after compress. Pool: 0 = 100.

| index | opcode      | arg | arg view | comment |
|------:|-------------|-----|----------|---------|
| 0     | YIELD_CONST | 0   | 0 (100)  | yield 100; nothing pushed first |
