# Overview

Koskari is the fourth iteration of the Sibilant idea: a language that keeps
s-expression and macro traditions while embracing Python's runtime and
ecosystem.

## What Koskari offers

- **Layout syntax** -- indent-based blocks (`if`, `while`, `class`, ...) read
  into s-expressions at parse time
- **Macros and syntax extension** -- `define-syntax`, quasiquote, hygienic
  expansion
- **Custom bytecode VM** -- coroutines, async generators, continuations,
  dynamic wind, tail calls
- **Python interop** -- runs on CPython; Koskari modules import as `.ki` files

## How this documentation is organized

Two tutorial on-ramps are planned:

- **New to programming** -- under `start/beginner/` (under construction)
- **From Python** -- under `start/from-python/` (under construction)

Both paths converge on the shared **guide** and the **reference** sections.
You can skip straight to reference if you already know Lisp-like languages.

| Section | Purpose |
|---------|---------|
| [Install and run](install-run.md) | Build, install, first REPL session |
| [Special forms](../reference/specials/index.md) | Compiler specials (`compile/...`) |
| [Layouts](../reference/layouts/index.md) | Block syntax handlers |
| [Macros](../reference/macros/index.md) | Bootstrap macros (`if`, `define`, ...) |
| [Runtime](../reference/runtime/index.md) | Bootstrap functions (`help`, `macroexpand`, ...) |
| [Bytecode](../reference/vm/index.md) | Opcode reference and disassembly |
| [API](../api/index.md) | Python modules (`koskari.dis`, ...) |

## Stability

Reference material adapted from implementer docs is marked **evolving** unless
a page states otherwise. For the implementation contract, see `design/` in
the repository.
