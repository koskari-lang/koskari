# compile/set-item

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/set-item`

**Syntax:** `(compile/set-item OBJ KEY VAL)`

**Use-case:** Set the item at KEY in OBJ to VAL. The form evaluates to None.
Same constant-key optimization as compile/item.

**Behaviour:** Compiles OBJ; if key is constant, compiles VAL and emits
CONST_ITEM_STORE; otherwise merges key fragment, compiles VAL, emits
ITEM_STORE 0. Then CONST None.

**Examples:**

```scheme
(compile/set-item vec 0 99)
```

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
