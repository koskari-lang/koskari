# compile/extend-syntax

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/extend-syntax`

**Syntax:** `(extend-syntax NAME TRANSFORMER)` with the same six transformer heads as
`define-syntax`.

**Behaviour:** Replaces **`NAME`** via **`setq`** with a new **`syntax-rules`** /
**`syntax-match`** / **`macro-rules`** / **`special-rules`** / **`special-match`**
spec (each expansion passes **`NAME`** as the first argument to the builder form).

**Layout:** `define layout/extend-syntax layout/flat`.

---

Source: [design/MACROS.md](../../design/MACROS.md)
