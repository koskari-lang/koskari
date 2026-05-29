# compile/refq

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/refq`

**Syntax:** `(compile/refq SYM)`

**Use-case:** Yield a mutable reference (Ref) to the variable named SYM. The
reference can be passed and later deref'd or stored into.

**Behaviour:** Requests the variable name and emits VAR_REF. The value on the
stack is a Ref for that name.

**Examples:**

```scheme
(compile/refq my-var)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
