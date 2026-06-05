# KOS_OP_YIELD
- **Style**: COUNT. Arg = number of values to yield (typically 1).
- **Status**: Implemented.
- **Stack**: Pops the yielded value(s). Sets **`KOS_CODE_YIELDS`** on the code object. Opcode sets **`out_ev = KOS_EV_YIELD`** and returns **`KOS_OP_POST_SUSPEND()`**; **`eval_event_yield`** (post-opcode) runs **`eval_yield_handle_walk_and_freeze`** (**`kos_dyn_walk_handle_yield`** via **`kos_dyn_transition`**), then either **`eval_event_yield_deliver_to_boundary`** when **`kos_dyn_need_kos_agen_boundary_deliver`** (flattened **NEXT** / **FOREACH** / **YIELD_FROM**, often **REPROLOGUE** without stopping **`koskari_eval`**) or **`STOP_EVENT`** with **`KOS_SUSP_YIELD`** for the outer driver.
- **Invariants**: Code is treated as async generator when YIELDS flag set.
- **See also**: [compile/yield](../../specials/compile-yield.md); **`koskari/lib/opcode/YIELD.c`**.

**Example:** Yield TOS (arg = 1). Pool: 0 = 100.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (100)  | stack: [..., 100] |
| 1     | YIELD  | 1   | 1        | yield 100; on resume continue |

