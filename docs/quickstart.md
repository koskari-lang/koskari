# Quickstart

## Install

Koskari publishes to PyPI with wheels for Linux and macOS:

```bash
pip install "koskari[repl]"
```

The `repl` extra adds syntax highlighting to the REPL. Weekly development
snapshots also publish as pre-releases; `pip install --pre` selects the
latest one. To build from a source checkout instead, see
[Build and testproject/build-test.md).

## First session

Start the REPL:

```bash
koskari
```

Type a form at the prompt and Koskari prints its value:

```koskari
koskari> (+ 1 2)
3
koskari> (print "Hello, Koskari!")
Hello, Koskari!
```

## A file

Save this as `hello.ki`:

```koskari
(print "Hello, Koskari!")
```

Run it:

```bash
koskari hello.ki
```

## A small program

A `.ki` file is written in layout: a line with more than one element is a
call, and a line ending in `:` opens an indented block. Parentheses still
group nested expressions. Save this as `fruits.ki` and run it the same
way:

```koskari
define fruits #["apple" "pear" "plum"]

define shout [word]:
    (+ (word.upper) "!")

for-each [fruit fruits]:
    print (shout fruit)

print (len fruits) "fruits, starting with" fruits[0]
```

```text
APPLE!
PEAR!
PLUM!
3 fruits, starting with apple
```

`(word.upper)` calls a Python string method and `fruits[0]` indexes a
Python list. Everything Koskari touches is a Python object.

## Compile to bytecode

```bash
koskari --compile-kio out.kio hello.ki
koskari --execute-kio out.kio
```

See [CLI and configurationreference/cli-and-config.md) for every flag.

## Editor setup

Syntax highlighting, and optionally LSP support, for three editors. The
files are under `editors/` in the repository.

| Editor | Included | Setup |
|--------|----------|-------|
| **Emacs** | Major mode: highlighting, indentation, comments | Load `editors/emacs/koskari-mode.el`; see `editors/emacs/README.md` |
| **Helix** | Tree-sitter grammar and LSP integration | Build the grammar; see `editors/helix/README.md` |
| **VSCode / TextMate** | TextMate grammar for highlighting | Install from the folder or VSIX; see `editors/textmate/README.md` |

For Helix and the language server, install the `lsp` extra as well:

```bash
pip install "koskari[repl,lsp]"
```

## Next

- New to programming: [Little Koskaritutorials/beginner/index.md).
- Know Python: [From Pythontutorials/from-python/index.md).
- Know a Lisp already: the [Guideguide/index.md) page on
  [Koskari and s-expressionsguide/layout-and-s-expressions.md), then the
  [Referencereference/index.md).
