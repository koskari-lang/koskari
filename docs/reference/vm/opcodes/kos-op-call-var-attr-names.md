# KOS_OP_CALL_VAR_ATTR_NAMES
- **Style**: EXTRA. Schema: VAR (name/cpool for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same as CALL_FAST_ATTR_NAMES. `obj` is read from the frame namespace by name instead of a fast slot.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

