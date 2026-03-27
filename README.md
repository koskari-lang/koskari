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
- `Reader`: Extensible s-expression reader with macro support

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

# Run all Python version tests
make test-all

# Lint code
make lint

# Clean build artifacts
make clean
```


## Project Structure

```
koskari/
├── design/              # Design documentation
├── koskari/             # Main package (import from submodules; see design/api/package-layout.md)
│   ├── __init__.py      # Minimal; no re-exports
│   ├── exceptions.py    # All exceptions (Koskari*, Reader*, Layout*, Compiler*, etc.)
│   ├── types.py         # Value types from lib + Python predicates/helpers
│   ├── reader/          # S-expression parser, layout block syntax, reader macros
│   │   ├── parser.py    # Reader, SourceStream, LayoutReader, parse errors
│   │   └── layouts/     # Layout handlers (conditionals, try, class, define-syntax, etc.)
│   ├── lexer/           # Lexical parsing, prompt_toolkit, LSP integration
│   ├── matcher.py       # Pattern matching for special forms
│   ├── importer/        # Import hooks for .ki files
│   ├── dis.py           # Disassembler for Code objects
│   ├── compile/         # Compiler, ByteCodeFragment, special forms
│   └── lib/             # C extension sources
│       ├── atom.c       # Symbol/keyword implementation
│       ├── pair.c       # Cons cell implementation
│       ├── namespace.c  # Namespace implementation
│       ├── formals.c    # Parameter binding
│       ├── ref.c        # Reference cells
│       ├── values.c     # Multi-value returns
│       ├── operator.c   # Operator helpers
│       ├── parse.c      # Native stream I/O
│       └── ...
├── tests/               # Test suite (see design/api/package-layout.md)
│   ├── lib/             # Native extension and ceval tests
│   ├── compile/         # Compiler and bytecode fragment tests
│   ├── reader/          # Parser and layout tests
│   ├── dis/             # Disassembler tests
│   ├── types/           # Type predicate/helper tests
│   ├── importer/        # Import system tests
│   ├── lexer/           # Lexer tests
│   ├── matcher/         # Matcher tests
│   ├── cli/             # CLI tests
│   └── native/          # Native memory tests (ASan, Valgrind)
└── examples/            # Example koskari code (.ki files)
```


## Design Documentation

Comprehensive design documentation is available in the [design/](design/) directory, including:
- Language features (special forms, operators, macros, syntax)
- API documentation (types, builtins, runtime)
- Interpreter architecture (bytecode, compiler, evaluator)
- Call/CC design considerations


## Testing

```bash
# Run Python tests
make test

# Run native memory tests (requires container or local tools)
make test-native           # Basic native tests
make test-native-asan      # AddressSanitizer
make test-native-valgrind  # Valgrind memcheck

# Container-based testing
make container-test        # All native tests in container
```


## License

GPL v3 - See [LICENSE](LICENSE) for details.


## Author

Christopher O'Brien <obriencj@preoccupied.net>
