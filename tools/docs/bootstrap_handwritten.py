#!/usr/bin/env python3

# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
tools.docs.bootstrap_handwritten

Write hand-authored and stub documentation pages for the Koskari doc site

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / 'docs'


def write(rel_path, content):
    path = DOCS / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.strip() + '\n', encoding='utf-8')


def stub(title, note, links=''):
    links_block = f'\n\n{links}' if links else ''
    return f'''# {title}

```{{admonition}} Under construction
:class: warning

{note}{links_block}
```
'''


def main():
    write('index.md', '''
# Koskari documentation

Koskari is a LISP dialect for Python: an extensible language with indent-based
layout syntax, macros, and a custom bytecode evaluator.

This site is the **user-facing** documentation. Maintainer design notes live
in the repository under `design/` and are linked from contributor pages only.

```{admonition} Pre-alpha
:class: note

Koskari is under active development. Behaviour and documentation may change
between releases. Pages marked with stability notes call out evolving areas.
```

## Getting started

```{toctree}
:maxdepth: 2

start/overview
start/install-run
faq
```

## Tutorials

```{toctree}
:maxdepth: 2

start/beginner/index
start/from-python/index
guide/index
```

## Reference

```{toctree}
:maxdepth: 2

reference/glossary
reference/cli-and-config
reference/specials/index
reference/layouts/index
reference/macros/index
reference/vm/index
reference/grammar
reference/error-codes
```

## Patterns and API

```{toctree}
:maxdepth: 2

patterns/index
api/index
```

## Project and corpus

```{toctree}
:maxdepth: 2

project/build-test
project/architecture-map
corpus/index
changelog
```
''')

    write('start/overview.md', '''
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
| [Bytecode](../reference/vm/index.md) | Opcode reference and disassembly |
| [API](../api/index.md) | Python modules (`koskari.dis`, ...) |

## Stability

Reference material adapted from implementer docs is marked **evolving** unless
a page states otherwise. For the implementation contract, see `design/` in
the repository.
''')

    write('start/install-run.md', '''
# Install and run

## Prerequisites

- Python 3.9 or newer (CPython)
- A C compiler for building native extensions
- [tox](https://tox.wiki/) for running tests and building docs (recommended)

## Build

From the repository root:

```bash
make build
```

This builds native extensions and produces a wheel under `dist/`.

## Install

```bash
make install
```

Or for development:

```bash
pip install -e ".[prompt_toolkit]"
```

The optional `prompt_toolkit` extra enables syntax-highlighted REPL input.

## Verify

Run the test suite:

```bash
make test
```

## First session

Start the REPL:

```bash
koskari
```

Or run a script:

```bash
koskari examples/hello.ki
```

Minimal example (`examples/hello.ki`):

```scheme
(print "Hello, Koskari!")
```

## Compile to bytecode file

```bash
koskari --compile-kio out.kio examples/hello.ki
koskari --execute-kio out.kio
```

See [CLI and configuration](../reference/cli-and-config.md) for all flags.

## Next steps

- [Special forms reference](../reference/specials/index.md)
- [Layout syntax](../reference/layouts/index.md)
- [Hello world recipe](../patterns/recipes/hello.md)
''')

    write('start/beginner/index.md', stub(
        'Beginner track',
        'Tutorial pages for readers new to programming are planned but not '
        'written yet.',
        'Start with [Install and run](../install-run.md) and the '
        '[Glossary](../../reference/glossary.md) instead.'))

    beginner_pages = [
        ('00-hello-change', 'Run a tiny program and change a literal'),
        ('01-words-and-punctuation', 'Names, numbers, delimiters'),
        ('02-calling-things', 'Calling functions and forms'),
        ('03-results-and-messages', 'Output and error messages'),
        ('04-repeating-patterns', 'Nesting and simple repetition'),
        ('where-next', 'Bridge to the shared guide'),
    ]
    for slug, desc in beginner_pages:
        write(f'start/beginner/{slug}.md', stub(
            slug.replace('-', ' ').title(),
            f'Planned beginner lesson: {desc}.'))

    write('start/from-python/index.md', stub(
        'From Python track',
        'Rosetta-style pages for Python developers are planned but not '
        'written yet.',
        'See [Special forms](../../reference/specials/index.md) and '
        '[Layouts](../../reference/layouts/index.md) for language shape.'))

    from_python_pages = [
        ('map', 'Python to Koskari mapping overview'),
        ('runtime-and-tooling', 'Running Koskari vs python'),
        ('data-and-control', 'Data types and control flow differences'),
        ('functions-and-scope', 'Functions, closures, scope'),
        ('errors-and-debugging', 'Exceptions and debugging'),
        ('interop', 'Python interop'),
        ('where-next', 'Merge into shared guide'),
    ]
    for slug, desc in from_python_pages:
        write(f'start/from-python/{slug}.md', stub(
            slug.replace('-', ' ').title(),
            f'Planned migration page: {desc}.'))

    write('guide/index.md', stub(
        'Guide',
        'The shared concept spine (evaluation model, bindings, modules, ...) '
        'is not written yet.',
        'Use [Special forms](../reference/specials/index.md) and '
        '[Macros](../reference/macros/index.md) meanwhile.'))

    guide_pages = [
        ('00-model-of-evaluation', 'Read vs eval vs data'),
        ('01-forms-and-combining', 'Program structure'),
        ('02-bindings-and-environments', 'Names and scope'),
        ('03-functions-deep', 'Functions and recursion'),
        ('04-data-structures', 'Pairs, lists, traversal'),
        ('05-control-flow', 'Conditionals and iteration'),
        ('06-modules-and-program-structure', 'Files and imports'),
        ('07-macros-or-syntax-extension', 'Macros at user level'),
        ('08-types-or-contracts', 'Types and contracts'),
        ('09-io-and-effects', 'I/O and effects'),
        ('10-testing-and-quality', 'Testing with tox'),
        ('bytecode-mental-model', 'Optional bytecode intuition'),
        ('from-source-to-stack', 'Optional source-to-bytecode trace'),
    ]
    for slug, desc in guide_pages:
        write(f'guide/{slug}.md', stub(
            slug.replace('-', ' ').title(),
            f'Planned guide chapter: {desc}.'))

    write('project/build-test.md', '''
# Build and test

## Make targets

Run `make help` for the full list. Common targets:

| Target | Purpose |
|--------|---------|
| `make build` | Build wheel (native ext + uv) |
| `make test` | Run tox test suite (phased pytest) |
| `make tox` | Full tox envlist |
| `make lint` | flake8 on `koskari/` |
| `make typecheck` | mypy on `koskari/` |
| `make docs` | Build this documentation site |
| `make clean` | Remove build artifacts |

## Tests via tox

All Python tests run through tox:

```bash
tox -qe test
```

Run a single file:

```bash
tox -qe py -- tests/reader/test_layout.py
```

Tests are ordered by **phase markers** (lib, infra, compile, specials,
importer, basics, cli, reproducers). See `setup.cfg` `[tool:pytest]`.

## Native memory tests

Containerized C harness tests (optional):

```bash
make test-native
make test-asan
make test-valgrind
```

## Documentation build

```bash
make docs
```

Output: `build/docs/` (dirhtml).
''')

    write('project/architecture-map.md', '''
# Architecture map

High-level pipeline from source text to execution:

```mermaid
flowchart LR
  source[Source text] --> reader[Reader and layouts]
  reader --> expand[Macro expansion]
  expand --> compile[Compiler specials]
  compile --> bytecode[KosCode bytecode]
  bytecode --> eval[Evaluator VM]
```

## Python package layout

| Area | Package path | Role |
|------|--------------|------|
| Reader | `koskari.reader` | S-expressions, layout blocks |
| Compiler | `koskari.compile` | Special forms, bytecode emission |
| Syntax | `koskari.syntax` | Syntax-rule transformers |
| Matcher | `koskari.matcher` | Pattern matching for specials |
| VM | `koskari.lib` | Native types, bytecode, evaluator |
| CLI | `koskari.cli` | REPL and script runner |
| Builtins | `koskari.builtins` | Standard library (virtual module) |

Bootstrap `.ki` sources compile into `koskari.builtins`; that loading
machinery is internal and not part of this user documentation.

## Contributor design docs

Implementation contracts and in-progress design live under `design/` in the
repository. Start at `design/README.md`.
''')

    write('project/vm-and-opcodes.md', '''
# VM and opcodes (contributors)

When adding or changing an opcode:

1. Add `OP(NAME, STYLE)` to `koskari/lib/opcodes.incl` (order matters)
2. Regenerate handlers: `make handlers` or `tox -e handlers`
3. Implement the handler under `koskari/lib/opcode/`
4. Document semantics in `design/OPCODES.md`
5. Add or update the user page under `docs/reference/vm/opcodes/`
6. Add tests under `tests/lib/opcode/` or relevant compile tests

User-facing opcode pages are one file per opcode for git-friendly updates.
Regenerate the initial tree with `tox -e regen-docs-ref` only when
re-bootstrapping from design docs; routine edits are per-file.

See [Opcode overview](../reference/vm/index.md) and
[Opcode families](../reference/vm/families.md).
''')

    write('project/release-and-versioning.md', stub(
        'Release and versioning',
        'Release process and compatibility policy are not documented yet.',
        'Version is currently defined in `setup.cfg` metadata.'))

    write('reference/index.md', '''
# Reference

```{toctree}
:maxdepth: 2

glossary
cli-and-config
specials/index
layouts/index
macros/index
vm/index
grammar
error-codes
```
''')

    write('reference/cli-and-config.md', '''
# CLI and configuration

The `koskari` command runs scripts and the REPL.

## Usage

```
koskari [options] [filename] [args ...]
```

## Options

| Flag | Description |
|------|-------------|
| `--no-tweak-path` | Do not add the current directory to `sys.path` |
| `--histfile PATH` | REPL readline/prompt_toolkit history file |
| `--repl-full-trace` | Show REPL infrastructure frames in error tracebacks |
| `-i`, `--interactive` | Enter REPL after running a script |
| `--compile-kio OUT` | Compile `.ki` to `.kio` at OUT, then exit |
| `--compile-kio-source-name PATH` | Logical source path stored in `.kio` |
| `--execute-kio` | Run filename as precompiled `.kio` |

Positional `filename` runs as a script. Remaining arguments are forwarded to
the script or REPL session.

## Related commands

| Command | Purpose |
|---------|---------|
| `koskari-lsp` | Language server (LSP) |
| `python -m koskari` | Same as `koskari` |

## Environment

Test and debug environment variables are documented in
[Build and test](../project/build-test.md) (`KOS_TRACE_*`, `DEBUG`, etc.).
''')

    write('reference/glossary.md', '''
# Glossary

Shared terms used across tutorials and reference pages.

**form**
: A Koskari expression: atoms, lists, or layout-derived s-expressions.

**special form**
: A form whose head is a compiler symbol such as `compile/call` or `compile/lambda`.
  Specials are implemented in Python under `koskari.compile.specials`.

**macro**
: A compile-time transformer defined with `define-syntax`. Expands to ordinary
  forms before compilation.

**layout**
: A read-time handler for indent-based block syntax (`layout/if`, `layout/block`, ...).

**namespace**
: Lexical environment mapping names to reference cells.

**pair**
: A cons cell (`car` / `cdr`); lists are proper chains of pairs terminated by `nil`.

**bytecode**
: `KosCode` instruction sequence executed by the Koskari VM.

**opcode**
: One VM instruction (see [Opcode reference](vm/opcodes/index.md)).

**REPL**
: Read-eval-print loop started by `koskari` with no script argument.
''')

    write('reference/grammar.md', stub(
        'Grammar',
        'A public syntax reference will be adapted from design/SYNTAX.md.',
        'See [Layouts](layouts/index.md) and [Special forms](specials/index.md) '
        'for implemented surface syntax today.'))

    write('reference/error-codes.md', stub(
        'Error codes',
        'Stable public error identifiers are not catalogued yet.',
        'See `koskari.exceptions` in the [API reference](../api/index.md).'))

    write('patterns/index.md', '''
# Patterns (cookbook)

Task-oriented examples with prerequisites and links back to reference material.

```{toctree}
:maxdepth: 1

recipes/hello
```

More recipes will be added as the tutorial tracks mature.
''')

    write('patterns/recipes/hello.md', '''
# Hello world

Run a minimal Koskari program.

## Prerequisites

- [Install and run](../../start/install-run.md)

## Steps

Create `hello.ki`:

```scheme
(print "Hello, Koskari!")
```

Run:

```bash
koskari hello.ki
```

## Variants

Layout style:

```scheme
print:
  "Hello from a block!"
```

## See also

- [Special forms: compile/call](../../reference/specials/compile-call.md)
- [Layout: layout/call](../../reference/layouts/layout-call.md)
''')

    write('faq.md', '''
# FAQ

## Is Koskari stable?

No. Development status is **Pre-Alpha**. Expect breaking changes.

## Which Python versions are supported?

Python 3.9 through 3.14 (see `setup.cfg` classifiers). CI exercises multiple
versions via containerized tox runs.

## Where are the design documents?

In the repository under `design/`. They target maintainers and agents, not
first-time learners. This user site links to them from contributor pages only.

## How do I report bugs or contribute?

Use the project issue tracker and follow `make test` / `make lint` before
submitting changes.

## Why does `koskari.builtins` look odd in API docs?

`koskari.builtins` is a virtual module assembled at import time from compiled
bootstrap sources. The API reference documents its exported names; the bootstrap
loader itself is intentionally not documented here.
''')

    write('changelog.md', stub(
        'Changelog',
        'Release notes will be published here when versioning stabilizes.',
        'Version is currently `0.1.0` in `setup.cfg`.'))

    write('corpus/index.md', '''
# Language conformance corpus

The corpus is the **source of truth** for canonical examples and verified
language behaviour. User docs, tests, and published ML datasets should derive
from corpus records rather than parallel example sets.

```{toctree}
:maxdepth: 1

README
SCHEMA
datasets
```

See [design/corpus-schema-philosophy.md](../../design/corpus-schema-philosophy.md)
for the full philosophy.
''')

    write('corpus/README.md', '''
# Corpus guide

## Purpose

The Koskari corpus describes language behaviour at subsystem boundaries:

- read (reader and layouts)
- expand (macros)
- compile (specials)
- eval (runtime)

Records are **not** shaped like ML training rows at source. Hugging Face and
similar exports are downstream transforms.

## Adding records

1. Define a record matching [SCHEMA.json](SCHEMA.json)
2. Place it under `records/` (format TBD: JSONL or directory tree)
3. Run corpus verification (runner not implemented yet)

## Status

Schema documentation exists; records and automated verification are **not
started**. Tests under `tests/` currently serve as the implicit conformance
corpus.
''')

    write('corpus/SCHEMA.md', '''
# Corpus record schema

Corpus records describe language behaviour at phase boundaries. JSON Schema
validates **structure only**; semantics are checked by corpus test runners.

## Recommended record

```json
{
  "id": "layout-if-basic-001",
  "feature": "layout/if",
  "source": "if x:\\n  y",
  "verify": {
    "read": "(compile/cond (x y))"
  },
  "references": [
    "reference/layouts/layout-if.md"
  ]
}
```

## Fields

**id**
: Unique record identifier.

**feature**
: Feature path (e.g. `layout/if`, `special/compile/cond`).

**source**
: Koskari source text.

**verify**
: Expected results at one or more phases: `read`, `expand`, `compile`, `eval`,
  or `error`.

**references**
: Links to user docs or design anchors.

**stability** (optional)
: `stable`, `evolving`, or `experimental`.

**tags**, **notes** (optional)
: Maintainer metadata; not copied verbatim into training exports.

See [SCHEMA.json](SCHEMA.json) for machine validation.
''')

    write('corpus/SCHEMA.json', '''{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://koskari.dev/corpus/schema.json",
  "title": "Koskari corpus record",
  "type": "object",
  "required": ["id", "feature", "source", "verify"],
  "additionalProperties": false,
  "properties": {
    "id": {
      "type": "string",
      "pattern": "^[a-z0-9][a-z0-9._-]*$"
    },
    "feature": {
      "type": "string",
      "minLength": 1
    },
    "source": {
      "type": "string"
    },
    "verify": {
      "type": "object",
      "minProperties": 1,
      "additionalProperties": {
        "type": "string"
      },
      "properties": {
        "read": { "type": "string" },
        "expand": { "type": "string" },
        "compile": { "type": "string" },
        "eval": { "type": "string" },
        "error": { "type": "string" }
      }
    },
    "references": {
      "type": "array",
      "items": { "type": "string" }
    },
    "stability": {
      "type": "string",
      "enum": ["stable", "evolving", "experimental"]
    },
    "tags": {
      "type": "array",
      "items": { "type": "string" }
    },
    "notes": {
      "type": "string"
    }
  }
}
''')

    write('corpus/datasets.md', '''
# Published datasets

Published ML datasets are **views** generated from corpus records, not the
authoritative schema.

```text
Corpus JSONL
      |
      v
 Transformation
      |
      v
 Parquet (optional)
      |
      v
 Hugging Face Dataset
```

## Status

No dataset has been published yet. Transform tooling and dataset cards are
not implemented.

Each exported row should carry provenance (`corpus_id`, schema version).
''')

    write('corpus/records/.gitkeep', '')

    print('Wrote handwritten and stub documentation pages')


if __name__ == '__main__':
    main()


# The end.
