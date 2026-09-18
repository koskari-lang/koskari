# KOS_OP_CALL_VAR_ATTR_NAMES
- **Style**: EXTRA. Schema: VAR (name/cpool for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same as CALL_FAST_ATTR_NAMES. Obj is loaded from frame namespace by name (`frame_ns_deref`) instead of fast slot.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

