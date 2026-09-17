# KOS_OP_RETURN_FAST
- **Style**: FASTVAR. Arg = fast slot index.
- **Status**: Implemented.
- **Stack**: `[...] → [...]` (no stack effect). As **`KOS_OP_RETURN_VAR`**, with the value read from the fast slot; an empty slot raises `NameError` as **`KOS_OP_FAST`** would.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of **`KOS_OP_RETURN_VAR`** when the name has a fast slot.
- **See also**: [KOS_OP_RETURN_VAR](kos-op-return-var.md); **`koskari/lib/opcode/RETURN_FAST.c`**.
- **Covered by**: `tests/lib/opcode/test_control_operand.py`, `tests/compile/test_fuse_feeders.py`

**Example:** `(define (f a) a)`: the body returns its parameter.

| index | opcode      | arg | arg view | comment |
|------:|-------------|-----|----------|---------|
| 0     | RETURN_FAST | 0   | 0        | return slot 0 (`a`) |
