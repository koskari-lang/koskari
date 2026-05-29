# compile/declare-exports

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** **`(declare-exports name …)`**

**Behaviour:** **`macro-rules`** → **`(define-global __all__ #(…))`** with sorted, deduplicated string names.


## koskari/bootstrap/_class.ki

---

Source: [design/MACROS.md](../../design/MACROS.md)
