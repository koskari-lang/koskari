# Quickstart

## Install

Koskari publishes to PyPI with wheels for Linux and macOS:

```bash
pip install "koskari[repl]"
```

The `repl` extra adds syntax highlighting to the REPL. Every release so
far is a weekly development snapshot, a pre-release, so `pip install
koskari` resolves to the newest one; once a final release exists, `--pre`
keeps selecting snapshots. To build from a source checkout instead, see
[Build and test](project/build-test.md).

## First session

Start the REPL:

```bash
koskari
```

The prompt is `>>> `. Type a form at it and Koskari prints the form's
value on the next line:

```koskari
>>> (+ 1 2)
3
>>> (print "Hello, Koskari!")
Hello, Koskari!
```

A form left open at the end of a line continues on the next, under a
`... ` prompt:

```koskari
>>> (+ 1
...    2)
3
```

Leave with `(exit)` or Ctrl-D.

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
var fruits #["apple" "pear" "plum"]

def shout [word]:
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

`--compile-kio` runs the program as well as writing the `.kio`, so the
first command prints `Hello, Koskari!` too.

See [CLI and configuration](reference/cli-and-config.md) for every flag.

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

- New to programming: [Little Koskari](tutorials/beginner/index.md).
- Know Python: [From Python](tutorials/from-python/index.md).
- Know a Lisp already: the [Guide](guide/index.md) page on
  [Koskari and s-expressions](guide/layout-and-s-expressions.md), then the
  [Reference](reference/index.md).
