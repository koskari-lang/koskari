# Koskari

Koskari is a LISP dialect for Python - a playground language designed for joyful expression and exploration.

## Overview

Koskari is the third iteration of the Sibilant project, learning from the experiences of:
- **python-sibilant**: CPython bytecode target (abandoned due to version churn)
- **sibilant-ceval**: Custom bytecode evaluator (blocked on call/cc implementation)

This version aims to be a "perfect playground" - a language with all the useful features of Python but significantly fewer restrictions, designed to make writing code "a joyous expression of will upon a computer."

## Features (Planned)

- **Flexible bind/ref model**: Runtime rebinding of references with executable evaluators
- **Macros**: Reader macros and compile-time macros for metaprogramming
- **Values type**: Multi-value returns for flexible composition
- **Call/CC**: Continuations for control flow experimentation (bounded and unbounded)
- **Tail-call optimization**: Efficient recursion
- **Async/await**: Full coroutine support
- **Generators**: Lazy evaluation
- **And more**: See [design documentation](design/) for complete feature catalog

## Implementation Status

### Completed (with native C extensions)

**Core Types:**
- `symbol`, `keyword`: Interned atom types
- `pair`, `nil`: Cons cells and empty list
- `values`: Multi-value return objects
- `gensym()`: Unique symbol generation

**Runtime Infrastructure:**
- `Namespace`, `ModuleNamespace`, `CollectorNamespace`: Lexical scopes
- `Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`: Reference cells
- `Formals`, `FormalsSpec`: Function parameter handling

**Parser:**
- `BaseSourceStream`: Native C base for efficient character I/O with Unicode position tracking
- `SourceStream`: Python stream wrapper with error handling
- `Reader`: Extensible s-expression reader with macro support

### In Progress

- Compiler and bytecode generation
- Custom evaluator (ceval)
- REPL and CLI

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
make install-dev
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
├── koskari/             # Main package
│   ├── __init__.py      # Package exports
│   ├── types.py         # Type definitions and predicates
│   ├── parse.py         # S-expression parser
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
├── tests/               # Test suite (570+ tests)
│   ├── types/           # Type unit tests
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

Christopher O'Brien <obriencj@gmail.com>
