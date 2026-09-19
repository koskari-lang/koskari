# compile/let-values

**Syntax:** `(let-values [PATTERN VALUE] BODY ...)`

**Behaviour:** `let` with unpacking, over one binding. `PATTERN` is a
list shape, with an optional `*:` rest segment, and `VALUE` is unpacked
into it for the extent of the body, through
[`define-values`](../specials/compile-define-values.md). The bracketed
form holds the pattern and the value, not a list of pairs -- for more
than one unpacking, nest `let-values` forms.

**Examples:**

```koskari
>>> (let-values [[u v] #(3 4)] (+ u v))
7
```

```koskari
>>> (let-values [[a b *: more] #[1 2 3 4 5]] more)
#(3 4 5)
```

The rest segment collects a tuple.

```koskari
let-values [[name *: rest] sys.argv]:
  print name (len rest)
```

**See also:** [`define-values`](../specials/compile-define-values.md),
[`let*`](compile-letstar.md).
