# compile/cadr, caddr, cadddr, caddddr

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** **`(cadr pair)`**, etc.

**Behaviour:** Expand to nested **`car`** / **`cdr`** (not **`compile/`**-prefixed heads in
the **`syntax-rules`** patterns).

---

Source: [design/MACROS.md](../../design/MACROS.md)
