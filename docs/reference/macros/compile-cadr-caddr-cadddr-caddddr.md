# cadr, caddr, cadddr, caddddr, first ... fifth

**Syntax:** `(cadr PAIR)`, `(caddr PAIR)`, `(cadddr PAIR)`,
`(caddddr PAIR)`; `(first PAIR)` through `(fifth PAIR)`

**Behaviour:** `cadr` through `caddddr` are macros that take nested
`car` and `cdr` of PAIR: `cadr` is the second element, `caddr` the
third, and so on. Being macros, they take effect in call position even when
a local binding of the same name exists. Each also has a runtime
function binding of the same name, so `cadr` can be handed to a
higher-order function as a value.

`first` through `fifth` are runtime bindings only -- `car`, `cadr`,
`caddr`, `cadddr`, and `caddddr` as functions. They resolve as ordinary
values, so a local binding of the same name shadows them in call
position.

**Examples:**

```koskari
>>> (cadr '(1 2 3))
2
>>> (third '(1 2 3))
3
>>> (fifth '(1 2 3 4 5))
5
>>> (list (map first #['(1 2) '(3 4)]))
#[1 3]
```

```koskari
>>> (let [[first (lambda [p] "shadowed")]] (first '(1 2 3)))
"shadowed"
>>> (let [[cadr (lambda [p] "shadowed")]] (cadr '(1 2 3)))
2
```

**See also:** [`car`](../specials/compile-car.md),
[`cdr`](../specials/compile-cdr.md),
[Data structures](../../guide/data-structures.md).

**Also:** function [`caddddr`, `cadr`, `fifth`, `first`](../runtime/functions.md#pairs).
