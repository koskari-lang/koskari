# KOS_OP_OBJECT_FORMAT_CONST
- **Style**: CONST.
- **Status**: Implemented.
- **Semantics**: format operation with constant rules from cpool.
  Pop object, fetch format rules string from constant pool arg, call
  `PyObject_Format(obj, rules_const)`, push resulting string.
  Stack effect: `[..., obj] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT_CONST.c`

