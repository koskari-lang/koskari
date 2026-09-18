# KOS_OP_CALL_ATTR_STAR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG, NOARG.
- **Stack**: `obj.name(*args, **kwargs)`. Stack layout per opa flags (bottom … TOS): opa=0 → `[obj]`; opa=1 → `[obj, pos]` (pos = *args); opa=2 → `[obj, kwd]` (kwd = **kwargs); opa=3 → `[obj, pos, kwd]`. Pop in reverse order (kwd then pos then obj when both star forms are present). `pos` is ensured to be a tuple (coerced via PySequence_Tuple if needed). Resolve obj.name via `kos_resolve_named_attr`; when `send_self=0`, prepend `obj` to the positional tuple before the call. Call via `KosContext_CallArgs`.
- **Invariants**: attr name in constant pool. opa bits: 1 = has *args, 2 = has **kwargs.

