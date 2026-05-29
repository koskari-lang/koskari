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
