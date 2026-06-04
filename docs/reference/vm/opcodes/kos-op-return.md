# KOS_OP_RETURN
- **Style**: COUNT. Arg = number of values to leave on stack as return value (typically 1).
- **Status**: Implemented.
- **Stack**: Pops the return value(s) per `arg`. Does **not** push onto the caller inside the opcode handler: after **`kos_dyn_transition`** (**`KOS_TRANS_WALK_RETURN_TO_FRAME`**) and **`kos_dyn_pop_frame`**, a **non-GEN** parent path returns **`KOS_OP_POST_PARENT_RETURN()`** (**`KosOpOutcome` / `KOS_LOP_POST`**), and **`eval_parent_return_deliver`** (post-opcode) performs iterator-boundary delivery, native-handler delivery, or **`KosStackBlock_PushRef`** onto the caller, then **`kos_dynnode_on_control_returned`**. **Top-level** or **GEN** parent: **`KOS_EV_RETURN`** in **`out_ev`**, **`KOS_OP_POST_SUSPEND()`** — driver-visible completion boundary. **Delimited** continuation returns (`RETURN_TARGET_CHAIN`) are a separate early path.
- **Invariants**: See execution-invariants: in-loop flattening for RETURN.
- **See also**: [compile/return](../../specials/compile-return.md); **`koskari/lib/opcode/RETURN.c`**.

**Example:** Return TOS (arg = 1). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | return 42 (post-opcode deliver to caller or boundary `KOS_EV_RETURN`) |

