# compile/break
**Symbol:** `compile/break`

**Syntax:** `(break)` or `(break VAL)`

**Use-case:** Exit the innermost loop (`compile/while` or `compile/for-each`),
optionally with a result value.

**Bare form:** the lone symbol `break` expands to `(break)`, so the
Python-style spelling exits the loop. Governed by the
[`compiler/bare-control-words`](../../guide/pragma.md#compiler) pragma of the
module being compiled (`convert` by default, or `warn` / `error`); because the
bare symbol always expands, `break` can never name a variable.

**Behaviour:** Compiles optional VAL (or pushes None), emits BREAK with count
0 or 1. Runtime unwinds to the loop's break target and pushes the result.
The target is the innermost loop of the same function, through any
[`reset`](compile-reset.md) between the `break` and the loop. A
[`shift`](compile-shift.md) handler runs after the shift has left the
captured region, so it has only the loops written inside it: a `break`
there with no loop of its own raises `RuntimeError("break outside loop")`,
whatever loop surrounds the `shift` or the `reset`. Inside a resumed
delimited continuation the slice is the continuation's body: a `break`
there reaches only a loop opened inside the slice, and with none it raises
the same error.

**Examples:**

```koskari
(break)
(break 99)
```
