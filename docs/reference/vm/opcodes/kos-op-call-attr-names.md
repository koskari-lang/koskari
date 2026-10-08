# KOS_OP_CALL_ATTR_NAMES
- **Style**: EXTRA. Schema: CONST (attr name), CONST (kwarg names tuple), COUNT (npos), NOARG.
- **Stack**: `[..., obj, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. `obj.name` resolves as CALL_ATTR describes; the call then proceeds like CALL_NAMES. The kwarg names tuple and attr name are in the constant pool (EXTRA row), not on the stack.
- **Compiler form:** Emitted by `compile/call-attr` when the call has keyword arguments and no `*:` or `**:`.

