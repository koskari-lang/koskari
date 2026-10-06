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
function, through any [`reset`](compile-reset.md) or `shift` handler
between the `continue` and the loop. Inside a resumed delimited
continuation the slice is the continuation's body: a `continue` there
reaches only a loop opened inside the slice, and with none it raises
`RuntimeError("continue outside loop")`.

**Examples:**

```koskari
(continue)
(continue last-value)
```
