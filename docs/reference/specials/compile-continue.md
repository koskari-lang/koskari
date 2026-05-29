# compile/continue

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/continue`

**Syntax:** `(compile/continue)` or `(compile/continue VAL)`

**Use-case:** Restart the innermost loop iteration. Must be inside a
`compile/while` or `compile/for-each` body.

**Behaviour:** Compiles optional VAL (or pushes None), emits CONTINUE with
count 0 or 1. Runtime jumps to the loop's continue target (loop head for while,
ITER_NEXT for for-each).

**Examples:**

```scheme
(compile/continue)
(compile/continue last-value)
```

**Tests:** `tests/compile/specials/test_cond.py`, `tests/compile/specials/test_while.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
