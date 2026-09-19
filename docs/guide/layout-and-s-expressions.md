# Koskari and s-expressions

Koskari is the layout language: indentation carries structure, and
parentheses carry expressions. Every layout form reads to the same list
structure that a parenthesized s-expression produces, so the compiler, the
macro expander, and the evaluator never see which spelling was used. Layout
reduces the reliance on parentheses for structure; it does not remove them.

The rules below are the whole surface. Each is shown as a pair: the layout
spelling on the left, and on the right the s-expression the reader produces
from it. The normative statement of the rules is
[Reader and layout syntax](../reference/reader-layout.md).


## Lines

A line with one element is that value. A line with more than one element is
a form headed by the first, which is a call, a definition, or any other
form the head names.

:::{pair}
:left: Koskari
:right: s-expression

```koskari
print "hello" name
name
define sq [x] (* x x)
```
+++
```koskari
(print "hello" name)
name
(define sq [x] (* x x))
```
:::


## Blocks

A line ending in `:` opens a block. The header's elements and the body's
lines join into one form, and each body line follows the line rule on its
own. The head names a layout handler, `layout/<head>`; with no handler
registered, the default splices body forms after the header.

:::{pair}
:left: Koskari
:right: s-expression

```koskari
when (> n 0):
  print n
  n
```
+++
```koskari
(when (> n 0) (print n) n)
```
:::


## Named heads and peers

Some heads have their own handler, which may gather peer blocks at the same
indent and lower the whole group to one form. Block `if` gathers `elif` and
`else` and reads as `cond`.

:::{pair}
:left: Koskari
:right: s-expression

```koskari
if (< x 0):
  "negative"
elif (== x 0):
  "zero"
else:
  "positive"
```
+++
```koskari
(cond
  [(< x 0) "negative"]
  [(== x 0) "zero"]
  [else: "positive"])
```
:::

The handlers and their peers are catalogued in the
[layout handler reference](../reference/layouts/index.md).


## Keywords

A line that opens with a keyword is an argument name and its value. One
form after the keyword is the value; several fold to a call. A keyword
followed by `:` takes a block as its value, and a bare `k::` takes a
`begin` block.

:::{pair}
:left: Koskari
:right: s-expression

```koskari
print:
  "hello"
  file: stderr
  end: ""
```
+++
```koskari
(print "hello" file: stderr end: "")
```
:::


## Parentheses inside layout

Any `(`, `[`, or `{` switches to s-expression rules until it closes, at any
depth. A newline inside an open delimiter does not end the line, so a
header argument may span lines.

:::{pair}
:left: Koskari
:right: s-expression

```koskari
let [[a 1]
     [b 2]]:
  + a b
```
+++
```koskari
(let [[a 1] [b 2]]
  (+ a b))
```
:::


## Side by side

:::{pair}
:left: Koskari
:right: s-expression

```koskari
define greet [name]:
  print f"hello {name}"
  name
```
+++
```koskari
(define greet [name]
  (print f"hello {name}")
  name)
```
:::

:::{pair}
:left: Koskari
:right: s-expression

```koskari
for-each [item items]:
  print item
```
+++
```koskari
(for-each [item items]
  (print item))
```
:::

:::{pair}
:left: Koskari
:right: s-expression

```koskari
while (running? x):
  step x
else:
  "done"
```
+++
```koskari
(while (running? x)
  (step x)
  (else: "done"))
```
:::

:::{pair}
:left: Koskari
:right: s-expression

```koskari
try:
  risky x
except [e ValueError]:
  print e
finally:
  cleanup
```
+++
```koskari
(try (begin (risky x))
  [[e ValueError] (print e)]
  [finally: cleanup])
```
:::

:::{pair}
:left: Koskari
:right: s-expression

```koskari
define-class Point (Base):
  define x 0
```
+++
```koskari
(define-class Point (Base)
  (begin (define x 0)))
```
:::

:::{pair}
:left: Koskari
:right: s-expression

```koskari
render:
  #[1 2 3]
  sep: ", "
```
+++
```koskari
(render #[1 2 3] sep: ", ")
```
:::


## Where parentheses stay

Four places keep their delimiters inside layout:

- Nested expressions, such as `(< x 0)` in a header.
- Inner calls on a line, such as `print (sign -3)`.
- Formals and bindings, such as `[name]` and `[[a 1] [b 2]]`.
- Collection literals, such as `#[1 2 3]`.

Statement-shaped code takes layout; expression-shaped code takes
parentheses. A whole program written with parentheses only is valid Koskari,
and the REPL accepts both spellings.


## See also

- [Reader and layout syntax](../reference/reader-layout.md), the normative block, line, colon, and peer rules
- [Layout handler reference](../reference/layouts/index.md), per-head lowering
- [Layout and calls](../tutorials/from-python/layout-and-calls.md), tutorial patterns in `.ki` files
