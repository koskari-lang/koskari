# compile/when-main

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/when-main` (surface: `when-main`)

**Syntax:** `(when-main BODY ...)`

**Behaviour:** Expands to **`when (equal? __name__ "__main__"): (begin BODY ...)`**.

---

Source: [design/MACROS.md](../../design/MACROS.md)
