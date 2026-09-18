# compile/take-item
**Symbol:** `compile/take-item`

**Syntax:** `(take-item OBJ KEY)`

**Use-case:** Get the item at KEY in OBJ, delete it, and return the value.

**Behaviour:** Compiles OBJ, then KEY, then emits ITEM_TAKE, folded and
shaped as compile/item is (ITEM_CONST_TAKE, VAR_ITEM_VAR_TAKE,
VAR_ITEM_TAKE, and so on). Errors if the key is missing (like `item`
followed by `del-item`).

**See also:** [compile/del-item](compile-del-item.md) (delete only, returns
None).

**Examples:**

```koskari
(take-item d "key")
(take-item tbl (compute-key))
```
