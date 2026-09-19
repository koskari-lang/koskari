# compile/set!

**Syntax:** `(set! SYM EXPR)`, `(set! (item OBJ KEY) EXPR)`,
`(set! (attr OBJ NAME) EXPR)`, `(set! (deref REF) EXPR)`,
`(set! (values PATTERN ...) EXPR)`

**Behaviour:** Stores `EXPR` into the target, choosing the special by
the target's shape: [`setq`](../specials/compile-setq.md) for a
variable, [`set-item`](../specials/compile-set-item.md) and
[`set-attr`](../specials/compile-set-attr.md) for a subscript and an
attribute, `set-ref` through a deref, and
[`setq-values`](../specials/compile-setq-values.md) for a pattern. The
subscript and dot suffixes read to the item and attribute forms, so
`d["k"]` and `obj.attr` are targets. An augmented store on a variable,
`(set! x (+ x 1))`, lowers to [`inplace`](../specials/compile-inplace.md),
and on an item or attribute to its in-place special.

**Examples:**

```koskari
define x 6
set! x 7

set! obj.count 0
set! d["k"] v

define r &x
set! (deref r) 8        ;; x is now 8

define point #(5 6)
set! (values a b) point
```

**See also:** [`incr!`, `decr!`](compile-incr-and-compile-decr.md),
[Bindings and refs](../../guide/bindings-and-refs.md).
