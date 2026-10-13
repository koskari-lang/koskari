# compile/bind!

**Syntax:** `(bind! NAME REF)`, or one of the typed forms:
`(bind! NAME (item OBJ KEY))`, `(bind! NAME (attr OBJ ATTR))`,
`(bind! NAME (dynamic GETTER SETTER))`, `(bind! NAME (constant VALUE))`,
`(bind! NAME (memoized GETTER))`, `(bind! NAME (parameter PARAM))`,
`(bind! NAME (environ KEY [DEFAULT]))`.

**Behaviour:** Binds `NAME` to a ref as its storage, through
[`bindq`](../specials/compile-bindq.md), so that reading `NAME` derefs
and `set!` on `NAME` stores through. A plain `REF` is any ref-valued
expression, such as `&other`. The typed forms build the ref: an item or
attribute ref, a dynamic ref over two functions, a constant, a memoized
getter, a parameter, or a variable in `os.environ`.

**Examples:**

```koskari
var total 5
var xs #[1 2 3]

(bind! alias &total)
(set! alias 6)            ;; total is now 6

(bind! second (item xs 1))
(set! second 20)          ;; xs is now [1, 20, 3]

(bind! home (environ "HOME"))
home                      ;; the value of $HOME
```

**See also:** [`ref`](compile-ref.md),
[References and storage indirection](../../guide/references-and-storage-indirection.md).
