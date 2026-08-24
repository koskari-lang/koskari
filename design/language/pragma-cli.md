# Koskari Design: CLI Pragma

## Status

Proposed. The pragma system ([../PRAGMA.md](../PRAGMA.md)) is implemented for
module-declared pragmas. This adds a **CLI-supplied** pragma that pre-loads the
default for the module the CLI runs (a script's main module, or the REPL
module), so a run can select feature settings without editing source.

## Goal

Let a user seed a module's pragma from outside the source — command-line flags,
a settings file, or both — for the module the CLI entry point loads. The
module's own `(pragma …)` forms still override, forward-only, on top of that
seed. Imported modules are unaffected.

## Where it fits

The pragma **precedence chain** (see [../PRAGMA.md](../PRAGMA.md) § Sources and
Precedence), lowest to highest:

1. Built-in defaults (the registry baselines).
2. Package pragma (inherited — see [pragma-package.md](../archive/pragma-package.md)).
3. **CLI / REPL session default** ← *this document*.
4. The module's own `(pragma …)` declaration.

The CLI pragma is a **session default**, not something that travels with code.
It applies only to the **entry module** the CLI loads and the **REPL module** —
never to modules imported by that entry. That preserves the invariant that only
a module's own declaration is portable: a library never behaves differently
because of how a downstream app was launched.

## Specifying the pragma

Two sources, both optional; when both are present, flags win over the file.

### Command-line flags

A repeatable flag naming one setting each, e.g.

```
koskari --pragma compiler/call-brackets=error \
        --pragma compiler/resolution=python \
        --pragma reader/brackets=false \
        script.ki
```

Open questions:

- **Spelling** — `--pragma KEY=VALUE` (repeatable) vs a single
  `--pragma "compiler/call-brackets=error compiler/resolution=python"`, and
  whether a short form (`-P`) is worth it.
- **Value parsing** — values arrive as strings; each is handed to the setting's
  registered function (the same path `rebuild` uses), so `error` / `python` /
  `false` coerce through `BracketCalls(…)` / `ResolutionMode(…)` / bool. Need a
  small string→datum rule (bare word → symbol, `true`/`false` → bool).
- **Bare key** — does `--pragma compiler/call-brackets` (no `=value`) mean the
  setting's parameter default (as bare `[key]` does in source)? Probably yes.

### Settings file

A file that declares pragma once for a project or user. Options to weigh:

- **Reuse Koskari surface** — a `.ki` file containing `(pragma …)` forms,
  evaluated against a bare pragma to produce the seed. Zero new grammar; the
  file is exactly what you'd write in source.
- **Structured config** — a `[pragma]` table in an INI/TOML project file. More
  familiar to non-Koskari tooling, but a second grammar to maintain.

Discovery and precedence (proposed): built-in < user file
(`~/.config/koskari/pragma.ki`) < project file (nearest `koskari-pragma.ki`
walking up from cwd) < CLI flags < module declaration. Keep v1 to **one** file
source if the layering proves fussy.

## Mechanism

The CLI builds a `ModulePragma` from (file then flags) and passes it as the
**active source** to the module-load seam. `build_module_load_facets`
(`koskari/module.py`) already centralizes "the pragma source" — today it is
always `ModulePragma()`; it grows an optional starting pragma:

```
build_module_load_facets(module, filename=None, source_pragma=None)
```

- **Script run** — the CLI resolves the flags/file into `source_pragma` and
  passes it when loading the entry module.
- **REPL** — same seed is installed as the session module's `__pragma__`;
  subsequent interactive `(pragma …)` entries rebuild it as usual (the REPL
  retains its facets, so the watchers stay live across entries).
- **Imports** — go through the normal loader path with `source_pragma=None`, so
  they get their built-in / package-pragma default, not the CLI seed.

## Open questions

- Flag spelling and short form; one file vs. layered files; `.ki` vs. TOML.
- Whether `--pragma` errors (unknown key / bad value) are fatal to the run or
  warn-and-continue (likely reuse `pragma/strict-settings`).
- Whether a run should be able to *see* the effective seed (`koskari
  --show-pragma`) for debugging.

## Non-goals

- Affecting imported/library modules (only entry + REPL).
- A persistent, per-module record — the seed is a session default, discarded
  once the module's own declarations take over.
- Package-wide defaults that travel with a distributed package — that is
  [pragma-package.md](../archive/pragma-package.md).

Ref: [../PRAGMA.md](../PRAGMA.md), [pragma-package.md](../archive/pragma-package.md).
