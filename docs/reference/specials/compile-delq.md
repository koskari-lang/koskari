# compile/delq
**Symbol:** `compile/delq`

**Syntax:** `(delq SYM)`

**Use-case:** Delete the binding named SYM from the current scope. The form
evaluates to None.

**Behaviour:** Requests the variable name, emits VAR_CLEAR, then pushes CONST
None.

**Examples:**

```koskari
(delq temp)
```

