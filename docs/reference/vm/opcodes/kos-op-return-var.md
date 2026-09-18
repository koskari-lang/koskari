# KOS_OP_RETURN_VAR
- **Style**: VAR. Arg = pool index of the name.
- **Status**: Implemented.
- **Stack**: `[...] → [...]` (no stack effect). Resolves the name in the current frame's namespace, raising `NameError` when it is unbound as **`KOS_OP_VAR`** would, and returns the value exactly as **`KOS_OP_RETURN`** **1** returns a popped one.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when **`KOS_OP_RETURN`** **1** is fed by a bare **`KOS_OP_VAR`**. Lowers to **`KOS_OP_RETURN_FAST`** through the generic schema-driven `convert_fast` path when the name has a fast slot.
- **See also**: [KOS_OP_RETURN](kos-op-return.md); [KOS_OP_RETURN_FAST](kos-op-return-fast.md); **`koskari/lib/opcode/RETURN_VAR.c`**.
- **Covered by**: `tests/lib/opcode/test_control_operand.py`, `tests/compile/test_fuse_feeders.py`

**Example:** `(return n)` at module level, where `n` has no fast slot. Pool: 0 = `n`.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | RETURN_VAR | 0   | 0 (n)    | return the value of `n` |
