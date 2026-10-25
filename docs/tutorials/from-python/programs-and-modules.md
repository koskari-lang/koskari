# Programs and modules

A `.ki` file is a module. Top-level forms run when it loads, as a Python
file's do on import.

## Running

:::{pair}
```bash
python prog.py
python -i prog.py
python
```
+++
```bash
koskari prog.ki
koskari -i prog.ki
koskari
```
:::

:::{dialogue}
Where do `.ki` files live?
+++
Beside your `.py` files, in the same packages and on the same
`sys.path`. Once `import koskari` has run, Python's `import mypkg.mod`
finds `mypkg/mod.ki`. Running a script adds its directory to the path,
as Python does.
:::

## The shape of a file

:::{pair}
```python
"""Process names from argv."""

import sys


def process(name):
    print("hello", name)


def main():
    for name in sys.argv[1:]:
        process(name)


if __name__ == "__main__":
    main()
```
+++
```koskari
"""
Process names from argv
"""

import sys


def process [name]:
  print "hello" name


@entry-point
def main []:
  for-each [name (item-slice sys.argv 1)]:
    (process name)
```
:::

:::{dialogue}
What is `@entry-point`?
+++
The mark on the function that runs when the file is executed as a
program and not when it is imported. Where Python guards a block with
`if __name__ == "__main__":` and runs it while the module is still
loading, Koskari calls the marked function once the module has
finished loading, and its return value is the exit status: `None` is
`0`, an int is that status. `main` is still an ordinary function that
a test or another module can call. Comments are `;` to the end of the
line, doubled by custom.
:::

## Comprehensions

:::{pair}
```python
squares = [x * x for x in range(6)]
```
+++
```koskari
var squares:
  list-each [x (range 6)]:
    (* x x)
```
:::

:::{pair}
```python
evens = [x for x in xs if x % 2 == 0]
```
+++
```koskari
var evens:
  list-each [x xs] when: (== 0 (% x 2)):
    x
```
:::

:::{pair}
```python
lengths = {w: len(w) for w in words}
```
+++
```koskari
var lengths:
  dict-each [w words] key: w value: (len w)
```
:::

:::{pair}
```python
gen = (x * x for x in xs)
```
+++
```koskari
var gen:
  each [x xs]:
    (* x x)
```
:::

:::{dialogue}
Which one is which?
+++
`list-each` builds a list, `dict-each` a dict, `each` a generator. All
three take the `[var seq]` binding that `for-each` takes, and a `when:`
or `unless:` clause on the header filters. A `for-each` loop is for
side effects; the `-each` family is for values.
:::

## Imports

:::{pair}
```python
import math
import os.path
from pathlib import Path
from functools import lru_cache as memoize
from mypkg import *
```
+++
```koskari
import math
import os.path
import-from pathlib Path
import-from functools [lru_cache as: memoize]
import-all-from mypkg
```
:::

:::{dialogue}
Does `import os.path` bind `os`?
+++
Yes, as in Python. `import [os.path as: p]` binds the leaf module
directly.
:::

## Packages

:::{pair}
```text
mypkg/
  __init__.py
  tools.py
```
+++
```text
mypkg/
  __init__.py
  tools.ki
```
:::

:::{dialogue}
Does the `__init__` have to be Python?
+++
No; `__init__.ki` works, and so does no `__init__` at all, which gives a
namespace package as it would in Python. A Python `__init__.py` that
runs `import koskari` is the common choice, since it turns the import
hook on for anyone who imports the package. The rules, including the
one trap, a `foo.py` beside a `foo.ki`, are in
[Modules and packages](../../guide/modules-and-packages.md).
:::

## Run it

Save as `squares.ki`:

```koskari
def squares [n]:
  list-each [i (range n)]:
    (* i i)

@entry-point
def main []:
  print (squares 6)
```

```text
[0, 1, 4, 9, 16, 25]
```

To ship a `.ki` program as a pip-installed command, see
[Packaging an installed command](../../guide/packaging.md).
