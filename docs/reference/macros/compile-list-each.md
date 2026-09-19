# compile/list-each

**Syntax:** `(list-each [BINDING SEQ] BODY ...)`, with an optional
`when:` or `unless:` clause on the header.

**Behaviour:** Builds a Python list from the value of `BODY` for each
element of `SEQ` bound as `BINDING`. It expands to a fresh list, a
`for-each` that appends to it, and a `takeq` that yields the list and
clears the accumulator slot. A `when:` clause keeps only elements whose
test is true; `unless:` keeps those whose test is false. A body that is
a single `when` or `unless` form also filters, contributing nothing
when its test fails.

**Examples:**

```koskari
list-each [x (range 6)]:
  (* x x)
;; [0, 1, 4, 9, 16, 25]

list-each [x xs] when: (== 0 (% x 2)):
  x

(list-each [w words] (w.upper))
```

**See also:** [`each`](compile-each.md) for a generator,
[`dict-each`](compile-dict-each.md) for a dict,
[`for-each`](../specials/compile-for-each.md) for a loop with effects.
