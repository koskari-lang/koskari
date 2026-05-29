# compile/del-attr

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/del-attr`

**Syntax:** `(compile/del-attr OBJ SYM)`

**Use-case:** Delete the attribute named SYM on OBJ. The form evaluates to None.

**Behaviour:** Compiles OBJ, emits ATTR_CLEAR, then pushes CONST None.

**Examples:**

```scheme
(compile/del-attr obj x)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
