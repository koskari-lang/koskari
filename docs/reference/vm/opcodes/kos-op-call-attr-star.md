# KOS_OP_CALL_ATTR_STAR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG, NOARG.
- **Stack**: `obj.name(*args, **kwargs)`. Stack layout per opa flags (bottom … TOS): opa=0 → `[obj]`; opa=1 → `[obj, pos]` (pos = *args); opa=2 → `[obj, kwd]` (kwd = **kwargs); opa=3 → `[obj, pos, kwd]`. Pop in reverse order (kwd then pos then obj when both star forms are present). `pos` is made a tuple when it is another sequence. `obj.name` resolves as CALL_ATTR describes; `obj` is prepended to the positional tuple when the attribute is an unbound function, and the callee is called with the tuple and the keywords.
- **Invariants**: attr name in constant pool. opa bits: 1 = has *args, 2 = has **kwargs.

