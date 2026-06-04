# Where next

You have a Rosetta map, layout-first syntax, tooling parity with Python, data
and control, functions, classes, programs, errors, and interop basics.

---

## Patterns and examples

> **Q:** Where are idiomatic full programs?
>
> **A:** The repository **`examples/`** tree and the [Patterns](../../patterns/index.md)
> cookbook:
>
> - [Fibonacci](../../patterns/recipes/fibonacci.md) — `examples/fib.ki`
> - [Point class](../../patterns/recipes/point-class.md) — `examples/dataclass.ki`
> - [Script entry point](../../patterns/recipes/script-main.md) — `when-main`
> - [Hello world](../../patterns/recipes/hello.md)

Run any example with `koskari path/to/file.ki` after install.

---

## Shared guide

> **Q:** What should I read next in the guide?
>
> **A:** [Model of evaluation](../../guide/00-model-of-evaluation.md) is complete
> and short. Later guide chapters (forms, bindings, modules, macros) are still
> **under construction** — use this from-python track and reference pages until
> those land.

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
> [examples/ext-syn.ki](../../../examples/ext-syn.ki) and
> [Macros or syntax extension](../../guide/07-macros-or-syntax-extension.md)
> (stub — reference macros are authoritative for now).

---

## Embedding in Python apps

> **Q:** Building a long-lived host for Koskari?
>
> **A:** Finish [Interop](interop.md), then
> [Python API reference](../../api/index.md) (`koskari-importer`,
> `koskari-module`, `koskari-asyncio`). Advanced example:
> [examples/fastapi/fastapi.ki](../../../examples/fastapi/fastapi.ki).

---

## Other on-ramps

> **Q:** I never learned Python — is there another track?
>
> **A:** [Beginner track](../beginner/index.md) teaches the same language from
> first principles. Both paths share [Model of evaluation](../../guide/00-model-of-evaluation.md).

---

## Remember

This track is meant to get you **writing `.ki` code** with Python semantics
under the hood. Use `examples/` and patterns for copy-paste starting points;
use reference for one-form lookup; use the API only when you embed.
