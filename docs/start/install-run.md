# Install and run

## Prerequisites

- Python 3.9 or newer (CPython)
- A C compiler with musttail support for the native extension: clang, or GCC 15 or newer
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
pip install -e ".[repl]"
```

The optional `repl` extra enables syntax-highlighted REPL input.

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

```koskari
(print "Hello, Koskari!")
```

## Compile to bytecode file

```bash
koskari --compile-kio out.kio examples/hello.ki
koskari --execute-kio out.kio
```

`--compile-kio` and `--execute-kio` take source paths on the flag itself.

See [CLI and configuration](../reference/cli-and-config.md) for all flags.

## Editor setup

Syntax highlighting and (optionally) LSP support are available for three editors.
Files live under `editors/` in the repository root.

| Editor | What's included | Setup |
|--------|-----------------|-------|
| **Emacs** | Major mode with syntax highlighting, indentation, comment support | Load `editors/emacs/koskari-mode.el` — see `editors/emacs/README.md` |
| **Helix** | Tree-sitter grammar + LSP integration | Requires building the grammar — see `editors/helix/README.md` |
| **VSCode / TextMate** | TextMate grammar for syntax highlighting | Install from folder or VSIX — see `editors/textmate/README.md` |

For Helix and LSP support, install the `lsp` extra first:

```bash
pip install -e ".[repl,lsp]"
```

## Next steps

- [Special forms reference](../reference/specials/index.md)
- [Layout syntax](../reference/layouts/index.md)
- [Hello world recipe](../patterns/recipes/hello.md)
