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
The target is the innermost loop of the same function. The body of a
[`reset`](compile-reset.md) and the handler of a
[`shift`](compile-shift.md) are functions of their own, so each has only
the loops written inside it: a `break` there with no loop of its own
raises `RuntimeError("break outside loop")`, whatever loop surrounds the
`reset` or the `shift`. The same holds in a resumed delimited
continuation, whose slice ends with the `reset`'s body.

**Examples:**

```koskari
(break)
(break 99)
```
