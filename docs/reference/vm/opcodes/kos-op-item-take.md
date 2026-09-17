# KOS_OP_ITEM_TAKE

- **Style**: NOARG. Key from stack.
- **Status**: Implemented.
- **Stack**: `[..., obj, key] → [..., value]`. Get item, delete it, push value.
  Errors if key missing.
- **Invariants**: Object and key on stack.
- **Compiler form:** Emitted by **`compile/take-item`** when key is not
  constant-only.
- **Implementation**: `koskari/lib/opcode/ITEM_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`
