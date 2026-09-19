# compile/ref

**Syntax:** `(ref SYM)`, `(ref (item OBJ KEY))`, `(ref (attr OBJ NAME))`,
`(ref (parameter NAME))`, `(ref (environ KEY [DEFAULT]))`

**Behaviour:** Yields a ref, a handle to a storage location, by the
shape of the target: [`refq`](../specials/compile-refq.md) for a
variable, [`ref-item`](../specials/compile-ref-item.md) and
[`ref-attr`](../specials/compile-ref-attr.md) for a subscript and an
attribute, `(ParamRef 'NAME)` for a parameter, and an `environ-ref` into
`os.environ`. `deref` reads through the ref and `(set! (deref r) v)`
stores through it.

The reader spells `(ref ...)` with `&`: `&name`, `&obj.attr`, and
`&(item coll key)`, where the `&` touches what follows.

**Examples:**

```koskari
define total 6
define xs #[1 2 3]

define r &total
define ri (ref (item xs 0))
(deref r)              ;; 6
(deref ri)             ;; 1
(set! (deref ri) 10)   ;; xs is now [10, 2, 3]
```

**See also:** [`bind!`](compile-bind.md),
[Bindings and refs](../../guide/bindings-and-refs.md),
[References and storage indirection](../../guide/references-and-storage-indirection.md).
