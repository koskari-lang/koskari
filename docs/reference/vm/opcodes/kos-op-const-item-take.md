# KOS_OP_CONST_ITEM_TAKE

- **Style**: CONST. Arg = constant pool index of key.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., value]`. Get item with constant key, delete
  it, push value. Errors if key missing.
- **Invariants**: Key from pool.
- **Compiler form:** Emitted by **`compile/take-item`** when key is
  constant-only.
- **Implementation**: `koskari/lib/opcode/CONST_ITEM_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`
