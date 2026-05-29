# compile/load-exception

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/load-exception`

**Syntax:** `(compile/load-exception)`

**Use-case:** Internal form used by try handlers. Pushes the current exception
value onto the stack (e.g. for binding with as: NAME).

**Behaviour:** Emits EXCEPTION. No arguments.

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
