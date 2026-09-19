# Modules and program structure

A `.ki` file is a module-shaped program: top-level forms run in order when the
file is loaded or executed.

---

## Small script shape

A friendly script usually has definitions first and the run-now part last:

```koskari
define greet [name]:
  (print "hello" name)

(greet "Ada")
```

For reusable files, put demonstration or command-line behavior under
`when-main:` so importing the file does not run the script body immediately:

```koskari
define greet [name]:
  (print "hello" name)

when-main:
  (greet "Ada")
```

---

## Imports

Koskari imports mirror Python imports:

```koskari
(import math)
(import os.path)                                ;; binds root 'os'
(import [os.path as: p])                        ;; binds leaf as 'p'
(import-from pathlib Path)
(import-from functools [lru_cache as: memoize])
(import-all-from mypackage)                     ;; from mypackage import *
```

Like Python, a plain dotted `(import os.path)` binds the **root** package
(`os`), with `os.path` reachable as an attribute; use the bracketed
`[MODULE as: BIND]` form to bind the leaf module directly. `import-all-from`
binds a module's public names and is only allowed at module top level.

Import modules at the top of the file when possible. Use aliases when the alias
makes later code clearer, not just shorter.

---

## Public shape

Group a module in this order unless you have a good reason not to:

1. Imports.
2. Constants and simple configuration.
3. Functions and classes.
4. `when-main:` script entry point.

That shape lets readers answer “what does this file need?”, “what does it
define?”, and “what happens when I run it?” quickly.

---

## Larger projects

When a file grows, split by responsibility rather than by syntax. For example,
put parsing helpers in one module and reporting helpers in another. Import the
names you need and keep the top-level run-now code small.

When you split across a directory, `.ki` files follow Python's package rules: a
directory with an `__init__.ki` is a regular package, one without is a namespace
package, and either imports its `.ki` submodules the same way. For the full model
of how files on disk map to importable names — including packages, namespace
packages, the `.py`/`.ki` same-name conflict, and getting a directory onto
`sys.path` — see
[Modules and packages](modules-and-packages.md).

---

## What you should remember

Top-level forms run in order. Imports mirror Python. Use `when-main:` for script
entry points, and keep reusable definitions separate from run-now behavior.
