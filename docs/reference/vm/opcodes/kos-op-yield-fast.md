# KOS_OP_YIELD_FAST
- **Style**: FASTVAR. Arg = fast slot index.
- **Status**: Implemented.
- **Stack**: `[...] → [...]` (no stack effect). As **`KOS_OP_YIELD_VAR`**, with the value read from the fast slot; an empty slot raises `NameError` as **`KOS_OP_FAST`** would. Marks the code as a generator.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of **`KOS_OP_YIELD_VAR`** when the name has a fast slot.
- **See also**: [KOS_OP_YIELD_VAR](kos-op-yield-var.md); **`koskari/lib/opcode/YIELD_FAST.c`**.
- **Covered by**: `tests/lib/opcode/test_control_operand.py`, `tests/compile/test_fuse_feeders.py`

**Example:** `(define (g a) (yield a))`: the body yields its parameter.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | YIELD_FAST | 0   | 0        | yield slot 0 (`a`) |
| 1     | RETURN     | 0   | 0        | |
