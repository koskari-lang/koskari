# compile/each

**Syntax:** `(each [BINDING SEQ] BODY ...)`, with an optional `when:` or
`unless:` clause on the header.

**Behaviour:** A generator that yields the value of `BODY` for each
element of `SEQ` bound as `BINDING`, built from a `for-each` and a
`yield` inside a function. The filter clause skips elements whose test
fails. The binding destructures like a `for-each` binding, so
`[[x y] pairs]` takes two names per element. The generator is lazy and
iterates from both sync and async consumers.

**Examples:**

```koskari
define squares:
  each [x (range 6)]:
    (* x x)

(list squares)                  ;; [0, 1, 4, 9, 16, 25]
```

```koskari
(list (each [x (range 10)] when: (== 0 (% x 2)) x))
;; [0, 2, 4, 6, 8]

each [[x y] (product (range 3) (range 3))]:
  #(x y)
```

**See also:** [`list-each`](compile-list-each.md),
[`dict-each`](compile-dict-each.md),
[`for-each`](../specials/compile-for-each.md).
