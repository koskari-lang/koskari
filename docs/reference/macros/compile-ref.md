# compile/ref

**Syntax:** `(ref SYM)`, or one of the typed forms:
`(ref (item OBJ KEY))`, `(ref (attr OBJ NAME))`,
`(ref (dynamic GETFN [SETFN DELFN EMPTYFN]))`,
`(ref (constant VALUE))`, `(ref (memoized GETTER))`,
`(ref (parameter NAME [DEFAULT]))`, `(ref (environ KEY [DEFAULT]))`.

**Behaviour:** Yields a ref, a handle to a storage location, by the
shape of the target: [`refq`](../specials/compile-refq.md) for a
variable, [`ref-item`](../specials/compile-ref-item.md) and
[`ref-attr`](../specials/compile-ref-attr.md) for a subscript and an
attribute, a `DynamicRef` over the supplied functions, a `ConstantRef`
that refuses stores, a `MemoizedRef` that calls its getter on the first
deref and keeps the answer, `(ParamRef 'NAME)` for a parameter, and an
`environ-ref` into `os.environ`. `deref` reads through the ref and
`(set! (deref r) v)` stores through it.

The `dynamic` and `memoized` refs demand-wrap their functions, so a
Koskari lambda works directly as a getter or setter.

The reader spells `(ref ...)` with `&`: `&name`, `&obj.attr`, and
`&(item coll key)`, where the `&` touches what follows.

**Examples:**

```koskari
var total 6
var xs #[1 2 3]

var r &total
var ri (ref (item xs 0))
(deref r)              ;; 6
(deref ri)             ;; 1
(set! (deref ri) 10)   ;; xs is now #[10 2 3]
```

The computed arms:

```koskari
var store #{"n": 1}

var rd (ref (dynamic (lambda [] (item store "n"))
                        (lambda [v] (set-item store "n" v))))
(deref rd)             ;; 1
(set! (deref rd) 9)
(deref rd)             ;; 9

var rc (ref (constant 42))
(deref rc)             ;; 42

var calls #[]
var rm (ref (memoized (lambda [] (.append calls 1) "computed")))
(deref rm)             ;; "computed"
(deref rm)             ;; "computed", the getter ran once
(len calls)            ;; 1
```

**See also:** [`bind!`](compile-bind.md),
[Bindings and refs](../../guide/bindings-and-refs.md),
[References and storage indirection](../../guide/references-and-storage-indirection.md).
