# KOS_OP_YIELD
- **Style**: COUNT. Arg = number of values to package as the yielded value: `0` yields `None`, `1` yields the single popped value, and `N > 1` pops `N` values and yields them as a tuple.
- **Stack**: Pops the yielded value(s). Sets **`KOS_CODE_YIELDS`** on the code object. Before the consumer has the value, the generator's open extents come out innermost first: a dyn-wind or `dyn-with` extent is left, its after segment or exit run, and a `parameterize` binding or the exception a `try` handler is handling is taken out. A binding written inside a dyn-wind is out before that dyn-wind's after segment runs, and one written around it is still in effect for the segment. A `try`, a `finally` and a plain `with` run nothing. The step that resumes the generator enters each dyn-wind and `dyn-with` extent again and puts each binding and handled exception back, outermost first, before the body continues. A flattened generator, one consumed inside the same evaluator, then hands the value to the consuming frame (a for-each step, a next request, or a yield-from waiting on it) and continues in that frame without stopping the evaluator. A generator run from outside stops the evaluator with the yielded value.
- **Invariants**: Code is treated as async generator when YIELDS flag set.
- **See also**: [compile/yield](../../specials/compile-yield.md); **`koskari/lib/opcode/YIELD.c`**.

**Example:** Yield TOS (arg = 1). Pool: 0 = 100.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (100)  | stack: [..., 100] |
| 1     | YIELD  | 1   | 1        | yield 100; on resume continue |
