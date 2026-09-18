# KOS_OP_YIELD_VAR
- **Style**: VAR. Arg = pool index of the name.
- **Status**: Implemented.
- **Stack**: `[...] → [...]` (no stack effect). Resolves the name in the current frame's namespace, raising `NameError` when it is unbound as **`KOS_OP_VAR`** would, and yields the value exactly as **`KOS_OP_YIELD`** **1** yields a popped one. Marks the code as a generator.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when **`KOS_OP_YIELD`** **1** is fed by a bare **`KOS_OP_VAR`**. Lowers to **`KOS_OP_YIELD_FAST`** through the generic schema-driven `convert_fast` path when the name has a fast slot.
- **See also**: [KOS_OP_YIELD](kos-op-yield.md); [KOS_OP_YIELD_FAST](kos-op-yield-fast.md); **`koskari/lib/opcode/YIELD_VAR.c`**.
- **Covered by**: `tests/lib/opcode/test_control_operand.py`, `tests/compile/test_fuse_feeders.py`

**Example:** `(yield x)` at module level, where `x` has no fast slot. Pool: 0 = `x`.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | YIELD_VAR | 0   | 0 (x)    | yield the value of `x` |
