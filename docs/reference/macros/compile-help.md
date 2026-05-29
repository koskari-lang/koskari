# compile/help

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** **`(help)`**, **`(help obj)`**, **`(help obj ns)`**

**Behaviour:** **`(compile/call help …)`** — no namespace: **`(help 'help (globals))`**;
object only: **`(help 'obj (globals))`**; object and namespace passed through.

---

Source: [design/MACROS.md](../../design/MACROS.md)
