# compile/inplace-item
**Symbol:** `compile/inplace-item`

**Syntax:** `(inplace-item OP OBJ KEY VALUE...)`

**Use-case:** In-place update of an item slot: load the current value at KEY in
OBJ, compute `current OP VALUE`, and store the result back. Equivalent to
augmented assignment on a subscript (`d[k] += 1`).

Normally written as **`(inplace OP (item OBJ KEY) VALUE)`** — the
**`compile/inplace`** macro extension expands item forms to this special.

**Behaviour:** Evaluates OBJ once. Loads the current item (CONST_ITEM when KEY is
constant, otherwise ITEM). Uses DUP so OBJ and KEY are not re-evaluated for the
store. Applies the corresponding OBJECT_INPLACE_* operator (the
`PyNumber_InPlace*` API, so mutable values such as lists and sets are updated
in place) to the loaded value and the VALUEs — multiple values (except for `**`, which takes exactly one) reduce
left-to-right through the single instruction — then stores via
CONST_ITEM_STORE or ITEM_STORE. The form evaluates to None.

**Supported operators:** Same set as [`compile/inplace`](compile-inplace.md).

**Examples:**

```koskari
(inplace + (item vec 0) 1)
(inplace - (item counts key) step)

;; Direct use
(inplace-item + table key delta)
```

**Implementation:** `koskari/compile/specials/object.py` — `special_inplace_item`
