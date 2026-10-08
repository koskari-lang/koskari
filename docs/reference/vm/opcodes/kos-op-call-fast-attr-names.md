# KOS_OP_CALL_FAST_ATTR_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot index for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. `obj` is read from fast slot `a0` and is not on the stack, one fewer element than CALL_ATTR_NAMES. Resolution and the call proceed as CALL_ATTR_NAMES describes: an unbound function found on the class receives `obj` first, any other callable the arguments alone.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

