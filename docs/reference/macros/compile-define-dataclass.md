# compile/define-dataclass

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** **`(define-dataclass NAME (members …))`** or with **`SUPERS`**.

**Behaviour:** Imports **`dataclass`** / **`field`**, emits **`@dataclass class`**, then
**`create-instance-check`** for **`NAME?`**.

---

Source: [design/MACROS.md](../../design/MACROS.md)
