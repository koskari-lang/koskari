# compile/ref-item

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/ref-item`

**Syntax:** `(compile/ref-item OBJ KEY)`

**Use-case:** Yield a mutable reference (ItemRef) to the item at KEY in OBJ. Same
constant-key optimization as compile/item.

**Behaviour:** Like compile/item but emits CONST_ITEM_REF or ITEM_REF.

**Examples:**

```scheme
(compile/ref-item lst i)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
