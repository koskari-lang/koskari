# compile/set-attr

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/set-attr`

**Syntax:** `(compile/set-attr OBJ SYM VAL)`

**Use-case:** Set the attribute named SYM on OBJ to the value of VAL. The form
evaluates to None.

**Behaviour:** Compiles OBJ, compiles VAL, emits ATTR_STORE with the attribute
name, then pushes CONST None.

**Examples:**

```scheme
(compile/set-attr obj "value" 42)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
