# compile/each

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Syntax:** **`(each [BINDING SEQ] BODY)`**; with **`when:`** or **`unless:`** filter.

**Behaviour:** **`lambda each`** wrapping **`for-each`** + **`yield`** (filtered variants use
**`when`** / **`unless`**).

---

Source: [design/MACROS.md](../../design/MACROS.md)
