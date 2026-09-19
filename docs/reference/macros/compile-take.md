# compile/take!

**Syntax:** `(take! NAME)`, `(take! (item OBJ KEY))`, `(take! (attr OBJ NAME))`

**Behaviour:** Reads the target's value, clears the target, and returns
the value, through [`takeq`](../specials/compile-takeq.md),
[`take-item`](../specials/compile-take-item.md), or
[`take-attr`](../specials/compile-take-attr.md) by the target's shape.
The subscript and dot suffixes read to the item and attribute forms. A
taken binding stays bound to its ref but empty.

**Examples:**

```koskari
define y 5
(take! y)              ;; 5; y is now empty

define d #{"j": 5}
(take! d["j"])         ;; 5; d is now {}

set! box.v 3
(take! box.v)          ;; 3; the attribute is gone
```

**See also:** [`del!`](compile-del.md), which clears without returning.
