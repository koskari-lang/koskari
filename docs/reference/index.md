# Reference

Catalogue pages. Each entry states a form's syntax, its behaviour, and an
example. The prose that explains why a mechanism works the way it does is
in the [Guide](../guide/index.md).

## Syntax

- [Grammar](grammar.md): every atom, literal, prefix, and suffix the reader
  accepts, and the collection literals.
- [Reader and layout syntax](reader-layout.md): the normative rules for
  blocks, lines, the structural colon, and peer blocks.
- [Glossary](glossary.md): the vocabulary the other pages use.

## Compile-time forms

- [Special forms](specials/index.md): the forms the compiler handles
  directly. `var`, `lambda`, `cond`, `try`, `match`, the operators, the
  imports, continuations.
- [Macros](macros/index.md): bootstrap macros that expand to special forms.
  `if`, `when`, `set!`, `class`, `let*`, the `-each` family.
- [Layout handlers](layouts/index.md): the block heads that read an
  indented block into a form, and their peers.
- [Fence handlers](fences/index.md): triple-backtick foreign literals.

## Runtime

- [Runtime library](runtime/index.md): builtins written in Koskari or
  native to it. `help`, `macroexpand`, `map` and `filter`,
  `koskari.itertools`, `koskari.solve`.
- [Bytecode and VM](vm/index.md): the instruction set, the opcode
  families, and a page per opcode.

## Tools and diagnostics

- [CLI and configuration](cli-and-config.md): the `koskari` command, its
  flags, and `.kio` compilation.
- [Error guide](error-codes.md): the reader, compile, and runtime
  exception classes and what each one means.

## Python API

- [Python API](api/index.md): autodoc for every module, for embedding
  Koskari in a Python program.

```{toctree}
:hidden:
:maxdepth: 1
:caption: Syntax

grammar
reader-layout
glossary
```

```{toctree}
:hidden:
:maxdepth: 1
:caption: Compile-time

specials/index
macros/index
layouts/index
fences/index
```

```{toctree}
:hidden:
:maxdepth: 1
:caption: Runtime

runtime/index
vm/index
```

```{toctree}
:hidden:
:maxdepth: 1
:caption: Tools

cli-and-config
error-codes
api/index
```
