# Where next

You have a Rosetta map, tooling parity with Python, data and control forms,
functions, errors, and interop basics.

---

## Into the shared guide

> **Q:** What should I read next?
>
> **A:** [Model of evaluation](../../guide/00-model-of-evaluation.md) — short,
> names read/eval/data explicitly. You likely already live this model; the
> guide makes it precise for both on-ramps.

> **Q:** Which guide chapters matter most for Python refugees?
>
> **A:** Skim in this order when you need depth:
>
> 1. [Forms and combining](../../guide/01-forms-and-combining.md) — nesting,
>    grouping, how layout expands.
> 2. [Bindings and environments](../../guide/02-bindings-and-environments.md)
>    — shadowing, persistence.
> 3. [Modules and program structure](../../guide/06-modules-and-program-structure.md)
>    — `.ki` projects aligned with import rules.
> 4. [Macros or syntax extension](../../guide/07-macros-or-syntax-extension.md)
>    — when you outgrow built-in forms.
>
> Stubs without content yet — check back or use reference meanwhile.

---

## Reference vs tutorial

> **Q:** When do I stop reading tutorials?
>
> **A:** When you are **looking up one form** rather than learning order.
> Then use:
>
> - [Special forms](../../reference/specials/index.md)
> - [Macros](../../reference/macros/index.md)
> - [Layouts](../../reference/layouts/index.md)
> - [Glossary](../../reference/glossary.md)

> **Q:** I never learned Lisp — should I read the beginner track?
>
> **A:** Optional. Chapters 2–3 (calling, REPL results) are the useful overlap
> if parentheses still feel alien.

---

## Patterns and corpus

> **Q:** Where are idiomatic examples?
>
> **A:** [Patterns](../../patterns/index.md) and recipes (e.g.
> [Hello world](../../patterns/recipes/hello.md)). The [corpus](../../corpus/index.md)
> collects real programs as documentation matures.

---

## If you are embedding

> **Q:** Building a Python app that hosts Koskari?
>
> **A:** Finish [Interop](interop.md), then read
> [Python API reference](../../api/index.md) (`koskari-importer`,
> `koskari-module`, `koskari-asyncio`). Run the importer and CLI tests under
> `tests/importer/` and `tests/cli/` for executable examples.

---

## What you should remember

This track gets you productive in Koskari syntax with Python semantics under
the hood. The guide adds depth; reference adds precision. Same evaluation
story as the beginner path — pick the on-ramp that matched your background,
then converge on the guide.
