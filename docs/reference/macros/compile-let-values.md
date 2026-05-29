# compile/let-values

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** **`(let-values [PATTERN VALUE] body …)`**

**Behaviour:** **`(let [[resolved VALUE]] (compile/define-values PATTERN resolved) body …)`**.

---

Source: [design/MACROS.md](../../design/MACROS.md)
