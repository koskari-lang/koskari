# compile/if

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** `(if COND THEN ELSE)`, `(if COND then: THEN else: ELSE)`, or `(if COND THEN)` (else `None`).

**Behaviour:** Expands to **`compile/cond`** with **`else:`** clause.

---

Source: [design/MACROS.md](../../design/MACROS.md)
