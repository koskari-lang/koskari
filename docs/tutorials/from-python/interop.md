# Interop

Koskari and Python share one process. Calls cross the boundary in both
directions.

## Koskari calls Python

:::{pair}
```python
from math import sqrt
import os.path as osp

sqrt(9)
osp.join("a", "b")
"abc".upper()
```
+++
```koskari
import-from math sqrt
import [os.path as: osp]

(sqrt 9)
(osp.join "a" "b")
("abc".upper)
```
:::

:::{dialogue}
Do I convert anything?
+++
No. Every value is already a Python object. Arguments go in as they
are, results come back as they are, and a Python function sees nothing
unusual about being called from Koskari.
:::

:::{dialogue}
Are attribute names translated?
+++
No. Python names are used verbatim: `(asyncio.get_running_loop)`, with
the underscore. Koskari's own names keep their dashes unless a Python
caller has to reach them by identifier, in which case spell them with
underscores.
:::

## Python calls Koskari

:::{pair}
```python
import koskari       # installs the .ki import hook
import mypkg.tools   # loads mypkg/tools.ki

mypkg.tools.CONSTANT
```
+++
```koskari
;; mypkg/tools.ki
define CONSTANT 42

define-demanding shout [word]:
  (+ (word.upper) "!")
```
:::

:::{dialogue}
Why `define-demanding`?
+++
A plain `define` produces an ambiently async function. Called from
Koskari it returns a value; called from Python it returns a coroutine.
`define-demanding` wraps the function so a Python caller with no event
loop gets the value. Any function Python will call directly wants it;
functions called only from Koskari do not.
:::

:::{pair}
```python
from mypkg.tools import shout

shout("hi")   # 'HI!'
```
+++
```koskari
(shout "hi")   ; "HI!" either way
```
:::

## Callbacks

:::{pair}
```python
sorted(rows, key=lambda r: r["k"])
rows.sort(key=lambda r: r["k"])
```
+++
```koskari
(sorted rows key: (lambda [r] r["k"]))
(rows.sort key: (demand (lambda [r] r["k"])))
```
:::

:::{dialogue}
Why does one need `demand` and not the other?
+++
`sorted` is a Koskari builtin that wraps the key for you. `rows.sort`
is Python's method and calls the key directly, so it gets coroutines
unless the lambda is wrapped. `itemgetter` and `attrgetter` are the
usual answer: `(rows.sort key: (itemgetter "k"))` builds a plain Python
callable with no lambda at all. The same applies to `min`, `max`, and
any Python function that calls what you hand it.
:::

## Async

:::{dialogue}
Do I write `await`?
+++
Rarely. A call that returns an awaitable is awaited at the call. The
explicit `await` is for a `defer`, a Task, or a Future you chose to
hold. See [Demand, async, and defer](../../guide/demand-async-and-defer.md).
:::

## What does not cross

:::{dialogue}
Can I paste Python into a `.ki` file?
+++
No. The syntax is Koskari's. Everything Python's import system can
reach is usable; Python source is not.
:::

## Run it

At the REPL:

```koskari
koskari> (import-from math pi)
koskari> (* pi 2)
6.283185307179586
```

To host Koskari inside a Python application, load files into a
namespace you control, or run the REPL from Python, see
[Hosting Koskari from Python](../../guide/hosting-from-python.md).
