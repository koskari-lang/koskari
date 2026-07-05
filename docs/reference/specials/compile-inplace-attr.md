# compile/inplace-attr
**Symbol:** `compile/inplace-attr`

**Syntax:** `(inplace-attr OP OBJ ATTR VALUE...)`

**Use-case:** In-place update of an attribute: load the current value of ATTR on
OBJ, compute `current OP VALUE`, and store the result back. Equivalent to
augmented assignment on an attribute (`obj.x += 1`). ATTR must be a symbol.

Normally written as **`(inplace OP (attr OBJ ATTR) VALUE)`** — the
**`compile/inplace`** macro extension expands attribute forms to this special.

**Behaviour:** When OBJ is a symbol, loads via VAR_ATTR and stores via
VAR_ATTR_STORE. Otherwise evaluates OBJ once, uses DUP for the load/store
sequence, loads via ATTR, applies the corresponding OBJECT_INPLACE_* operator
(the `PyNumber_InPlace*` API, so mutable values such as lists and sets are
updated in place; multiple values (except for `**`, which takes exactly one) reduce left-to-right through the single
instruction), and stores via ATTR_STORE. The form evaluates to None.

**Supported operators:** Same set as [`compile/inplace`](compile-inplace.md).

**Examples:**

```koskari
(inplace + (attr obj count) 1)
(inplace // (attr totals divisor) step)

;; Direct use
(inplace-attr + obj total amount)
```

**Implementation:** `koskari/compile/specials/object.py` — `special_inplace_attr`
