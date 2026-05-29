# compile/del-item

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/del-item`

**Syntax:** `(compile/del-item OBJ KEY)`

**Use-case:** Delete the item at KEY in OBJ. The form evaluates to None. Same
constant-key optimization as compile/item.

**Behaviour:** Compiles OBJ, then either CONST_ITEM_CLEAR or key fragment +
ITEM_CLEAR, then CONST None.

**Examples:**

```scheme
(compile/del-item d "key")
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
