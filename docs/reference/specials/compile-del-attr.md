# compile/del-attr
**Symbol:** `compile/del-attr`

**Syntax:** `(del-attr OBJ SYM)`

**Use-case:** Delete the attribute named SYM on OBJ. The form evaluates to None.

**Behaviour:** Compiles OBJ, emits ATTR_CLEAR, then pushes CONST None.

**Examples:**

```koskari
(del-attr obj x)
```

