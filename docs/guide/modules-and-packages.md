# Modules and packages

A `.ki` file is a module. Its top-level forms run in order when it is
loaded, and it imports by Python's rules once the import hook is active:
a file is a module, a directory is a package, a directory with an
`__init__.ki` or `__init__.py` is a regular package, and a directory with
no `__init__` is a namespace package. Dotted names, `as` aliases, and
relative imports behave as they do in Python.

## The shape of a file

Definitions first, then the run-now part under `when-main:`, so that
importing the file does not run the script body:

```koskari
"""
Greet whoever is named on the command line
"""

import sys


define greet [name]:
  print "hello" name


when-main:
  for-each [name (item-slice sys.argv 1)]:
    (greet name)
```

A docstring at the top becomes the module's `__doc__`. The order that
reads best is imports, then constants and configuration, then functions
and classes, then `when-main:`; a reader can then answer what the file
needs, what it defines, and what happens when it runs.

## Imports

```koskari
import math
import os.path                                ;; binds os
import [os.path as: p]                        ;; binds the leaf as p
import-from pathlib Path
import-from functools [lru_cache as: memoize]
import-all-from mypackage                     ;; from mypackage import *
```

A plain dotted `import os.path` binds the root package, `os`, with
`os.path` reachable as an attribute, as Python does. The bracketed
`[MODULE as: NAME]` form binds the leaf. `import-all-from` binds a
module's public names and is allowed only at module top level. See
[`import`](../reference/specials/import.md),
[`import-from`](../reference/specials/import-from.md), and
[`use`](../reference/specials/use.md), which imports a macro together
with its runtime sibling.

## How the import hook works

Importing `koskari` installs a meta-path finder ahead of CPython's. That finder
recognizes the `.ki` and `.kio` extensions; when it locates such a source file it
loads it as a module and copies the module's names onto the Python module object,
so `(import-from mymod thing)` and `from mymod import thing` both work. When the
finder is asked for a **directory that has no `__init__`**, it deliberately steps
aside and lets CPython's own machinery produce an implicit namespace package.
That single deferral is why a bare directory of `.ki` files is importable with no
ceremony.

The hook is normally active because Koskari installs a site `*.pth` bootstrap.
In environments where that is not present (some editable installs, some embedded
hosts), make sure something runs `import koskari` before the first `.ki` import.
A package `__init__.py` containing just `import koskari` is the common insurance.

## The one rule, plus the `.ki` extension

Everything below is Python's import model with `.ki`/`.kio` added as recognized
source extensions:

| On disk | Imports as | Kind |
|---|---|---|
| `foo.ki` | `foo` | single module |
| `pkg/__init__.ki` (or `.py`) + `pkg/foo.ki` | `pkg`, `pkg.foo` | regular package |
| `pkg/foo.ki` with **no** `pkg/__init__.*` | `pkg.foo` | namespace package |
| `foo.kio` | `foo` | precompiled module |

## A regular package: `__init__.ki`

Give a directory an `__init__.ki` and it is a regular package. The init runs when
the package is first imported and typically re-exports the submodules' public
names using **relative imports** (a leading dot is "this package"):

```text
kosrunner/
  __init__.ki
  job.ki
  server.ki
  store.ki
```

```koskari
;; kosrunner/__init__.ki
import-from .job Config Job JobSet
import-from .server HttpServer get-server
import-from .store StorageConfig RunStore
```

`examples/scheduler/kosrunner/` is a full working example of this shape (a
pure-`.ki` package whose `__init__.ki` gathers a dozen submodules). A `.py`
`__init__` works equally well for a Koskari package; use `__init__.ki` when the
init logic is itself Koskari.

## A namespace package: no `__init__`

Leave the `__init__` out and the directory is an implicit namespace package. Its
`.ki` submodules still import as `pkg.submod`; there is simply no package-level
init and no re-exports:

```text
ki/
  loop.ki
  nbody.ki
```

```koskari
(import-from ki nbody)     ;; loads ki/nbody.ki
(import ki.loop)
```

This is the lightest layout: drop `.ki` files in a directory and import them as
submodules, no boilerplate file required.

## A mixed package: `__init__.py` with `.ki` submodules

A Python `__init__.py` alongside `.ki` submodules is a regular package too. This
is the recommended shape when you want the `import koskari` insurance in the init:

```text
koslife/
  __init__.py         ;; just: import koskari
  life.ki
  curse.ki
  cli.ki
```

Relative imports work across the `.ki` submodules regardless of whether the init
is `.py` or `.ki`:

```koskari
;; koslife/curse.ki
(import-from .life LifeBoard)
```

`examples/life/koslife/` is a working example. Deeper relative levels follow
Python's dot rules (each leading dot climbs one package); see
[`import`](../reference/specials/import.md) and [`import-from`](../reference/specials/import-from.md).

## The `.py` / `.ki` same-name trap

A `foo.py` and a `foo.ki` **in the same directory** both claim the module name
`foo`. That is an **import conflict with undefined behavior** -- which finder wins
is unspecified, and it can change with build state or import order. Do not rely
on it.

Two ways to keep two-language layouts unambiguous:

- **Distinct base names.** Differentiate before the extension: `foo_py.py` and
  `foo_ki.ki`, never `foo.py` + `foo.ki`.
- **Separate packages.** Put each language in its own directory so the *package*
  name disambiguates, and the base names can match:

  ```text
  py/
    nbody.py
  ki/
    nbody.ki
  ```

  ```koskari
  (import-from py nbody)     ;; py/nbody.py
  (import-from ki nbody)     ;; ki/nbody.ki
  ```

  `py/` and `ki/` are two separate top-level namespace packages (no `__init__`
  needed), so `py.nbody` and `ki.nbody` are distinct fully-qualified names with
  no collision. This is the clean way to hold a Python variant and a Koskari
  variant of the same thing side by side.

## Getting a directory onto the import path

Import resolution searches `sys.path`. The usual ways a directory lands there:

- **Running a script.** `python path/to/app.py` prepends the script's own
  directory to `sys.path[0]`, so files next to `app.py` import by bare name.
  Note the interaction with **`PYTHONSAFEPATH`** (equivalently `python -P`): when
  set, it **disables** that automatic prepend. If a runner sets
  `PYTHONSAFEPATH=1` globally, clear it (`PYTHONSAFEPATH=`) for the target that
  needs bare sibling imports, or put the directory on `PYTHONPATH` explicitly.
- **An installed package.** `pip install .` puts your declared packages on the
  path for good. For shipping an installed command, see
  [Packaging an installed command](packaging.md).
