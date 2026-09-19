# Layout and calls

Most Koskari in a `.ki` file looks like Python: a header line ending in a
colon, then an indented body. Inside `(`, `[`, or `{` the reader switches
to s-expression rules, which is why a nested call is still `(+ 1 2)`.

## Two rules

:::{dialogue}
What does a line mean?
+++
A line with more than one element is a call whose first element is the
callee. A line with one element is that element. So `print "hi"` is
`(print "hi")`, and `x` alone is the value of `x`.
:::

:::{dialogue}
What does a colon mean?
+++
A line ending in `:` opens a block, and the indented lines under it are
arguments to that line. `define double [x]:` followed by a body is one
form, `(define double [x] BODY...)`.
:::

## Functions

:::{pair}
```python
def double(x):
    return x * 2
```
+++
```koskari
define double [x]:
  (* x 2)
```
:::

:::{dialogue}
Where is `return`?
+++
The last form in the body is the result. `return` exists for leaving
early; chapter 4 covers it.
:::

:::{pair}
```python
double = lambda x: x * 2
```
+++
```koskari
define double [x] (* x 2)
```
:::

:::{dialogue}
Why no colon on that one?
+++
The body fits on the header line, so there is nothing to continue. A
colon is only for a body that follows on indented lines.
:::

## Conditionals

:::{pair}
```python
def sign(n):
    if n < 0:
        return "negative"
    elif n == 0:
        return "zero"
    else:
        return "positive"
```
+++
```koskari
define sign [n]:
  if (< n 0):
    "negative"
  elif (== n 0):
    "zero"
  else:
    "positive"
```
:::

:::{dialogue}
Why is the test in parentheses?
+++
`(< n 0)` is a call to `<`, and a call inside a header needs its
parentheses. Only the line itself gets the implicit call.
:::

:::{pair}
```python
if n > 0:
    print(n)

if not ready:
    print("wait")
```
+++
```koskari
when (> n 0):
  print n

unless ready:
  print "wait"
```
:::

## Where parentheses stay

:::{dialogue}
When do I still write them?
+++
- A nested expression: `(< n 0)` in a header, `(* x 2)` in a body.
- A call used as an argument: `print (sign -3)`.
- Formals and bindings: `[n]`, `[[a 1] [b 2]]`.
- Collection literals: `#[1 2 3]`.

Statement-shaped code takes layout; expression-shaped code takes
parentheses. Every layout form reads to the same s-expression, so a
whole program written with parentheses only is also valid.
:::

## Run it

Save as `sign.ki`:

```koskari
define sign [n]:
  if (< n 0):
    "negative"
  elif (== n 0):
    "zero"
  else:
    "positive"

when-main:
  print (sign -3)
  print (sign 0)
  print (sign 9)
```

```bash
koskari sign.ki
```

```text
negative
zero
positive
```

The rules, each beside the s-expression it reads to, are in
[Koskari and s-expressions](../../guide/layout-and-s-expressions.md).
