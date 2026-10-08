# KOS_OP_RETURN
- **Style**: COUNT. Arg = number of values to package as the return value: `0` returns `None`, `1` returns the single popped value, and `N > 1` pops `N` values and returns them as a tuple.
- **Stack**: Pops the return value(s) per `arg`. The returning frame's dynamic extent unwinds, finally blocks and wind thunks included, and the frame is popped. The value goes where the frame's parent expects it: pushed onto a calling Koskari frame, delivered to the iterator or native handler driving the frame, or handed back to the host when the frame is the outermost one or a generator's. A return across a delimited continuation boundary delivers to that boundary.
- **Invariants**: See execution-invariants: in-loop flattening for RETURN.
- **See also**: [compile/return](../../specials/compile-return.md).

**Example:** Return TOS (arg = 1). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | return 42 to the caller |
