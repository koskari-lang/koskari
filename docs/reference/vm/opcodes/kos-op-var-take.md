# KOS_OP_VAR_TAKE

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., value]`. Deref name via `frame_ns_take`, clear the
  binding, push the value. **Errors with NameError if the binding is empty**
  (contrast **VAR_CLEAR**, which is idempotent on an already-empty declared
  slot).
- **Invariants**: Name in constant pool; binding must hold a value.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.
  Emitted by **`compile/takeq`**. May lower to **FAST_TAKE** via
  **`convert_fast`**.
- **Implementation**: `koskari/lib/opcode/VAR_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`,
  `tests/compile/specials/test_take_specials.py`
