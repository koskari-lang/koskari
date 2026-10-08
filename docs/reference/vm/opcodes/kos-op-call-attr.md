# KOS_OP_CALL_ATTR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (npos), NOARG, NOARG.
- **Stack**: `[..., obj, arg_0, ..., arg_{N-1}] → [..., result]`. Attr name from constant pool (EXTRA row). `obj.name` is resolved as an attribute read would resolve it, then invoked. An unbound plain function found on the class is called as `fn(obj, *args)`; a bound method, an instance-dict callable or a descriptor's result is called as `callable(*args)`.
- **Invariants**: Stack has at least 1 + N elements (obj, N args). Resolved attribute must be callable or TypeError.
- **Receiver**: the resolution decides whether `obj` is passed: an unbound plain function from the class gets it as the first argument; anything else is called with the arguments alone. A Koskari callable runs in the evaluator; any other callable is called and its result pushed, awaited when awaitable.

**Example:** `"hello".upper()` (0 args). Pool: 0 = "upper", 1 = "hello". EXTRA row 0: a0 = cpool index of "upper", a1 = 0 (npos).

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 1   | 1 ("hello") | stack: [..., obj] |
| 1     | CALL_ATTR| 0   | 0 (extra idx) | resolve obj.upper; call; push result. stack: [..., "HELLO"] |
| 2     | RETURN   | 1   | 1        | |

**Example:** `c.add_one(41)` (1 arg, unbound method). Pool: 0 = "add_one", 1 = c, 2 = 41. EXTRA row 0: a0 = cpool index of "add_one", a1 = 1 (npos).

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 1   | 1 (c)    | stack: [..., obj] |
| 1     | CONST    | 2   | 2 (41)   | stack: [..., obj, 41] |
| 2     | CALL_ATTR| 0   | 0 (extra idx) | resolve; call add_one(c, 41); push 42. stack: [..., 42] |
| 3     | RETURN   | 1   | 1        | |

