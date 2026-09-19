# compile/del!

**Syntax:** `(del! NAME)`, `(del! (item OBJ KEY))`, `(del! (attr OBJ NAME))`

**Behaviour:** Expands to [`delq`](../specials/compile-delq.md),
[`del-item`](../specials/compile-del-item.md), or
[`del-attr`](../specials/compile-del-attr.md) by the shape of the
target. The subscript and dot suffixes read to the item and attribute
forms, so `d["k"]` and `obj.attr` are targets. A deleted binding stays
bound to its ref but empty, and reading it raises `NameError`.

**Examples:**

```koskari
define d #{"k": 1 "j": 2}
(del! d["k"])          ;; d is {'j': 2}

define b (Box)
(del! b.w)             ;; the attribute is gone

define gone 1
(del! gone)
gone                   ;; NameError: Empty reference: gone
```

**See also:** [`take!`](compile-take.md), which returns the value it
clears.
