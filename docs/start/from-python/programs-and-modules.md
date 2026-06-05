# Programs and modules

A `.ki` file is a module: imports and top-level `define` forms run at load time,
like a Python file executed on import. Scripts often end with **`when-main:`**
instead of `if __name__ == "__main__":`.

---

## What this chapter builds

You will structure a small program file, run it with `koskari`, use
`when-main:` for entry points, write comments, and use **`each`** /
**`list-each`** for comprehensions over Python iterables.

---

## Anatomy of a `.ki` file

A typical program file contains:

1. **Comments** — `;;` to end of line (like Lisp `;`).
2. **Imports** — `(import m)` or `(import-from m name …)` at top level.
3. **Definitions** — `define`, `define-class`, sometimes `define-syntax` later.
4. **Entry point** — optional `when-main:` block for script-only code.

There is no requirement to wrap everything in a `main()` function. Top-level
`define` binds module-level names the way module-level assignments do in Python.

Example skeleton:

```koskari
;; mytool.ki — process names from argv

import-from sys argv

define main []:
  for-each [arg (cdr argv)]:
    (process arg)

define process [name]:
  print "hello" name

when-main:
  (main)
```

---

## `when-main`

> **Q:** Python `if __name__ == "__main__":`?
>
> **A:**
>
> ```koskari
> when-main:
>   (main)
> ```
>
> Expands to run the body only when the module is executed as a script, not when
> imported.

> **Q:** Can I put several forms under `when-main:`?
>
> **A:** Yes — indent multiple lines like any other layout block:
>
> ```koskari
> when-main:
>   print "starting"
>   (main)
>   print "done"
> ```

See [compile/when-main](../../reference/macros/compile-when-main.md).

---

## Module docstrings

> **Q:** A string at the top of the file?
>
> **A:** Bootstrap and library `.ki` files often place a triple-quoted string
> after initial forms to document the module. Tooling may attach it to the module
> object similarly to Python docstrings on packages.

---

## Comprehensions: `each` and `list-each`

> **Q:** Generator expression `(f(x) for x in seq)`?
>
> **A:** **`each`** builds a generator:
>
> ```koskari
> each [x #[1 2 3]]:
>   (* x x)
> ```
>
> Optional filters: `when:` or `unless:` on the `each` form.

> **Q:** List comprehension `[f(x) for x in seq]`?
>
> **A:** **`list-each`**:
>
> ```koskari
> list-each [x #[1 2 3]]:
>   (* x x)
> ```
>
> Produces a Python list.

> **Q:** Dict comprehension?
>
> **A:** **`dict-each`** with `key:` and `value:` clauses — see the macros
> reference when you need exact shapes.

> **Q:** Generator over a Cartesian product (like nested `for` loops)?
>
> **A:** Combine **`each`** with a Python iterable, for example `itertools.product`:
>
> ```koskari
> (import-from itertools product)
> each [[x y] (product (range 0 w) (range 0 h))]:
>   (yield (x y))
> ```

---

## Running and importing

> **Q:** Run a script?
>
> **A:** `koskari path/to/file.ki` — see [Runtime and tooling](runtime-and-tooling.md).

> **Q:** Import from another `.ki` file?
>
> **A:** Same import rules as Python once the Koskari importer is active:
> `(import mypackage.mymodule)` loads `mymodule.ki` when present.

---

## Runnable mini-program

Save as `squares.ki`:

```koskari
define squares [n]:
  list-each [i (range 0 n)]:
    (* i i)

when-main:
  print (squares 6)
```

Run:

```bash
koskari squares.ki
```

Expected: `[0, 1, 4, 9, 16, 25]` (Python list printed).

Full recipe: [Script entry point](../../patterns/recipes/script-main.md).

---

## Try it

Run your `squares.ki` from the previous section, or open the
[Point class](../../patterns/recipes/point-class.md) recipe if you want a
full class plus `when-main`.

---

## Remember

`.ki` modules mirror Python modules. Use `when-main:` for script entry,
`;;` for comments, and `each` / `list-each` for comprehensions. Keep imports and
definitions at the top; run side effects under `when-main:` when appropriate.

Next: [Errors and debugging](errors-and-debugging.md).
