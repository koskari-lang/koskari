# Overview

Koskari is the fourth iteration of the Sibilant idea: a language that keeps
s-expression and macro traditions while embracing Python's runtime and
ecosystem.

## What Koskari offers

- **Layout syntax** -- indent-based blocks (`if`, `while`, `define-class`, ...)
  read into forms at parse time
- **Macros and syntax extension** -- `define-syntax`, quasiquote, hygienic
  expansion
- **Custom bytecode VM** -- coroutines, async generators, continuations,
  dynamic wind, tail calls
- **Python interop** -- runs on CPython; Koskari modules import as `.ki` files

## How this documentation is organized

Two tutorial on-ramps — pick the one that fits your background:

- **[From Python](from-python/index.md)** -- if you already know Python, start here.
  Layout-first style, Python↔Koskari side-by-side comparisons, runnable examples.
  This is the recommended path for most readers.
- **[New to programming](beginner/index.md)** -- if you are new to programming,
  start here. Dialogue-style chapters, no assumed background.

Both paths converge at the shared **guide** and **reference** sections.
You can skip straight to reference if you already know Lisp-like languages.

| Section | Purpose |
|---------|---------|
| [Install and run](install-run.md) | Build, install, first REPL session |
| [From Python](from-python/index.md) | Python developer on-ramp |
| [Special forms](../reference/specials/index.md) | Compiler specials (`compile/...`) |
| [Layouts](../reference/layouts/index.md) | Block syntax handlers |
| [Macros](../reference/macros/index.md) | Bootstrap macros (`if`, `define`, ...) |
| [Patterns](../patterns/index.md) | Task-oriented recipes |
| [Runtime](../reference/runtime/index.md) | Bootstrap functions (`help`, `macroexpand`, ...) |
| [API](../api/index.md) | Python modules for embedding |

## Stability

Reference material is marked **evolving** unless a page states otherwise. For
cross-cutting topics (bindings, demand vs async, macros), see
[Concepts](../reference/concepts/index.md) in the language reference.
