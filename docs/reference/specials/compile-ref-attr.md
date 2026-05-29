# compile/ref-attr

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/ref-attr`

**Syntax:** `(compile/ref-attr OBJ SYM)`

**Use-case:** Yield a mutable reference (AttributeRef) to the attribute named
SYM on OBJ. The reference can be deref'd or stored into.

**Behaviour:** Compiles OBJ, then emits ATTR_REF with the attribute name.

**Examples:**

```scheme
(compile/ref-attr obj count)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
