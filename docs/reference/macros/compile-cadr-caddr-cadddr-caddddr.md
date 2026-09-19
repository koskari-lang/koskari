# cadr, caddr, cadddr, caddddr, first ... fifth

**Syntax:** `(cadr PAIR)`, `(caddr PAIR)`, `(cadddr PAIR)`,
`(caddddr PAIR)`; `(first PAIR)` through `(fifth PAIR)`

**Behaviour:** The accessors expand to nested `car` and `cdr`: `cadr`
is the second element, `caddr` the third, and so on. `first` through
`fifth` are aliases for `car`, `cadr`, `caddr`, `cadddr`, and
`caddddr`. They are macros, so in call position they expand even when
a local binding of the same name exists.

**Examples:**

```koskari
koskari> (cadr '(1 2 3))
2
koskari> (third '(1 2 3))
3
koskari> (fifth '(1 2 3 4 5))
5
```

**See also:** [`car`](../specials/compile-car.md),
[`cdr`](../specials/compile-cdr.md),
[Data structures](../../guide/data-structures.md).
