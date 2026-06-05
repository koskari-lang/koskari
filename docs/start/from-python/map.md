# Map

This chapter is the Rosetta stone: what transfers cleanly from Python, what
looks similar but behaves differently, and what is genuinely new. Later
chapters teach **layout-first** syntax in depth.

---

## How you will write Koskari

If you are coming from Python, plan on **indent blocks with colons** for
definitions, conditionals, loops, classes, and try/except. The reader expands
those blocks into forms the compiler runs; you can peek at that expansion when
debugging, but day-to-day authoring matches Python’s “header + body” rhythm more
than it matches “everything in parentheses.”

Parenthesized calls still appear for nested math, imports, and short one-liners.
The [Layout and calls](layout-and-calls.md) chapter is the hands-on introduction.

---

## Same runtime, different surface

> **Q:** Is Koskari a new Python implementation?
>
> **A:** No. Koskari runs on **CPython**. Numbers, strings, many builtins, and
> imported Python modules are ordinary Python objects. What changes is mostly
> **how you write programs**: layout blocks, macros, and s-expression forms where
> nesting is clearer in parentheses.

> **Q:** So `(+ 1 2)` is just calling `+`?
>
> **A:** Yes. The first word after `(` is the callee; the rest are arguments.
> That is Koskari’s default call rule — like `f(x, y)` with the function name
> first inside one pair of parentheses. At top level you will often write
> `print:` blocks instead; see the layout chapter.

---

## Transfers cleanly

> **Q:** What Python knowledge carries over immediately?
>
> **A:**
>
> | Python habit | Koskari (layout-first) | Koskari (s-expression) |
> |--------------|------------------------|-------------------------|
> | `42`, `"hi"` | same literals | same |
> | `import os` | top-level `(import os)` | `(import os)` |
> | `from os import path` | `(import-from os path)` | same |
> | `obj.attr` | `obj.attr` in many contexts | `(attr obj attr)` |
> | `obj.method(a, b)` | `(obj.method a b)` | `(call-attr obj method a b)` |
> | `if` / `elif` / `else` | `if:` / `elif:` / `else:` peers | `(if test a b)` or `cond` |
> | `while` / `for` | `while:` / `for-each [x seq]:` | `(while …)` / `(for-each …)` |
> | `try` / `except` | `try:` / `except [Type]:` peers | `(try …)` |
> | `def f(x): …` | `define f [x]: …` | `(define f [x] …)` |
> | `class C:` | `define-class C []: …` | see [Classes](classes-and-objects.md) |

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
> - **Not all calls are bare names.** `print "hi"` is invalid; use a `print:`
>   block or `(print "hi")`.
> - **`define` vs `setq`.** `define` introduces a binding (like first assignment
>   at module scope). `set!` updates an existing name (like reassignment).
> - **`None` vs `nil`.** `None` is Python’s null. `nil` ends Koskari list
>   structure. `(None? x)` and `(nil? x)` mean different things.
> - **Truth tests follow Python truthiness** in `if`, `cond`, and `while`.
> - **Results at the REPL.** Useful values print; defining a name stays quiet,
>   like assignment without `print` in Python.

> **Q:** Is `[a b]` a list?
>
> **A:** In parameter positions it is **formals** — names for `define` /
> `lambda`. Literal collections use reader shorthands such as `#[1 2 3]` (Python
> list) and `#{key val}` (dict). Details in [Data and control](data-and-control.md).

---

## Genuinely new (covered later in this track)

> **Q:** What should I not expect to paste from Python verbatim?
>
> **A:**
>
> - **Layout peers** — `elif`, loop `else:`, `except` blocks have Koskari-specific
>   header rules; see [Layouts](../../reference/layouts/index.md).
> - **Macros** — `define-syntax` rewrites code before evaluation. Covered when
>   you need syntax extension, not on day one.
> - **Pairs and `cons`** — list structure for macros and advanced code; `#[…]`
>   covers most Python-list needs.
> - **Continuations and dynamic wind** — present in the language; skip until you
>   need them.

> **Q:** Where is the full catalog?
>
> **A:** [Special forms](../../reference/specials/index.md), [macros](../../reference/macros/index.md), and [layouts](../../reference/layouts/index.md). This track teaches usage; reference gives exact rules.

---

## How to use this track

> **Q:** Read in order?
>
> **A:** Yes, starting here, then [Layout and calls](layout-and-calls.md). Skim
> what you already know; slow down on **classes**, **programs**, **errors**, and
> **interop** if you embed Koskari in a Python app.

> **Q:** What about the shared guide?
>
> **A:** [Model of evaluation](../../guide/00-model-of-evaluation.md) names read /
> eval / data explicitly. The shared guide now continues into forms, bindings,
> functions, data, control flow, modules, macros, effects, and testing; this
> track remains the fastest Rosetta-style route for Python readers.

---

## Try it

From the repository root (after [Install and run](../install-run.md)):

```bash
koskari examples/hello.ki
```

You should see greeting lines and attribute access on a small class. Open the
file alongside the output.

---

## Remember

Koskari is Python-hosted with a layout-friendly surface: same objects, different
grammar. Most daily Python maps to a small set of forms. Watch `define`/`set!`,
`nil`/`None`, and use layout blocks for structure.

Next: [Layout and calls](layout-and-calls.md).
