# compile/ref-attr
**Symbol:** `compile/ref-attr`

**Syntax:** `(ref-attr OBJ SYM)`

**Use-case:** Yield a mutable reference (AttributeRef) to the attribute named
SYM on OBJ. The reference can be deref'd or stored into.

**Behaviour:** Compiles OBJ, then emits ATTR_REF with the attribute name.

**Examples:**

```koskari
(ref-attr obj count)
```

