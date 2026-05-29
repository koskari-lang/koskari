# compile/let*

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** **`(let* [[NAME INIT] …] body …)`**

**Behaviour:** Nested **`let`** (empty, single, or recursive **`(let [first] (let* [rest …] body …))`**).

---

Source: [design/MACROS.md](../../design/MACROS.md)
