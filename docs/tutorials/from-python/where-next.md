# Where next

You have a Rosetta map, layout-first syntax, tooling parity with Python, data
and control, functions, classes, programs, errors, and interop basics.

---

## Patterns and examples

> **Q:** Where are idiomatic full programs?
>
> **A:** Start with the [Patterns](examples/index.md) cookbook:
>
> - [Fibonacci](examples/fibonacci.md) — recursion and function calls
> - [Point class](examples/point-class.md) — data and methods
> - [Script entry point](examples/script-main.md) — `when-main`
> - [Packaging an installed command](../../guide/packaging.md) — pip-installable CLI
> - [Hello world](../../quickstart.md) — the smallest runnable script

Copy a pattern into a `.ki` file, run it, and change one line at a time.

---

## Shared guide

> **Q:** What should I read next in the guide?
>
> **A:** [Model of evaluation](../../guide/model-of-evaluation.md) is the
> first shared chapter. Continue through forms, bindings, functions, data,
> control flow, modules, macros, types, effects, and testing as needed.

> **Q:** Which reference sections matter day to day?
>
> **A:**
>
> - [Layouts](../../reference/layouts/index.md) — block syntax you write in files
> - [Special forms](../../reference/specials/index.md) — compiler forms
> - [Macros](../../reference/macros/index.md) — `define`, `set!`, `when-main`, …
> - [Glossary](../../reference/glossary.md)

---

## Macros and syntax extension

> **Q:** When do I learn `define-syntax`?
>
> **A:** When you repeat a code pattern that cannot be a function. Start with
> [Macros or syntax extension](../../guide/macros-or-syntax-extension.md),
> then use the [macro reference](../../reference/macros/index.md) for exact
> syntax.

---

## Embedding in Python apps

> **Q:** Building a long-lived host for Koskari?
>
> **A:** Finish [Interop](interop.md), then follow
> [Hosting Koskari in a Python app](../../guide/hosting-from-python.md) —
> the import hook, running a file into a namespace, the REPL wrappers, and
> awaiting Koskari functions from Python.

---

## Other on-ramps

> **Q:** I never learned Python — is there another track?
>
> **A:** [Little Koskari](../beginner/index.md) teaches the same language from
> first principles. Both paths share [Model of evaluation](../../guide/model-of-evaluation.md).

---

## Remember

This track is meant to get you **writing `.ki` code** with Python semantics
under the hood. Use patterns for copy-paste starting points; use reference for one-form lookup;
use the API only when you embed.
