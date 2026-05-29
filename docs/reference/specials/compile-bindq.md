# compile/bindq

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/bindq`

**Syntax:** `(compile/bindq SYM EXPR)`

**Use-case:** Introduce a new binding SYM in the current scope with the value of
EXPR. The form evaluates to None.

**Behaviour:** Declares the variable name, compiles EXPR, emits VAR_BIND, then
pushes CONST None.

**Examples:**

```scheme
(compile/bindq local 10)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
