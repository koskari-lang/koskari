# <img src="docs/_static/koskari-icon.svg" height="42" align="top" alt=""> Koskari

Koskari is a LISP dialect for Python -- indent-based layout syntax, macros, and a
custom bytecode evaluator that runs on CPython.

```koskari
define greet [name]:
  print (+ "Hello, " name "!")

when-main:
  greet "world"
```

```bash
koskari hello.ki
# Hello, world!
```


## Overview

Koskari is the fourth iteration of the Sibilant idea, learning from the experiences of:
- **spexy**: String-to-string transformer; parsed s-expression text and emitted Python expression strings. The earliest groundwork -- reader and s-expression representation -- carries through to this day.
- **python-sibilant**: CPython bytecode target (abandoned due to version churn)
- **sibilant-ceval**: Custom bytecode evaluator (blocked on call/cc implementation)

This version aims to be a perfect playground -- a language with all the useful features of Python but significantly fewer restrictions, designed to make writing code a joyous expression of will upon the computer.


## Quick Start

**Prerequisites:** Python 3.9+, a C compiler, and [uv](https://docs.astral.sh/uv/) for
the virtual-environment workflow below.

### Virtual environment

From a repository checkout:

```bash
make venv          # creates .venv and installs koskari (editable, with repl extra)
make venv-koslife  # optional: adds the koslife demo
uv run koskari     # interactive REPL
uv run koskari hello.ki
```

### Install

To install into your active Python environment instead:

```bash
pip install ".[repl]"
koskari            # interactive REPL
koskari hello.ki   # run a script
```

The `repl` extra enables syntax-highlighted REPL input via `prompt_toolkit`. Other
optional extras: `lsp`, `pygments`, `sphinx`.

See [docs/start/install-run.md](docs/start/install-run.md) for the full first-session walkthrough.


## Documentation

User documentation lives in [docs/](docs/) and builds into a Sphinx site with
`make docs`:

- **[Getting started](docs/start/overview.md)** -- install, first program, and
  two on-ramps: a [beginner track](docs/start/beginner/index.md) for readers new
  to programming and a [from-Python track](docs/start/from-python/index.md) for
  Python developers.
- **[Guide](docs/guide/index.md)** -- the shared concept spine. Each chapter is a
  quick orientation that points to the authoritative
  [concepts](docs/reference/concepts/index.md) pages for the full rules.
- **[Reference](docs/reference/index.md)** -- special forms, macros, layouts, the
  runtime library, and VM opcodes.
- **[Patterns](docs/patterns/index.md)** -- task-oriented recipes.


## Corpus and MCP server

Koskari maintains a verified example corpus under
[docs/corpus/](docs/corpus/): records carry `read` / `expand` / `eval`
verification phases, so they double as regression tests and as canonical
teaching material. The corpus is published as a Hugging Face dataset,
[obriencj/koskari](https://huggingface.co/datasets/obriencj/koskari).

A container-based **MCP server** exposes the corpus and the documentation to AI
coding assistants (Cursor, Claude Desktop, VS Code, and similar) so they can
search and author idiomatic Koskari. See
[tools/mcp/README.md](tools/mcp/README.md) for build and client setup.


## Project status

Koskari is past early bootstrap. Layout syntax and macros compile to bytecode,
the native evaluator runs programs, `.ki` modules import like Python packages,
and the CLI provides a REPL and script runner. The test suite and
[verified corpus](docs/corpus/) exercise behavior across read, expand, compile,
and eval phases.

| Area | Status |
|------|--------|
| **Language** | Layout blocks, hygienic macros, and a broad special-form surface are implemented — see the [language reference](docs/reference/index.md) |
| **Runtime** | Stackless bytecode VM: coroutines, ambient async, continuations, dynamic wind, tail-call optimization |
| **Tooling** | REPL (`repl` extra), disassembler, optional LSP, Pygments, and Sphinx integrations |
| **Self-hosting** | Core builtins and much of the library live in bootstrap `.ki` sources; CLI port and self-hosted compiler work continue |

The language and APIs are still **evolving** — treat the reference as authoritative,
not frozen. Compiler and opcode inventories for maintainers live in
[design/SPECIALS.md](design/SPECIALS.md) and [design/OPCODES.md](design/OPCODES.md).
Open work is tracked in [design/TODO.md](design/TODO.md); architecture notes are
in [design/](design/).


## Development

```bash
# Lint code
make lint

# Run unit tests
make test

# Run tests across Python versions (containerized)
make test-pyversions

# Clean build artifacts
make clean

# Build user documentation (Sphinx)
make docs
```

Downstream Sphinx projects should enable
`extensions = ['koskari.extras.sphinx']` so autodoc renders `Function`, `Syntax`,
and `LayoutHandler` bindings with signatures and pattern/peer sections.


## Project Structure

The repository root is the Koskari project. Import symbols from the submodule that defines them; the top-level `koskari` package does not re-export.

```
.
├── design/                  # Design documentation (see design/README.md)
├── editors/                 # Editor syntax assets (emacs, helix, textmate)
├── examples/                # Example koskari programs (.ki files)
├── koskari/                 # Main Python package
├── koskari_importer.py      # Import hooks for .ki and .kio modules
├── koskari.pth              # Enables import hooks when installed
├── tests/                   # Test suite (mirrors package layout)
├── tools/                   # Opcode/editor regen, corpus tooling, and the MCP server
├── setup.py, setup.cfg      # Package build and tox configuration
├── Makefile                 # Build, test, and lint targets
└── Containerfile            # Runtime container image
```

### koskari package

```
koskari/
├── __init__.py              # Package init; registers import hooks
├── __main__.py              # python -m koskari entry
├── exceptions.py            # Koskari*, Reader*, Layout*, Compiler* exceptions
├── types.py                 # Value types from lib + Python predicates/helpers
├── syntax.py                # Syntax form definitions
├── hygiene.py               # Macro hygiene
├── matcher.py               # Pattern matching for special forms (Python layer)
├── builtins.py              # Builtins module placeholder (filled by bootstrap)
├── cli/                     # Console entry: Python stub, host, launchpad, and REPL
│   ├── __init__.py          # Sync entry; main() boots launch.ki via hosted_run
│   ├── host.py              # Python REPL host (prompter, eval boundary, banners)
│   ├── launch.ki            # CLI launchpad: argparse, file/command load, dispatch
│   └── repl.ki              # Interactive REPL loop
├── module.py                # Module lifecycle for .ki source files
├── importer.py              # Re-exports koskari_importer hooks
├── dis.py                   # Disassembler for Code objects
├── dis_ng.ki                # Self-hosted disassembler sources
├── bootstrap/               # Bootstrap koskari sources and loader
│   ├── *.ki                 # Core forms compiled into builtins
│   ├── __init__.py          # Bootstrap loader
│   └── compile/             # Self-hosted compiler pieces (in progress)
├── reader/                  # S-expression parser and layout block syntax
│   ├── parser.py            # Reader, SourceStream, LayoutReader
│   └── layouts/             # Layout handlers (conditionals, try, class, ...)
├── lexer/                   # Token lexer and prompt_toolkit REPL
│   ├── tokens.py, parser.py, discovery.py, highlight.py
│   └── prompt_toolkit/      # REPL integration
├── extras/                  # Optional integrations (namespace; no __init__.py)
│   ├── pygments/            # Syntax highlighting lexer
│   ├── sphinx/              # Sphinx extension and autodocumenters
│   └── lsp/                 # koskari-lsp language server
├── help/                    # In-REPL help for syntax, layouts, and tools
├── compile/                 # Compiler, bytecode fragments, special forms
│   ├── compiler.py
│   ├── helpers.py
│   ├── pragma/              # Compile-time pragma resolution
│   └── specials/            # Special form handlers
├── serialize/               # Code serialization (codex format)
└── lib/                     # C extension (native types, bytecode, evaluator)
    ├── atom.c, pair.c, namespace.c, formals.c, ref.c, values.c, ...
    ├── bytecode.c, eval.c, cont.c, dynchain.c, matcher.c, ...
    ├── dynchain/            # Dynamic extent handlers (with, wind, try, ...)
    ├── eval/                # Evaluator state machine
    └── opcode/              # Per-opcode implementations
```

### tests

```
tests/
├── lib/                     # Native extension and evaluator tests
│   └── opcode/              # Opcode-specific tests
├── compile/                 # Compiler and special-form tests
│   ├── helpers/
│   └── specials/
├── reader/                  # Parser and layout tests
│   └── layouts/
├── dis/                     # Disassembler tests
├── types/                   # Type predicate tests
├── importer/                # Import hook tests
├── lexer/                   # Lexer and prompt_toolkit tests
├── matcher/                 # Matcher tests
├── syntax/                  # Syntax form tests
├── hygiene/                 # Hygiene tests
├── help/                    # Help system tests
├── exceptions/              # Exception hierarchy tests
├── serialize/               # Serialization tests
├── cli/                     # CLI tests
├── reproducers/             # Regression reproducers
├── extras/                  # Optional integration tests (phase_extras)
│   ├── pygments/
│   ├── sphinx/
│   └── lsp/
└── native/                  # C harness and container memory tests
```


## Design Documentation

Comprehensive design documentation lives in [design/](design/). Start with [design/README.md](design/README.md) for the index. Topics include language features (special forms, operators, macros, layouts), API and types, interpreter architecture (bytecode, compiler, evaluator), and call/CC design.


## Testing

```bash
# Run Python tests (via tox)
make test

# Run native memory tests in container (requires podman or docker)
make test-native           # Basic native tests
make test-asan             # AddressSanitizer
make test-valgrind         # Valgrind memcheck

# Run tests across Python versions in containers
make test-pyversions
```


## License

GPL v3 - See [LICENSE](LICENSE) for details.


## Author

Christopher O'Brien <obriencj@preoccupied.net>
