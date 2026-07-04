# Forms and combining

A **form** is one complete piece of Koskari code. Some forms are small values,
some look up names, and some combine other forms.

---

## The everyday shapes

Most code you read will be one of these shapes:

```koskari
42                         ;; a value
message                    ;; a name lookup
(+ 1 2)                    ;; a parenthesized call
(print "area:" (* 6 7))    ;; a call with a nested call
```

The parenthesized call rule is the one from the tutorials: evaluate the inputs,
then apply the thing in the first position.

For layout blocks and lines in `.ki` files, see
[Layout and s-expressions](../reference/concepts/layout-and-s-expressions.md).

---

## Several forms in order

A file contains forms that run from top to bottom:

```koskari
(define width 6)
(define height 7)
(print "area:" (* width height))
```

When you need one expression that contains several steps, use a block or
`begin`. The value is the last form:

```koskari
(begin
  (print "measuring")
  (* width height))
```

---

## Layout blocks are forms too

Koskari also has indentation-based layout. A block header ends with `:`, and the
indented body belongs to that header:

```koskari
if (> width height):
  (print "wide")
else:
  (print "tall or square")
```

Layout is reader syntax: it reads into ordinary Koskari forms before evaluation.
Use it when it makes code easier for humans to scan.

---

## Combining by naming

A name lets you give a result a role in the story:

```koskari
define subtotal 24
define tax (* subtotal 0.08)
define total (+ subtotal tax)
(print "total:" total)
```

Good names make later calls legible. Prefer `subtotal` over `x` once a program
has more than one idea.

---

## What you should remember

Forms are the pieces Koskari evaluates. Calls combine forms. Files and blocks
run forms in order, and the last form in an expression-oriented body is the
value you usually keep.
