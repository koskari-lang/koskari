# compile/set-attr
**Symbol:** `compile/set-attr`

**Syntax:** `(set-attr OBJ SYM VAL)`

**Use-case:** Set the attribute named SYM on OBJ to the value of VAL. The form
evaluates to None.

**Behaviour:** Compiles OBJ, compiles VAL, emits ATTR_STORE with the attribute
name, then pushes CONST None.

**Examples:**

```koskari
(set-attr obj "value" 42)
```
