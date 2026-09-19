# compile/let-values

**Syntax:** `(let-values [[PATTERN VALUE] ...] BODY ...)`

**Behaviour:** `let` with unpacking. Each `PATTERN` is a list shape,
with an optional `*:` rest segment, and `VALUE` is unpacked into it for
the extent of the body, through
[`define-values`](../specials/compile-define-values.md).

**Examples:**

```koskari
koskari> (let-values [[u v] #(3 4)] (+ u v))
7
```

```koskari
let-values [[[name *: rest] sys.argv]]:
  print name (len rest)
```

**See also:** [`define-values`](../specials/compile-define-values.md),
[`let*`](compile-letstar.md).
