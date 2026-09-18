# KOS_OP_RETURN
- **Style**: COUNT. Arg = number of values to package as the return value: `0` returns `None`, `1` returns the single popped value, and `N > 1` pops `N` values and returns them as a tuple.
- **Stack**: Pops the return value(s) per `arg`. The opcode returns **`EVAL_RETURN(val)`**; **`apply_kos_op_result`** routes to **`eval_event_return`**, which runs **`kos_dyn_unwind_return`**, **`kos_dyn_pop_frame`**, iterator-boundary delivery, native-handler delivery, or stack push onto the caller, then **`kos_dynnode_on_control_returned`**. **Top-level** or **GEN** parent: **`KOS_RES_RETURN`** from **`koskari_eval`**. **Delimited** continuation returns use **`KosEvalState_KReturnDelimited`** in the opcode.
- **Invariants**: See execution-invariants: in-loop flattening for RETURN.
- **See also**: [compile/return](../../specials/compile-return.md); **`koskari/lib/opcode/RETURN.c`**.

**Example:** Return TOS (arg = 1). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | return 42 (`eval_event_return` or in-loop parent push) |
