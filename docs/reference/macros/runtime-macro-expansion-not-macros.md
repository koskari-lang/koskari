# Runtime macro expansion (not macros)

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

**Symbols:** **`macroexpand-1`**, **`macroexpand`** (`define-global` in the same file).

**Behaviour:** Look up **`__compiler__`** on the namespace, **`find_compiled`** on the form
head, match a **`Syntax`** transformer, and run **`handle_expand`** once
(**`macroexpand-1`**) or in a loop until no macro applies (**`macroexpand`**). If the
result is a single-expression **`begin`** / **`compile/begin`**, that wrapper is stripped.

---

Source: [design/MACROS.md](../../design/MACROS.md)
