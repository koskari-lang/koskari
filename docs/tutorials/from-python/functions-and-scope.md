# Functions and scope

`def`, `lambda`, defaults, closures, and decorators all have a direct
spelling. One thing is new: a parameter that refers to the caller's
variable.

## Defining

:::{pair}
```python
def greet(name, greeting="hello"):
    return f"{greeting} {name}"
```
+++
```koskari
define greet [name greeting: "hello"]:
  f"{greeting} {name}"
```
:::

:::{dialogue}
How are keyword defaults written?
+++
`name: value` in the formals, and `name: value` at the call:
`(greet "Ada" greeting: "hi")`. A `*: rest` formal collects extra
positionals and `**: kwargs` collects extra keywords.
:::

:::{pair}
```python
lambda x: x + 1
```
+++
```koskari
(lambda [x] (+ x 1))
```
:::

## Returning

:::{pair}
```python
def first_even(xs):
    for x in xs:
        if x % 2 == 0:
            return x
    return None
```
+++
```koskari
define first-even [xs]:
  for-each [x xs]:
    when (== 0 (% x 2)):
      return x
  None
```
:::

:::{dialogue}
When do I write `return`?
+++
To leave early. Otherwise the last form is the result. `return` with
two or more arguments returns a tuple, and `define-values [a b] (f)`
unpacks it.
:::

## Local names

:::{pair}
```python
def area():
    w, h = 6, 7
    return w * h
```
+++
```koskari
define area []:
  let [[w 6] [h 7]]:
    (* w h)
```
:::

:::{dialogue}
What does `let` add over `define`?
+++
Scope. The names exist only inside the `let` body. A `define` in a
function body is local to the function, as in Python, but a `let` can
scope names to one calculation inside it.
:::

:::{pair}
```python
def count_down(n):
    while n > 0:
        print(n)
        n -= 1
```
+++
```koskari
define count-down [n]:
  let loop [[n n]]:
    when (> n 0):
      print n
      (loop (- n 1))
```
:::

:::{dialogue}
What is a named `let`?
+++
A loop written as a function. `loop` is bound inside the body to a
function of the same bindings; calling it is the next iteration. Tail
calls do not grow the stack, so this is as cheap as `while`. Use
whichever reads better.
:::

## Closures

:::{pair}
```python
def make_counter():
    count = 0
    def tick():
        nonlocal count
        count += 1
        return count
    return tick
```
+++
```koskari
define make-counter []:
  define count 0
  define tick []:
    incr! count
    count
  tick
```
:::

:::{dialogue}
Where is `nonlocal`?
+++
Not needed. `set!` and `incr!` store into the name they find, and the
inner function finds the outer `count`. `var` and `global` exist for
the cases where you want to say so explicitly.
:::

## By-reference parameters

:::{pair}
```python
def bump(box):
    box[0] += 1

value = [10]
bump(value)
value[0]   # 11
```
+++
```koskari
define bump [&n]:
  incr! n

define value 10
(bump &value)
value   ; 11
```
:::

:::{dialogue}
What is the `&`?
+++
A by-reference parameter. `&n` in the formals says the parameter is the
caller's storage location, not a copy of the value, and `&value` at the
call passes that location. Python has no way to rebind a caller's
variable; the box in the Python column is the usual workaround. See
[References and storage indirection](../../guide/references-and-storage-indirection.md).
:::

## Decorators

:::{pair}
```python
from functools import lru_cache

@lru_cache(maxsize=None)
def fib(n):
    ...
```
+++
```koskari
import-from functools lru_cache

@(lru_cache maxsize: None)
@demand
define fib [n]:
  ...
```
:::

:::{dialogue}
Why the extra `@demand`?
+++
A Koskari function is ambiently async: called from Koskari it yields a
value, but called from plain Python code, which `lru_cache` is, it
yields a coroutine. `@demand` gives the Python side a function that
returns the value. Any Python decorator that calls the function it
wraps wants this; a decorator that only registers it does not. The
[Fibonacci](examples/fibonacci.md) example shows both.
:::

## Higher-order functions

:::{pair}
```python
list(map(lambda x: x * 2, xs))
sorted(xs, key=lambda x: -x)
```
+++
```koskari
(list (map (lambda [x] (* x 2)) xs))
(sorted xs key: (lambda [x] (- x)))
```
:::

:::{dialogue}
Is that Python's `map`?
+++
No, Koskari's, which takes a Koskari function directly. `sorted` is
Python's and wraps the key for you. Other Python functions that call a
callable you pass them, `min`, `max`, `list.sort`, want a `demand`
around a Koskari lambda. See
[Demand, async, and defer](../../guide/demand-async-and-defer.md).
:::

## Run it

Save as `greet.ki`:

```koskari
define greet [name greeting: "hello" punct: "!"]:
  (+ greeting " " name punct)

@entry-point
define main []:
  print (greet "Ada")
  print (greet "Grace" greeting: "Hi" punct: ".")
```

```text
hello Ada!
Hi Grace.
```
