# KOS_OP_VAR_ATTR_TAKE

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get attribute on object from namespace,
  delete it, push value. Errors if attribute missing (same as **VAR_ATTR** +
  **VAR_ATTR_CLEAR** sequence).
- **Compiler form:** Emitted by **`compile/take-attr`** when OBJ is a symbol.
  May lower to **FAST_ATTR_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`
