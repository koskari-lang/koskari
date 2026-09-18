# compile/inplace-item
**Symbol:** `compile/inplace-item`

**Syntax:** `(inplace-item OP OBJ KEY VALUE...)`

**Use-case:** In-place update of an item slot: load the current value at KEY in
OBJ, compute `current OP VALUE`, and store the result back. Equivalent to
augmented assignment on a subscript (`d[k] += 1`).

Normally written as **`(inplace OP (item OBJ KEY) VALUE)`** — the
**`compile/inplace`** macro extension expands item forms to this special.

**Behaviour:** One fused row reads the current item, applies the
operator (the `PyNumber_InPlace*` API, so mutable values such as lists and
sets are updated in place) to it and the VALUEs, and stores the result
back; multiple values (except for `**`, which takes exactly one) reduce
left-to-right through that row. When OBJ is a symbol and KEY is a
constant or a symbol the row is VAR_CONST_ITEM_INPLACE or
VAR_ITEM_INPLACE, resolving OBJ (and KEY) by name after the VALUEs have
run; a single constant VALUE folds into the row as the `_CONST` twin,
leaving the stack untouched. Otherwise OBJ (and a computed KEY) ride the
stack under the VALUEs and the row is CONST_ITEM_INPLACE or ITEM_INPLACE.
The form evaluates to None.

**Supported operators:** Same set as [`compile/inplace`](compile-inplace.md).

**Examples:**

```koskari
(inplace + (item vec 0) 1)
(inplace - (item counts key) step)

;; Direct use
(inplace-item + table key delta)
```

**Implementation:** `koskari/compile/specials/object.py` — `special_inplace_item`
