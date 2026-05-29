# compile/delq

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/delq`

**Syntax:** `(compile/delq SYM)`

**Use-case:** Delete the binding named SYM from the current scope. The form
evaluates to None.

**Behaviour:** Requests the variable name, emits VAR_CLEAR, then pushes CONST
None.

**Examples:**

```scheme
(compile/delq temp)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
