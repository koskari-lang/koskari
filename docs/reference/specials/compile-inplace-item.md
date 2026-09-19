# compile/inplace-item
**Symbol:** `compile/inplace-item`

**Syntax:** `(inplace-item OP OBJ KEY VALUE...)`

**Use-case:** In-place update of an item slot: load the current value at KEY in
OBJ, compute `current OP VALUE`, and store the result back. Equivalent to
augmented assignment on a subscript (`d[k] += 1`).

Normally written as **`(inplace OP (item OBJ KEY) VALUE)`** -- the
**`compile/inplace`** macro extension expands item forms to this special.

**Behaviour:** One fused row reads the current item, applies the
operator (the `PyNumber_InPlace*` API, so mutable values such as lists and
sets are updated in place) to it and the VALUEs, and stores the result
back; multiple values (except for `**`, which takes exactly one) reduce
left-to-right through that row. When KEY and every VALUE are
feeders (a constant or a symbol) the special emits OBJ, KEY, the
VALUEs, and ITEM_INPLACE, and the compress pipeline folds a symbol OBJ
and the KEY into the row that names them, re-emitting the values ahead
of it; a single constant VALUE folds into the `_CONST` twin, so
`(inplace + d["k"] 1)` is one VAR_ITEM_CONST_INPLACE_CONST row with no
stack effect. Otherwise the special names what a fold could not reach:
a symbol OBJ under a computed KEY emits VAR_ITEM_INPLACE with the key
and values on the stack, and a computed VALUE behind a symbol OBJ or a
feeder KEY emits VAR_ITEM_VAR_INPLACE, VAR_ITEM_CONST_INPLACE,
ITEM_VAR_INPLACE, or ITEM_CONST_INPLACE with the values on the stack. A
named object or key is fetched when the row runs, after the VALUEs. The
form evaluates to None.

**Supported operators:** Same set as [`compile/inplace`](compile-inplace.md).

**Examples:**

```koskari
(inplace + (item vec 0) 1)
(inplace - (item counts key) step)

;; Direct use
(inplace-item + table key delta)
```

**Implementation:** `koskari/compile/specials/object.py` -- `special_inplace_item`
