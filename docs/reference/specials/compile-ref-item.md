# compile/ref-item
**Symbol:** `compile/ref-item`

**Syntax:** `(ref-item OBJ KEY)`

**Use-case:** Yield a mutable reference (ItemRef) to the item at KEY in OBJ.

**Behaviour:** Like compile/item but emits ITEM_REF, folded and shaped
the same way (ITEM_CONST_REF, VAR_ITEM_VAR_REF, VAR_ITEM_REF, and so on).

**Examples:**

```koskari
(ref-item lst i)
```

