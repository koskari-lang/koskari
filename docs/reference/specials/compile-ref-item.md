# compile/ref-item
**Symbol:** `compile/ref-item`

**Syntax:** `(ref-item OBJ KEY)`

**Use-case:** Yield a mutable reference (ItemRef) to the item at KEY in OBJ. Same
constant-key optimization as compile/item.

**Behaviour:** Like compile/item but emits CONST_ITEM_REF or ITEM_REF.

**Examples:**

```koskari
(ref-item lst i)
```

