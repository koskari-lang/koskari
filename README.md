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

# Run tests (when implemented)
make test

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
│   ├── lib/             # C extension sources
│   ├── compiler/        # Compiler implementation
│   ├── parser/          # Parser implementation
│   ├── evaluator/       # Evaluator/ceval implementation
│   └── runtime/         # Runtime system
├── tests/               # Test suite
└── examples/            # Example koskari code (.ki files)
```

## Design Documentation

Comprehensive design documentation is available in the [design/](design/) directory, including:
- Language features (special forms, operators, macros, syntax)
- API documentation (types, builtins, runtime)
- Interpreter architecture (bytecode, compiler, evaluator)
- Call/CC design considerations

## Status

**Early Development** - This is a fresh start. Core components are being designed and implemented.

## License

GPL v3 - See [LICENSE](LICENSE) for details.

## Author

Christopher O'Brien <obriencj@gmail.com>
