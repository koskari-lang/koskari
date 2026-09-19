# Layout handlers

A layout handler reads an indented block into a form. The reader resolves
the block's head as `layout/<head>` in the module namespace, then in the
builtins, and hands the handler the header and the body lines. A head
with no handler takes the default, which splices the body lines after
the header as arguments. The general rules for blocks, lines, the
structural colon, and peer blocks are in
[Reader and layout syntax](../reader-layout.md); the motivation is in
[Koskari and s-expressions](../../guide/layout-and-s-expressions.md).

## Handlers

| Head | Peers | Reads to |
|------|-------|----------|
| [any other head](layout-block-default.md) | | `(HEAD ARGS... BODY...)`, one argument per body line, keyword lines as keyword arguments |
| [`block-begin`](layout-block-begin.md) | | `(HEAD ARGS... (begin BODY...))`; bind `layout/<head>` to it for a head that wants one body form |
| [`if`](layout-if.md) | `elif`, `else` | `(cond [TEST BODY] ... [else: BODY])` |
| [`while`](layout-while.md) | `else` | `(while TEST BODY... (else: BODY...))` |
| [`for-each`](layout-for-each.md) | `else` | `(for-each [VAR SEQ] BODY... (else: BODY...))` |
| [`try`](layout-try.md) | `except`, `else`, `finally` | `(try (begin BODY...) [[e Type] ...] [else: ...] [finally: ...])` |
| [`switch`](layout-switch.md) | | `(switch SUBJECT [VALUE BODY...] ... [else: BODY...])` |
| [`match`](layout-match.md) | | `(match SUBJECT (case PATTERN BODY...) ... (else: BODY...))` |
| [`class`](layout-class.md) | | `(class NAME SUPERS (begin BODY...))`, a class expression |
| [`define-class`](layout-define-class.md) | | `(define-class NAME SUPERS (begin BODY...))` |
| [`syntax-rules`](layout-syntax-rules.md) | | a `syntax-rules` transformer, one arm per `PATTERN:` block |
| [`syntax-match`](layout-syntax-match.md) | | a `syntax-match` transformer |
| [`macro-rules`](layout-macro-rules.md) | | a `macro-rules` transformer |
| [`macro-match`](layout-macro-match.md) | | a `macro-match` transformer |

Peers are blocks at the same indent as the primary header whose head is
in the handler's peer set; the reader gathers them and expands the whole
spine as one form. Blank and comment-only lines between a body and its
peer do not break the spine.

## Blocks as data

An indented block is first read as a `:block:` cons, the header's
elements as one list and then one entry per body line:

```text
(:block: (HEAD ARG ...) ENTRY ...)
ENTRY ::= (ELEMENT ...) | (:block: ...)
```

`LayoutReader.read()` expands those conses through `apply_layout` for a
single block and `apply_layouts` for a primary block with its peers,
invoking the handler as `read(reader, source, peers)`. The handler calls
`reader.fold_line` or `reader.fold_lines` where it wants forms and reads
the elements directly where it does not, which is how the default block
handler turns a keyword line into a keyword argument.

Quoted and quasiquoted blocks keep the `:block:` structure as data,
through `layout/quote` and `layout/quasiquote`, and a macro template
that contains a block is expanded through `apply_layout` before hygiene
so that expansion matches what the reader would have produced.

## Adding a handler

1. A block that only needs its body spliced after the header needs no
   handler; the default applies.
2. A block whose body should be one `begin` form binds
   `define layout/<head> layout/block-begin` in a `.ki` module.
3. Anything else is a Python function `(reader, source, peers)` under
   `koskari/reader/layouts/`, decorated with
   `@layout(symbol('layout/head'), peers=(...))` when it gathers peers,
   and registered in the layout namespace.
4. Add a page under `docs/reference/layouts/` for it.

```{toctree}
:maxdepth: 1
:hidden:
:glob:

layout-*
```
