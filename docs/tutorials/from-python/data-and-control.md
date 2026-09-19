# Data and control

Values and control flow are where Python habits carry the furthest.

## Atoms

:::{pair}
```python
42
-3
"hello"
True, False, None
```
+++
```koskari
42
-3
"hello"
True False None
```
:::

:::{dialogue}
What is `nil`?
+++
The empty Lisp list, the end of every pair chain. It is not `None`:
`(nil? nil)` is true and `(None? nil)` is false. Python data never
produces one; you meet it with quoted lists and macros.
:::

## Collections

:::{pair}
```python
[1, 2, 3]
(1, 2, 3)
{"a": 1, "b": 2}
```
+++
```koskari
#[1 2 3]
#(1 2 3)
#{"a": 1 "b": 2}
```
:::

:::{dialogue}
Why the `#`?
+++
Bare `[1 2 3]` reads like `(1 2 3)`, a call of `1`. The `#` marks a
literal. Keys in `#{...}` are expressions, so a string key is quoted; a
bare `a:` key would look up the variable `a`.
:::

:::{pair}
```python
[*xs, 1, 2]
(1, *xs)
```
+++
```koskari
#[*: xs 1 2]
#(1 *: xs)
```
:::

:::{pair}
```python
xs[0]
xs[-1]
d["a"]
xs[1:]
```
+++
```koskari
xs[0]
xs[-1]
d["a"]
(item-slice xs 1)
```
:::

## Assignment

:::{pair}
```python
x = 6
x = 7
x += 1
n -= 2
```
+++
```koskari
define x 6
set! x 7
incr! x
decr! n 2
```
:::

:::{dialogue}
Why two words for `=`?
+++
`define` introduces a name in the current scope. `set!` stores into a
name that exists, and it is an error if none does. Python lets one
symbol do both and infers which you meant.
:::

:::{pair}
```python
obj.count = 0
d["k"] = v
```
+++
```koskari
set! obj.count 0
set! d["k"] v
```
:::

## Conditionals

:::{pair}
```python
if x < 0:
    s = "negative"
elif x == 0:
    s = "zero"
else:
    s = "positive"
```
+++
```koskari
define s:
  if (< x 0):
    "negative"
  elif (== x 0):
    "zero"
  else:
    "positive"
```
:::

:::{dialogue}
Why does the `define` go outside?
+++
An `if` is an expression. Its value is the value of the branch that
ran, so one `define` takes the whole choice. Python's `if` is a
statement, so each branch assigns.
:::

:::{pair}
```python
def invite(user):
    if not user:
        return "missing user"
    if not user.email:
        return "missing email"
    return f"sent to {user.email}"
```
+++
```koskari
define invite [user]:
  unless user:
    return "missing user"
  unless user.email:
    return "missing email"
  f"sent to {user.email}"
```
:::

:::{dialogue}
What are `when` and `unless`?
+++
One-armed `if`. `when` runs its body if the test is true, `unless` if
it is false. They keep a guard clause positive instead of writing
`(not ...)`.
:::

## Loops

:::{pair}
```python
while n > 0:
    n -= 1
else:
    print("done")
```
+++
```koskari
while (> n 0):
  decr! n
else:
  print "done"
```
:::

:::{pair}
```python
for item in seq:
    if item.ok:
        continue
    process(item)
```
+++
```koskari
for-each [item seq]:
  when item.ok:
    continue
  (process item)
```
:::

:::{dialogue}
Does `for-each` have `else` and `break`?
+++
Both, as in Python: the `else:` peer runs when the loop ends without a
`break`. `break` may carry a value, which becomes the value of the
loop:

```koskari
define found:
  for-each [v values]:
    when (> v 2):
      break v
  else:
    None
```
:::

:::{pair}
```python
a and b
a or b
not a
```
+++
```koskari
(and a b)
(or a b)
(not a)
```
:::

## Run it

Save as `countdown.ki`:

```koskari
define countdown [n]:
  while (> n 0):
    print n
    decr! n
  else:
    print "lift-off"

when-main:
  (countdown 3)
```

```text
3
2
1
lift-off
```
