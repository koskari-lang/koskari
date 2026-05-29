# compile/define-class

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** **`(define-class NAME SUPERS BODY …)`**

**Behaviour:** **`(define NAME (class NAME SUPERS BODY …))`** plus **`create-instance-check`**
(defines **`NAME?`** as **`lambda [value] (compile/instance? value NAME)`**).

---

Source: [design/MACROS.md](../../design/MACROS.md)
