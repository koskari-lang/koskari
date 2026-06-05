# compile/attr

**Symbol:** `compile/attr`

**Syntax:** `(attr OBJ SYM)`

**Use-case:** Load the attribute named SYM from OBJ (getattr). The value is pushed on the stack.

**Behaviour:** Compiles OBJ, then emits ATTR with the attribute name (string).

**Examples:**

```koskari
(attr obj "value")
(attr thing count)
```

