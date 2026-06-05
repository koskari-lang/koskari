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
(import-from pathlib Path)
(import-from functools (lru_cache as: memoize))
```

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

---

## What you should remember

Top-level forms run in order. Imports mirror Python. Use `when-main:` for script
entry points, and keep reusable definitions separate from run-now behavior.
