# Map

This chapter is the Rosetta stone: what transfers cleanly from Python, what
looks similar but behaves differently, and what is genuinely new.

---

## Same runtime, different surface

> **Q:** Is Koskari a new Python implementation?
>
> **A:** No. Koskari runs on **CPython**. Numbers, strings, many builtins, and
> imported Python modules are ordinary Python objects. What changes is mostly
> **how you write programs**: parenthesized forms, macros, and optional layout
> blocks instead of indentation-driven statement syntax.

> **Q:** So `(+ 1 2)` is just calling `+`?
>
> **A:** Yes. The first word after `(` is the callee; the rest are arguments.
> That is Koskari’s default call rule — like `f(x, y)` with the function name
> first inside one pair of parentheses.

---

## Transfers cleanly

> **Q:** What Python knowledge carries over immediately?
>
> **A:**
>
> | Python habit | Koskari shape |
> |--------------|---------------|
> | `42`, `"hi"` | `42`, `"hi"` |
> | `import os` | `(import os)` |
> | `from os import path` | `(import-from os path)` |
> | `obj.attr` | `(attr obj attr)` |
> | `obj.method(a, b)` | `(call-attr obj method a b)` |
> | `if test: a else: b` | `(if test a b)` |
> | `while test: body` | `(while test body ...)` |
> | `for x in seq: body` | `(for-each [x seq] body ...)` |
> | `try` / `except` / `finally` | `(try body [except-clauses] [finally: ...])` |

> **Q:** Can I use Python objects inside Koskari calls?
>
> **A:** Yes. Import them, bind names, pass them as arguments. Interop is the
> default path, not an escape hatch.

---

## Looks similar, mind the gap

> **Q:** What trips up Python readers first?
>
> **A:**
>
> - **Calls are always parenthesized.** `print "hi"` is not valid; use
>   `(print "hi")`.
> - **`define` vs `setq`.** `(define x 6)` introduces a binding (like first
>   assignment at module scope). `(setq x 7)` updates an existing name (like
>   reassignment). `(set! x 7)` is the macro spelling most people write.
> - **`None` vs `nil`.** `None` is Python’s null. `nil` ends Koskari list
>   structure. `(None? x)` and `(nil? x)` mean different things.
> - **Truth tests follow Python truthiness** in `if`, `cond`, and `while` — but
>   the **syntax** is expression-oriented, not statement-oriented.
> - **Results at the REPL.** Useful values print; `(define x 99)` stays quiet,
>   like running an assignment in Python without `print`.

> **Q:** Is `[a b]` a list?
>
> **A:** In call positions it is usually **formals** — parameter names for
> `define` / `lambda`. Literal collections use reader shorthands such as
> `#[1 2 3]` (Python list) and `#{key val}` (dict). Details in the next
> chapter.

---

## Genuinely new (defer depth)

> **Q:** What should I not expect to paste from Python verbatim?
>
> **A:**
>
> - **Layout blocks** — indent syntax for `if`, `while`, `class`, and more
>   expands to s-expressions at read time. Optional sugar; the underlying forms
>   still matter.
> - **Macros** — `define-syntax` rewrites code before evaluation. Python has
>   no direct analogue.
> - **Pairs and `cons`** — Koskari lists are often built with `cons`, not only
>   with `#[...]`.
> - **Continuations, delimited control, dynamic wind** — present in the
>   language; not needed for migration day one.

> **Q:** Where is the full catalog?
>
> **A:** [Special forms](../../reference/specials/index.md), [macros](../../reference/macros/index.md), and [layouts](../../reference/layouts/index.md). This track gives the mental model; reference gives exact rules.

---

## How to use this track

> **Q:** Read in order?
>
> **A:** Yes, starting here. Skim what you already know; slow down on
> **functions**, **errors**, and **interop** if you will embed Koskari or mix
> `.ki` modules into a Python project.

> **Q:** What about the shared guide?
>
> **A:** Both on-ramps meet at [Model of evaluation](../../guide/00-model-of-evaluation.md). Read it after this track or in parallel once calling and `define` feel familiar.

---

## What you should remember

Koskari is Python-hosted Lisp-ish syntax: same objects, different grammar.
Most daily Python maps to a small set of forms. Watch `define`/`setq`, `nil`/`None`,
and parenthesized calls.

Next: [Runtime and tooling](runtime-and-tooling.md).
