# Python API

The Python modules, documented from their docstrings. Most programs need
none of them: writing `.ki` files takes the language, and importing one
from Python takes `import koskari`. They matter when Koskari is embedded
in a Python application, extended with a Python-side special form or
layout handler, or driven by tooling.

## Embedding and running

| Module | Holds |
|--------|-------|
| [`koskari`](koskari.md) | The package: importing it installs the `.ki` import hook |
| [`koskari.module`](koskari-module.md) | Creating a module, attaching a file, loading it on the demand or async path |
| [`koskari.importer`](koskari-importer.md) | The meta-path finder and loaders for `.ki` and `.kio` |
| [`koskari.cli`](koskari-cli.md) | The `koskari` command, the REPL, and the async entry points |
| [`koskari.asyncio`](koskari-asyncio.md) | Bridging Koskari callables and asyncio tasks |
| [`koskari.exceptions`](koskari-exceptions.md) | The exception classes for the read, compile, and run stages |

## The language

| Module | Holds |
|--------|-------|
| [`koskari.builtins`](koskari-builtins.md) | Every name bound in a fresh module, partitioned into runtime, compile-time, and reader sections |
| [`koskari.lib`](koskari-lib.md) | The native extension: symbols, pairs, refs, namespaces, functions, the evaluator |
| [`koskari.types`](koskari-types.md) | Type predicates and helpers |
| [`koskari.reader`](koskari-reader.md) | The s-expression and layout readers |
| [`koskari.reader.layouts`](koskari-reader-layouts.md) | The layout handlers |
| [`koskari.compile`](koskari-compile.md) | The compiler |
| [`koskari.compile.specials`](koskari-compile-specials.md) | The special forms, by family |
| [`koskari.syntax`](koskari-syntax.md) | Syntax transformers: rules, match, and bare forms |
| [`koskari.hygiene`](koskari-hygiene.md) | Datum wrappers and renaming for macro expansion |
| [`koskari.matcher`](koskari-matcher.md) | The pattern matcher behind specials, macros, and `match` |
| [`koskari.serialize`](koskari-serialize.md) | The codex format for `.kio` files |
| [`koskari.dis`](koskari-dis.md) | The bytecode disassembler |
| [`koskari.help`](koskari-help.md) | The help system |

## Tooling

| Module | Holds |
|--------|-------|
| [`koskari.lexer`](koskari-lexer.md) | The token stream used by the REPL and the language server |
| [`koskari.lexer.prompt_toolkit`](koskari-lexer-prompt_toolkit.md) | The highlighted REPL input |
| [`koskari.extras.pygments`](koskari-extras-pygments.md) | The Pygments lexer |
| [`koskari.extras.sphinx`](koskari-extras-sphinx.md) | The Sphinx extension: autodoc for Koskari objects, the builtins directive, the dialogue and pair rows |
| [`koskari.extras.lsp`](koskari-extras-lsp.md) | The language server |

```{toctree}
:maxdepth: 1
:hidden:

koskari
koskari-module
koskari-importer
koskari-cli
koskari-asyncio
koskari-exceptions
koskari-builtins
koskari-lib
koskari-types
koskari-reader
koskari-reader-layouts
koskari-compile
koskari-compile-specials
koskari-syntax
koskari-hygiene
koskari-matcher
koskari-serialize
koskari-dis
koskari-help
koskari-lexer
koskari-lexer-prompt_toolkit
koskari-extras-pygments
koskari-extras-sphinx
koskari-extras-lsp
```
