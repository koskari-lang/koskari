# Layout and calls

Most Koskari you will write in a `.ki` file looks like Python: a header line
ending with a colon, then an indented body. Inside `()`, `[]`, or `{}` the
reader uses s-expression rules, which is why nested calls still look like
`(+ 1 2)`. The rules, with each layout shape beside the s-expression it reads
to, are in [Koskari and s-expressions](../../guide/layout-and-s-expressions.md).

---

## Defining functions with layout

`define` with formals uses the same block shape as Python `def`:

```koskari
define double [x]:
  * x 2
```

A one-line body can follow the formals on the same line, without a colon:

```koskari
define double [x] (* x 2)
```

---

## Conditionals: `if`, `elif`, `else`

`elif` and `else` are peer blocks at the same indent as the `if` header:

```koskari
define sign [n]:
  if (< n 0):
    "negative"
  elif (equal? n 0):
    "zero"
  else:
    "positive"
```

One-arm branches use `when`, and `unless` inverts the test:

```koskari
when (> n 0):
  (print n)
```

---

## Runnable mini-program

Save as `sign.ki` in any directory (with Koskari installed):

```koskari
define sign [n]:
  if (< n 0):
    "negative"
  elif (equal? n 0):
    "zero"
  else:
    "positive"

when-main:
  print (sign -3)
  print (sign 0)
  print (sign 9)
```

Run:

```bash
koskari sign.ki
```

Expected output (three lines):

```text
negative
zero
positive
```

---

## Try it

At the REPL, predict then check:

```koskari
koskari> define sq [x] (* x x)
koskari> (sq 5)
```

You should see `25`.

---

## Remember

- Layout is the default authoring style in `.ki` files; parentheses remain for
  nested expressions, inner calls, formals, and literals.
- `define name [formals]:` plus an indented body replaces most `def` uses in
  files; a single expression can sit on the same line after the formals.
- `if` / `elif` / `else` peers mirror Python's chain; see
  [Data and control](data-and-control.md) for loops and assignment next.

---

## See also

- [Koskari and s-expressions](../../guide/layout-and-s-expressions.md)
  -- the rules, side by side with their s-expressions
- [Reader and layout syntax](../../reference/reader-layout.md) -- normative syntax
  rules

Next: [Runtime and tooling](runtime-and-tooling.md).
