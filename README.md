# <img src="https://koskari.org/_static/koskari-icon.svg" height="42" align="top" alt=""> Koskari

Koskari is a Lisp that lives inside Python: its modules import and are
imported like any other, its values are Python objects, and its programs run
on a bytecode evaluator written to run alongside CPython's own.

```koskari
def greet [name]:
  print f"Hello, {name}!"

@entry-point
def main []:
  greet "world"
```

```bash
koskari hello.ki
# Hello, world!
```

- **Layout or parentheses.** Indented blocks read as forms, so a program looks
  like Python; full s-expressions are there whenever you want them.
- **Python interop in both directions.** A `.ki` file sits beside `.py` files
  and imports through the ordinary import system; Python code imports it
  back and calls into it.
- **Hygienic macros**, `syntax-rules` style, with quasiquote and reader
  fences for foreign literals.
- **Async without the keywords.** The evaluator awaits awaitable values in
  place, so code that uses asyncio reads as straight-line code.
- **Control flow from Scheme.** Tail calls, delimited continuations, `call/cc`,
  dynamic wind and parameterize, alongside Python's exceptions and `with`.
- **Pattern matching** over Python objects and Koskari data.
- **A REPL, a disassembler, and cached compiled modules.**


## Quick Start

**Prerequisites:** Python 3.9+, a C compiler with musttail support (clang, or
GCC 15 or newer; the evaluator threads opcode dispatch through guaranteed tail
calls, and the build picks clang when it is on the PATH or honors `CC`), and
[uv](https://docs.astral.sh/uv/) for the virtual-environment workflow below.

**Platforms:** Koskari is designed and tested against 64-bit CPython only.
The wheels cover Linux x86_64 and aarch64 and macOS arm64 and x86_64; a
source build on anything else is untested.


### Virtual environment

From a repository checkout:

```bash
make venv          # creates .venv and installs koskari (editable, with repl extra)
make venv-koslife  # optional: adds the koslife demo
uv run koskari     # interactive REPL
uv run koskari examples/hello.ki
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

See the [quickstart](docs/quickstart.md) for the full first-session walkthrough.


## Documentation

User documentation is published at [docs.koskari.org](https://docs.koskari.org/)
and lives in [docs/](docs/), where `make docs` builds it:

- **[Quickstart](docs/quickstart.md)** -- install, a first session, a first
  program.
- **[Tutorials](docs/tutorials/index.md)** -- [Little Koskari](docs/tutorials/beginner/index.md)
  for readers new to programming, and [From Python](docs/tutorials/from-python/index.md)
  with worked examples for Python developers.
- **[Guide](docs/guide/index.md)** -- one page per concept: evaluation, layout,
  bindings and refs, functions, control flow, macros, demand and async, hosting.
- **[Reference](docs/reference/index.md)** -- grammar, special forms, macros,
  layouts, the runtime library, VM opcodes, and the Python API.


## Corpus and MCP server

Koskari maintains a verified example corpus under
[docs/corpus/](docs/corpus/): records carry `read` / `expand` / `eval`
verification phases, so they double as regression tests and as canonical
teaching material. The corpus is published as a Hugging Face dataset,
[koskari/koskari](https://huggingface.co/datasets/koskari/koskari).

A container-based **MCP server** exposes the corpus and the documentation to AI
coding assistants (Cursor, Claude Desktop, VS Code, and similar) so they can
search and author idiomatic Koskari. See
[tools/mcp/README.md](tools/mcp/README.md) for build and client setup.


## History

Koskari is the fourth iteration of the Sibilant idea, learning from the experiences of:
- **[spexy](https://github.com/obriencj/python-spexy)**: S-expressions compiled to Python expression strings, with `let`, `cond`, and multi-expression `lambda` grafted on. Where the reader and s-expression representation began.
- **[python-sibilant](https://github.com/obriencj/python-sibilant)**: CPython bytecode target (abandoned due to version churn)
- **[sibilant-ceval](https://github.com/obriencj/sibilant-ceval)**: Custom bytecode evaluator (blocked on call/cc implementation)

This version aims to be a perfect playground -- a language with all the useful features of Python but significantly fewer restrictions, designed to make writing code a joyous expression of will upon the computer.


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
| **Self-hosting** | Core builtins and much of the library live in bootstrap `.ki` sources; the CLI runs on self-hosted `.ki`, and the self-hosted compiler is on the roadmap |

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
├── docs/                    # User documentation (Sphinx) and the example corpus
├── editors/                 # Editor syntax assets (emacs, helix, textmate, tree-sitter, zed)
├── examples/                # Example koskari programs (.ki files)
├── koskari/                 # Main Python package
├── koskari.pth              # Imports koskari on startup to install the .ki/.kio hooks
├── tests/                   # Test suite (mirrors package layout)
├── tools/                   # Opcode/editor regen, corpus tooling, and the MCP server
├── setup.py, setup.cfg      # Package build and tox configuration
├── pyproject.toml           # Build-system declaration
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
├── pragma.py                # Module pragma object (immutable setting mapping)
├── builtins.py              # Builtins module placeholder (filled by bootstrap)
├── asyncio.py               # asyncio compatibility support
├── launcher.py              # Runtime host for koskari_scripts commands and programmatic entry
├── itertools.ki             # Iterator combinators over sync and async sources
├── solve.ki                 # Nondeterministic choice (amb) on delimited continuations
├── assets/                  # Packaged assets (icon)
├── cli/                     # Console entry: Python stub, host, launchpad, and REPL
│   ├── __init__.py          # Sync entry; main() boots launch.ki via hosted_run
│   ├── host.py              # Python REPL host (prompter, eval boundary, banners)
│   ├── launch.ki            # CLI launchpad: argparse, file/command load, dispatch
│   ├── repl.ki              # Interactive REPL loop
│   └── term_graphics.ki     # Terminal graphics detection for the REPL banner
├── module.py                # Module lifecycle for .ki source files
├── importer.py              # Import hook and loader for .ki/.kio modules
├── dis.ki                   # Self-hosted disassembler for Code objects
├── bootstrap/               # Bootstrap koskari sources and loader
│   ├── *.ki                 # Core forms compiled into builtins
│   └── __init__.py          # Bootstrap loader
├── reader/                  # S-expression parser and layout block syntax
│   ├── parser.py            # Reader, SourceStream, LayoutReader
│   ├── stream.py            # Source streams over files and strings
│   ├── layout.py            # Layout block reading
│   ├── pragma.py            # Reader-side pragma settings
│   └── layouts/             # Layout handlers (conditionals, try, class, ...)
├── matcher/                 # Pattern compiler for match forms (Python layer)
├── fences/                  # Fence handlers for fenced foreign literals
├── lexer/                   # Token lexer and prompt_toolkit REPL
│   ├── tokens.py, parser.py, discovery.py, highlight.py
│   └── prompt_toolkit/      # REPL integration
├── extras/                  # Optional integrations (namespace; no __init__.py)
│   ├── pygments/            # Syntax highlighting lexer
│   ├── sphinx/              # Sphinx extension and autodocumenters
│   ├── setuptools/          # Build-time helpers for koskari_scripts packaging
│   └── lsp/                 # koskari-lsp language server
├── help/                    # In-REPL help for syntax, layouts, and tools
├── compile/                 # Compiler, bytecode fragments, special forms
│   ├── compiler.py
│   ├── helpers.py
│   ├── processor.py         # Bytecode post-processing passes
│   ├── pragma/              # Compile-time pragma resolution
│   └── specials/            # Special form handlers
├── serialize/               # Code serialization (codex format)
└── lib/                     # C extension (native types, bytecode, evaluator)
    ├── atom.c, pair.c, namespace.c, formals.c, ref.c, values.c, ...
    ├── bytecode.c, eval.c, cont.c, dynchain.c, matcher.c, transform.c, ...
    ├── dynchain/            # Dynamic extent handlers (with, wind, try, ...)
    ├── eval/                # Evaluator state machine
    ├── matcher/             # Native pattern-matching steps
    ├── opcode/              # Per-opcode implementations
    └── transform/           # Lazy transform providers (map, filter, zip, ...)
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
├── matcher/                 # matcher tests
├── syntax/                  # Syntax form tests
├── hygiene/                 # Hygiene tests
├── help/                    # Help system tests
├── exceptions/              # Exception hierarchy tests
├── serialize/               # Serialization tests
├── cli/                     # CLI tests
├── corpus/                  # Corpus record conformance (phase_corpus)
├── itertools/               # Lazy transform provider tests
├── solve/                   # Solver library tests
├── tools/                   # Maintainer tool tests
├── reproducers/             # Regression reproducers
├── extras/                  # Optional integration tests (phase_extras)
│   ├── pygments/
│   ├── sphinx/
│   ├── setuptools/
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
