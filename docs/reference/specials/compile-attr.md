# compile/attr

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/attr`

**Syntax:** `(compile/attr OBJ SYM)`

**Use-case:** Load the attribute named SYM from OBJ (getattr). The value is
pushed on the stack.

**Behaviour:** Compiles OBJ, then emits ATTR with the attribute name (string).

**Examples:**

```scheme
(compile/attr obj "value")
(compile/attr thing count)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
