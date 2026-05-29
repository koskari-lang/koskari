# KOS_OP_CALL_NAMES
- **Style**: COUNT. Arg = N (number of positional arguments).
- **Status**: Implemented.
- **Stack**: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple] → [..., result]`. TOS = names_tuple (tuple of M keyword names); below it the M keyword values in the same order as names; below that the N positional args; then callable. TailPtr for callable + args + kwnames; validate names tuple; kos_callable_vcall; DropNoErr. No tuple or dict allocation.
- **Invariants**: names_tuple must be a tuple; stack must have 1 + N + M + 1 elements (callable + N positionals + M keyword values + names).

**Example:** Call with 1 positional and 1 keyword (x=42). Pool: 0 = fn, 1 = 10, 2 = 42, 3 = ("x",).

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | CONST     | 1   | 1 (10)   | stack: [..., callable, 10] |
| 2     | CONST     | 2   | 2 (42)   | stack: [..., callable, 10, 42] |
| 3     | CONST     | 3   | 3 (names)| stack: [..., callable, 10, 42, names] |
| 4     | CALL_NAMES| 1   | 1        | stack: [..., result] |
| 5     | RETURN    | 1   | 1        | |

