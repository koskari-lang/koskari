# compile/list-each

**Syntax:** `(list-each [BINDING SEQ] BODY ...)`, with an optional
`when:` or `unless:` clause on the header.

**Behaviour:** Builds a list with one element per element of `SEQ`,
bound as `BINDING`; each element is the value of `BODY`. `SEQ` is
evaluated in the scope the form is in, so a class-body name is in
reach. A `when:` clause keeps only elements whose test is true;
`unless:` keeps those whose test is false. A body that is a single
`when` or `unless` form also filters, contributing nothing when its
test fails. An `await` or a `shift` in the body works.

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
