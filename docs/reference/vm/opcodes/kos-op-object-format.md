# KOS_OP_OBJECT_FORMAT
- **Style**: COUNT.
- **Semantics**: format operation. Arg must be 1 or 2.
  - arg=1: pop object, call `PyObject_Format(obj, NULL)`.
  - arg=2: pop rules then object, call `PyObject_Format(obj, rules)`.
  Stack effect: `[..., obj] -> [..., str]` or `[..., obj, rules] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT.c`

