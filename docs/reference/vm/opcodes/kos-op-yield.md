# KOS_OP_YIELD
- **Style**: COUNT. Arg = number of values to package as the yielded value: `0` yields `None`, `1` yields the single popped value, and `N > 1` pops `N` values and yields them as a tuple.
- **Status**: Implemented.
- **Stack**: Pops the yielded value(s). Sets **`KOS_CODE_YIELDS`** on the code object. The handler finds the running frame's generator node (**`kos_dyn_find_gen`**). A flattened generator (its node has a parent on the chain) is handled in the handler: **`eval_event_yield`** freezes the generator's chain span into the node (**`kos_dyn_gen_freeze`**), delivers the value to the consuming **NEXT** / **FOREACH** / **YIELD_FROM** node, and threads into the consumer without stopping **`koskari_eval`**. A generator driven from outside returns **`EVAL_YIELD(val)`**; the trampoline runs the same freeze and stops with **`KOS_RES_YIELD`** and **`KOS_SUSP_YIELD`** for the outer driver.
- **Invariants**: Code is treated as async generator when YIELDS flag set.
- **See also**: [compile/yield](../../specials/compile-yield.md); **`koskari/lib/opcode/YIELD.c`**.

**Example:** Yield TOS (arg = 1). Pool: 0 = 100.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (100)  | stack: [..., 100] |
| 1     | YIELD  | 1   | 1        | yield 100; on resume continue |
