# KOS_OP_FAST_TAKE

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[...] → [..., value]`. Take value from slot via
  `KosNamespace_FastTake` (steal pointer on raw slots without extra INCREF),
  clear slot. **Errors with NameError if slot is empty** (contrast
  **FAST_CLEAR**, idempotent on empty declared slots).
- **Invariants**: Slot index in range; slot must hold a value.
- **Compiler form:** Lowered from **VAR_TAKE** when name has a fixed slot.
- **Implementation**: `koskari/lib/opcode/FAST_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`
