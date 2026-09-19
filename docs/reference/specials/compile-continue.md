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
ITER_NEXT for for-each).

**Examples:**

```koskari
(continue)
(continue last-value)
```
