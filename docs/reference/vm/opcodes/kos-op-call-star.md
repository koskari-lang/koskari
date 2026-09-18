# KOS_OP_CALL_STAR
- **Style**: STAR. Arg = bitflags (1 = has *args, 2 = has **kwargs).
- **Stack**: `[..., callable, pos, kwd]` with kwd at TOS. pos = *args (tuple or iterable); kwd = **kwargs (dict). Pop per opa: both (opa & 3 == 3): pop 3; **kwargs only (opa & 2): pop 2, pos = (); *args only (opa & 1): pop 2, kwd = NULL; neither: pop 1, pos = (), kwd = NULL. Ensure pos is tuple (PySequence_Tuple if needed). Call via kos_callable_call(ctx, out_ev, callable, pos, kwd).
- **Invariants**: Stack height per opa; pos converted to tuple when *args present.

**Example:** Call with *args and **kwargs. Pool: 0 = fn, 1 = (1,2), 2 = {"x": 3}.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | CONST   | 1   | 1 (pos)  | stack: [..., callable, pos] |
| 2     | CONST   | 2   | 2 (kwd)  | stack: [..., callable, pos, kwd] |
| 3     | CALL_STAR | 3 | 3 (bits) | stack: [..., result] |
| 4     | RETURN  | 1   | 1        | |

