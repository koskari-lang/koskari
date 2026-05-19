# Koskari

Koskari is a LISP dialect for Python


## Overview

Koskari is the fourth iteration of the Sibilant idea, learning from the experiences of:
- **spexy**: String-to-string transformer; parsed s-expression text and emitted Python expression strings. The earliest groundwork — reader and s-expression representation — carries through to this day.
- **python-sibilant**: CPython bytecode target (abandoned due to version churn)
- **sibilant-ceval**: Custom bytecode evaluator (blocked on call/cc implementation)

This version aims to be a perfect playground - a language with all the useful features of Python but significantly fewer restrictions, designed to make writing code a joyous expression of will upon the computer.


## Implementation Status

**Core Types:**
- `symbol`, `keyword`: Interned atom types
- `pair`, `nil`: Cons cells and empty list
- `values`: Multi-value return objects
- `defer`: Coroutine deferral

**Runtime Infrastructure:**
- `Namespace`, `ModuleNamespace`, `CollectorNamespace`: Lexical scopes
- `Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`: Reference cells
- `Formals`, `FormalsSpec`: Function parameter handling

**Parser:**
- `BaseSourceStream`: Native C base for efficient character I/O with Unicode position tracking
- `SourceStream`: Python stream wrapper with error handling
- `Reader`, `LayoutReader`: Extensible s-expression reader with layout block syntax and reader macros
- `lexer`: Token stream for REPL and LSP

**Compiler:**
- many special forms implemented; see [design/SPECIALS.md](design/SPECIALS.md) for the current list
- squishing and compounding passes to convert into super-ops

**Evaluator:**
- Custom bytecode evaluator (`koskari_eval`): coroutines, async generators, continuations, dynamic wind/unwind, parameterize, with/dyn-with
- Stackless (where possible), tail-call optimizing
- Ambiently async: awaitable results are automatically awaited when they do not have an immediate result. The evaluator context suspends at once and resume in-place.
- Bounded and unbounded continuations.
- CLI with REPL


## TODO

Many ideas are under evaluation, see [design/TODO.md](design/TODO.md) for what's on the table.


## Quick Start

### Building

```bash
make build
```

### Installing

```bash
make install
```

Or for development:

```bash
uv install -e .[prompt_toolkit]
uv run koskari
```

### Development

```bash
# Build native extensions
make build-ext

# Run tests
make test

# Run tests across Python versions (containerized)
make test-pyversions

# Full tox suite (tests, flake8, ...)
make tox

# Lint code
make lint

# Type-check
make typecheck

# Clean build artifacts
make clean
```


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
├── tools/                   # Opcode/editor regeneration and container helpers
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
├── cli.py                   # koskari REPL CLI
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
├── lexer/                   # Token lexer, prompt_toolkit REPL, LSP server
│   ├── tokens.py, parser.py, discovery.py
│   ├── prompt_toolkit/      # REPL integration
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
├── lexer/                   # Lexer, LSP, and prompt_toolkit tests
├── matcher/                 # Matcher tests
├── syntax/                  # Syntax form tests
├── hygiene/                 # Hygiene tests
├── help/                    # Help system tests
├── exceptions/              # Exception hierarchy tests
├── serialize/               # Serialization tests
├── cli/                     # CLI tests
├── reproducers/             # Regression reproducers
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
