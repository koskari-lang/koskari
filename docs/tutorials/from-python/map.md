# Map

What carries over from Python unchanged, what looks the same but is not,
and what is new.

## Same runtime

:::{dialogue}
Is Koskari a new Python implementation?
+++
No. Koskari runs inside CPython. Numbers, strings, lists, dicts, the
builtins, and every module you import are ordinary Python objects. What
changes is how you write programs.
:::

:::{dialogue}
So `(+ 1 2)` is a call to `+`?
+++
Yes. The first thing inside the parentheses is the callee and the rest
are its arguments, the way `f(x, y)` puts the name first and the
arguments in one pair of parentheses. In a `.ki` file, a line with more
than one element is the same call without the parentheses: `print "hi"`
reads as `(print "hi")`.
:::

## The shapes, side by side

:::{pair}
```python
import os
from os import path
```
+++
```koskari
import os
import-from os path
```
:::

:::{pair}
```python
obj.attr
obj.method(a, b)
xs[0]
```
+++
```koskari
obj.attr
(obj.method a b)
xs[0]
```
:::

:::{pair}
```python
x = 6
x = 7
x += 1
```
+++
```koskari
define x 6
set! x 7
incr! x
```
:::

:::{pair}
```python
if n < 0:
    ...
elif n == 0:
    ...
else:
    ...
```
+++
```koskari
if (< n 0):
  ...
elif (== n 0):
  ...
else:
  ...
```
:::

:::{pair}
```python
while cond:
    ...

for x in seq:
    ...
```
+++
```koskari
while cond:
  ...

for-each [x seq]:
  ...
```
:::

:::{pair}
```python
def f(x, y=2):
    return x + y
```
+++
```koskari
define f [x y: 2]:
  (+ x y)
```
:::

:::{pair}
```python
class C(Base):
    ...
```
+++
```koskari
define-class C (Base):
  ...
```
:::

:::{pair}
```python
try:
    ...
except ValueError as e:
    ...
```
+++
```koskari
try:
  ...
except [ValueError as: e]:
  ...
```
:::

:::{pair}
```python
[1, 2, 3]
(1, 2, 3)
{"a": 1}
```
+++
```koskari
#[1 2 3]
#(1 2 3)
#{"a": 1}
```
:::

## Looks the same, is not

:::{dialogue}
What looks like Python and is not?
+++
- **A line is one form.** `print "hi"` on its own line is a call. A lone
  `print` is the function.
- **`define` and `set!`.** `define` introduces a name; `set!` changes one
  that exists. Python's `=` does both.
- **`[a b]` is not a list.** Square brackets read like parentheses. In
  `define f [x y]` they hold the parameters. A list literal is `#[...]`.
- **`None` and `nil`.** `None` is Python's. `nil` is the empty Lisp
  list. `(None? x)` and `(nil? x)` ask different questions.
- **Truth tests are Python's.** `if`, `while`, and `cond` test Python
  truthiness, so `#[]` and `0` are false.
- **Commas are not separators.** Whitespace is. `#[1, 2]` reads two odd
  symbols.
:::

## New

:::{dialogue}
What has no Python counterpart?
+++
- **Macros.** `define-syntax` rewrites code before it runs. Chapter 9.
- **By-reference parameters.** A function can write into its caller's
  variable. Chapter 4.
- **Pairs and quoting.** The Lisp list, for macros and code as data.
- **Continuations.** Present in the language; not needed on day one.
:::
