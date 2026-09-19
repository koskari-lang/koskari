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

**Examples:**

```koskari
(break)
(break 99)
```

