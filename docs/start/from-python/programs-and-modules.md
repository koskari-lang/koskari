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
  define args (item-slice argv 1)
  for-each [arg args]:
    (process arg)

define process [name]:
  print "hello" name

when-main:
  (main)
```

**`item-slice`** drops the first element of **`argv`**; see
[compile/item-slice](../../reference/macros/compile-slice-and-compile-item-slice.md).

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

Python gives you several related shapes:

```python
squares_iter = (x * x for x in range(6))
squares_list = [x * x for x in range(6)]
evens = [x for x in range(10) if x % 2 == 0]
```

Koskari splits those shapes into names that say what they produce. **`each`** is
for generators. **`list-each`** is for a Python list. **`dict-each`** is for a
Python dict.

> **Q:** Generator expression `(f(x) for x in seq)`?
>
> **A:** **`each`** builds a generator. The layout body is the value yielded for
> each item:
>
> ```koskari
> each [x (range 0 6)]:
>   (* x x)
> ```
>
> When you need to nest the generator inside another call, use the parenthesized
> spelling:
>
> ```koskari
> print (list (each [x (range 0 6)] (* x x)))
> ```

> **Q:** Generator expression with an `if` clause?
>
> **A:** Put `when:` or `unless:` on the `each` header:
>
> ```koskari
> each [x (range 0 10)] when: (equal? 0 (% x 2)):
>   x
> ```
>
> Read that as “for each `x`, when this condition is true, yield the body.” The
> `unless:` variant is delightful for “skip the dull cases”:
>
> ```koskari
> each [name names] unless: (name.startswith "_"):
>   (name.upper)
> ```
>
> The same filter works in s-expression form when the whole generator is an
> argument:
>
> ```koskari
> (list (each [x (range 0 10)] when: (equal? 0 (% x 2)) x))
> ```

> **Q:** List comprehension `[f(x) for x in seq]`?
>
> **A:** **`list-each`** produces a Python list immediately:
>
> ```koskari
> list-each [x (range 0 6)]:
>   (* x x)
> ```
>
> If you are building an API response, a test fixture, or any value you plan to
> index more than once, reach for `list-each` instead of `each`.
>
> Nested as an argument:
>
> ```koskari
> print (list-each [word words] (word.title))
> ```

> **Q:** Plain `for item in seq: ...` with side effects?
>
> **A:** Use **`for-each`**. It is a loop, not a comprehension, so its body is for
> doing work: printing, appending, calling methods, breaking, and continuing.
>
> ```koskari
> for-each [path paths]:
>   print "checking" path
>   unless (path.exists):
>     continue
>   (upload path)
> ```
>
> Parenthesized `for-each` is useful inside macro output or compact examples:
>
> ```koskari
> (for-each [path paths]
>   (print "checking" path)
>   (upload path))
> ```

> **Q:** What about Python `for` / `else`?
>
> **A:** `for-each` has an `else:` peer, like Python. The peer runs when the
> iterable is exhausted naturally, not when `break` exits the loop:
>
> ```koskari
> define find-user [users email]:
>   for-each [user users]:
>     when (equal? user.email email):
>       break user
>   else:
>     None
> ```

> **Q:** Dict comprehension?
>
> **A:** **`dict-each`** with `key:` and `value:` clauses — see
> [compile/dict-each](../../reference/macros/compile-dict-each.md) when you need
> exact shapes.
>
> ```koskari
> dict-each [name names] key: name value: (len name)
> ```

> **Q:** Generator over a Cartesian product (like nested `for` loops)?
>
> **A:** Combine **`each`** with a Python iterable, for example
> `itertools.product`:
>
> ```koskari
> (import-from itertools product)
>
> each [[x y] (product (range 0 w) (range 0 h))]:
>   #(x y)
> ```
>
> Destructuring in the binding (`[x y]`) is the Koskari version of Python’s
> `for x, y in ...`.

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

To ship a command users install with pip, see
[Packaging an installed command](../../patterns/recipes/packaging-installed-scripts.md).

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
