# KOS_OP_VAR_OBJECT_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, CONST). Row a0 = bare KosOperand
  selector (the count is fixed at 1 by the shape), a1 = constant pool
  index of the name, a2 = constant pool index of the operand.
- **Status**: Implemented.
- **Stack**: No stack effect. Augmented assignment with a constant rhs
  through the frame namespace, completely stack-free: obj = deref(name);
  obj OP= const; store back.
- **Compiler form:** Produced by `Compiler.compress` fusing a prior CONST
  into a count-1 VAR_OBJECT_INPLACE -- the `(set! i (+ i 1))` / `incr!`
  idiom. Lowers to FAST_OBJECT_INPLACE_CONST when the name has a fast
  slot. Cold handler.
