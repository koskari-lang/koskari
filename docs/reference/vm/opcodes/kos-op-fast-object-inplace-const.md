# KOS_OP_FAST_OBJECT_INPLACE_CONST
- **Style**: EXTRA (OPERAND, FASTVAR, CONST). Row a0 = bare KosOperand
  selector (the count is fixed at 1 by the shape), a1 = fast slot index,
  a2 = constant pool index of the operand.
- **Status**: Implemented.
- **Stack**: No stack effect. Augmented assignment with a constant rhs on
  a fast slot, completely stack-free: obj = deref(slot); obj OP= const;
  store back. A counted loop's back-edge increment is this single
  dispatch.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  VAR_OBJECT_INPLACE_CONST.

**Example:** `set! i (+ i 1)` on fast slot 1.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 6 | FAST_OBJECT_INPLACE_CONST | 4 | (ADD, fast 1 ("i"), c#1 (1), 0) | i += 1, stack-free |
