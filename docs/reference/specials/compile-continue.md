# compile/continue
**Symbol:** `compile/continue`

**Syntax:** `(continue)` or `(continue VAL)`

**Use-case:** Restart the innermost loop iteration. Must be inside a
`compile/while` or `compile/for-each` body.

**Bare form:** the lone symbol `continue` expands to `(continue)`, so the
Python-style spelling restarts the iteration. Governed by the
[`compiler/bare-control-words`](../../guide/pragma.md#compiler) pragma of the
module being compiled (`convert` by default, or `warn` / `error`); because the
bare symbol always expands, `continue` can never name a variable.

**Behaviour:** Compiles optional VAL (or pushes None), emits CONTINUE with
count 0 or 1. Runtime jumps to the loop's continue target (loop head for while,
ITER_NEXT for for-each). The target is the innermost loop of the same
function. The body of a [`reset`](compile-reset.md) and the handler of a
[`shift`](compile-shift.md) are functions of their own, so each has only
the loops written inside it: a `continue` there with no loop of its own
raises `RuntimeError("continue outside loop")`, whatever loop surrounds
the `reset` or the `shift`. The same holds in a resumed delimited
continuation, whose slice ends with the `reset`'s body.

**Examples:**

```koskari
(continue)
(continue last-value)
```
