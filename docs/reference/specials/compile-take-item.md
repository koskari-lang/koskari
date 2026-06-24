# compile/take-item
**Symbol:** `compile/take-item`

**Syntax:** `(take-item OBJ KEY)`

**Use-case:** Get the item at KEY in OBJ, delete it, and return the value.
Same constant-key optimization as `item`.

**Behaviour:** Compiles OBJ. If the key expression is constant-only, emits
CONST_ITEM_TAKE; otherwise compiles KEY and emits ITEM_TAKE. Errors if the key
is missing (like `item` followed by `del-item`).

**See also:** [compile/del-item](compile-del-item.md) (delete only, returns
None).

**Examples:**

```koskari
(take-item d "key")
(take-item tbl (compute-key))
```
