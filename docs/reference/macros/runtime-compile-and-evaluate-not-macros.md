# Runtime compile and evaluate (not macros)

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Symbols:** **`compile`**, **`evaluate`** (`define-global`).

**Behaviour:** **`compile`** resolves **`__compiler__`** from **`ns`**, runs
**`full_compile`**, returns an **`Expression`** (or passes through if already
**`Expression`** / **`Function`**). **`evaluate`** compiles when needed, then invokes
**`as_expression`**.

---

Source: [design/MACROS.md](../../design/MACROS.md)
