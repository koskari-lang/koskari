# compile/del-item
**Symbol:** `compile/del-item`

**Syntax:** `(del-item OBJ KEY)`

**Use-case:** Delete the item at KEY in OBJ. The form evaluates to None.

**Behaviour:** Compiles OBJ, then KEY, then emits ITEM_CLEAR, folded and
shaped as compile/item is (ITEM_CONST_CLEAR, VAR_ITEM_VAR_CLEAR,
VAR_ITEM_CLEAR, and so on). Then CONST None.

**Examples:**

```koskari
(del-item d "key")
```

