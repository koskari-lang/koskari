# KOS_OP_ATTR_TAKE

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., value]`. Get attribute, delete it, push value.
  Errors if attribute missing.
- **Invariants**: TOS is the object.
- **Compiler form:** Emitted by **`compile/take-attr`** when OBJ is not a
  symbol.
- **Implementation**: `koskari/lib/opcode/ATTR_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`
