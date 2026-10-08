# KOS_OP_TAIL_CALL_NAMES
- **Style**: COUNT. Arg = N (number of positional arguments).
- **Stack**: Same layout as CALL_NAMES: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple]`. The arguments and the names tuple are taken off the stack before the callee is examined; the call then proceeds as TAIL_CALL describes, with the keywords bound by name.

