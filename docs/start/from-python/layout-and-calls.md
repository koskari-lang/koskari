# Layout and calls

Most Koskari you will write in a `.ki` file looks like Python: a header line
ending with a colon, then an indented body. The reader turns those **layout
blocks** into the same forms the compiler already understands. You do not need
to memorize s-expressions to be productive.

---

## Top level vs inside parentheses

At **top level** in a file (or at the REPL when you start a fresh line outside
`()`, `[]`, or `{}`), Koskari reads **layout mode**: header until `:`, then
indented body.

Inside delimiters, the reader uses the classic s-expression rules. That is why
nested calls still look like `(+ 1 2)` even when the surrounding function uses
layout syntax.

> **Q:** Can I mix styles in one file?
>
> **A:** Yes. Real programs use layout for definitions and control flow, and
> parentheses for nested expressions, collection literals, and short inner calls.

---

## Defining functions with layout

`define` with formals uses the same block shape as Python `def`:

```koskari
define double [x]:
  * x 2
```

this is semantically identical to the s-expr

```koskari
(define double [x] (* x 2))
```

**Under the hood:** the layout reader splices the body lines as expressions
after the header, producing a flat list the `define` form compiles.

---

## Conditionals: `if`, `elif`, `else`

Layout `if` lowers to `cond` with optional `elif` and `else` **peers** at the
same indent level as the primary `if` header.

```koskari
define sign [n]:
  if (< n 0):
    "negative"
  elif (equal? n 0):
    "zero"
  else:
    "positive"
```

One-arm branches use `when` (no `else`):

```koskari
when (> n 0):
  (print n)
```

`unless` inverts the test (body runs when the condition is false).

See [layout/if](../../reference/layouts/layout-if.md).

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

You should see `25`. A one-line body can follow the formals on the same line
(without a colon). Multi-line bodies in files use `define name [formals]:` and
indentation, as above.

---

## Remember

- Layout is the default authoring style in `.ki` files; parentheses remain for
  nested expressions.
- `define name [formals]:` plus an indented body replaces most `def` uses in
  files; a single expression can sit on the same line after the formals.
- `if` / `elif` / `else` peers mirror Python’s chain; see
  [Data and control](data-and-control.md) for loops and assignment next.

---

## See also

- [Layout and s-expressions](../../reference/concepts/layout-and-s-expressions.md)
  — mental model and three authoring surfaces
- [Reader and layout syntax](../../reference/reader-layout.md) — normative syntax
  rules

Next: [Runtime and tooling](runtime-and-tooling.md).
