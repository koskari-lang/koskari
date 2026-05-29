# compile/break

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/break`

**Syntax:** `(compile/break)` or `(compile/break VAL)`

**Use-case:** Exit the innermost loop (`compile/while` or `compile/for-each`),
optionally with a result value.

**Behaviour:** Compiles optional VAL (or pushes None), emits BREAK with count
0 or 1. Runtime unwinds to the loop's break target and pushes the result.

**Examples:**

```scheme
(compile/break)
(compile/break 99)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
