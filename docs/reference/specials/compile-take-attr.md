# compile/take-attr
**Symbol:** `compile/take-attr`

**Syntax:** `(take-attr OBJ SYM)`

**Use-case:** Get the attribute named SYM on OBJ, delete it, and return the
value.

**Behaviour:** Compiles OBJ when needed, then emits ATTR_TAKE or
VAR_ATTR_TAKE when OBJ is a symbol. Pushes the removed value.

**See also:** [compile/del-attr](compile-del-attr.md) (delete only, returns
None).

**Examples:**

```koskari
(take-attr obj x)
```
