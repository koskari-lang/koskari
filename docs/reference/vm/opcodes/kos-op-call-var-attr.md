# KOS_OP_CALL_VAR_ATTR
- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), CONST (attr name), COUNT (npos), NOARG.
- **Stack**: Same as `KOS_OP_CALL_FAST_ATTR`. `obj` is read from the frame namespace by name.
- **Behaviour**: otherwise as `KOS_OP_CALL_FAST_ATTR` and `KOS_OP_CALL_ATTR`: the same resolution, the same receiver rule, a Koskari callable run in the evaluator and any other called with its result pushed.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

