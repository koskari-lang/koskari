# KOS_OP_CALL_VAR_NAMES
- **Style**: EXTRA. Schema: VAR (name cpool idx), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. Callable from namespace by name; kw names tuple from constant pool. Same call semantics as CALL_NAMES.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

