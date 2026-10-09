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
| [`inline-block`](layout-inline-block.md) | | `(HEAD ARGS... ELEMENT...)`, every element of every body line spliced and nothing folded; bind `layout/<head>` to it for a head whose block only breaks a long line |
| [`import-from`](layout-import-from.md) | | `(import-from MODULE NAME...)`, the inlining lowering applied to a long import list |
| [`if`](layout-if.md) | `elif`, `else` | `(cond [TEST BODY] ... [else: BODY])` |
| [`while`](layout-while.md) | `else` | `(while TEST BODY... (else: BODY...))` |
| [`for-each`](layout-for-each.md) | `else` | `(for-each [VAR SEQ] BODY... (else: BODY...))` |
| [`try`](layout-try.md) | `except`, `else`, `finally` | `(try (begin BODY...) ([Type] BODY...) ... (else: ...) (finally: ...))`, each except clause keeping the bracketed form as written |
| [`dyn-wind`](layout-dyn-wind.md) | `before`, `after` | `(dyn-wind BEFORE (begin BODY...) AFTER)`, an absent peer emitting `()` |
| [`switch`](layout-switch.md) | | `(switch SUBJECT [VALUE BODY...] ... [else: BODY...])` |
| [`match`](layout-match.md) | | `(match SUBJECT [[PATTERN...] BODY...] ... [else: BODY...])` |
| [`class`](layout-class.md) | | `(class NAME SUPERS (begin BODY...))`, a class expression |
| [`define-class`](layout-define-class.md) | | `(define-class NAME SUPERS (begin BODY...))` |
| [`define-dataclass`](layout-define-dataclass.md) | | `(define-dataclass NAME SUPERS (begin BODY...))` |
| [`syntax-rules`](layout-syntax-rules.md) | | a `syntax-rules` transformer, one arm per rule block |
| [`syntax-match`](layout-syntax-match.md) | | a `syntax-match` transformer |
| [`macro-rules`](layout-macro-rules.md) | | a `macro-rules` transformer |
| [`macro-match`](layout-macro-match.md) | | a `macro-match` transformer |
| [`special-rules`](layout-special-rules.md) | | a `special-rules` transformer |
| [`special-match`](layout-special-match.md) | | a `special-match` transformer |
| [`bare-syntax`](layout-bare-syntax.md) | | `(bare-syntax [NAME] SLOT TEMPLATE)`, the arm for the name used as a lone atom |

Peers are blocks at the same indent as the primary header whose head is
in the handler's peer set; the reader gathers them and expands the whole
spine as one form. Blank and comment-only lines between a body and its
peer do not break the spine.

## Blocks as data

An indented block is first read as a `#block:` cons, the header's
elements as one list and then one entry per body line:

```text
(#block: (HEAD ARG ...) ENTRY ...)
ENTRY ::= (#line: ELEMENT ...) | (#block: ...)
```

`LayoutReader.read()` expands those conses through `apply_layout` for a
single block and `apply_layouts` for a primary block with its peers,
invoking the handler as `read(reader, source, peers)`. The handler calls
`reader.fold_line` or `reader.fold_lines` where it wants forms and reads
the elements directly where it does not, which is how the default block
handler turns a keyword line into a keyword argument.

Quoted and quasiquoted blocks keep the `#block:` structure as data,
through `layout/quote` and `layout/quasiquote`, or `layout/quote-syntax`
and `layout/quasiquote-syntax` for the `#'head:` and `` #`head: ``
prefixes that keep hygiene marks, and a macro template
that contains a block is expanded through `apply_layout` before the
captured material is unboxed, so that expansion matches what the
reader would have produced.

## Writing a handler

A block that only needs its body spliced after the header needs no handler;
the default applies.

A block whose body should be one `begin` form binds
`define layout/<head> layout/block-begin` in the module that uses it. A block
that only breaks a long line binds `layout/inline-block` the same way.

Anything else is a `LayoutHandler` bound to `layout/<head>`. The handler wraps a
function of `(reader, source, peers)`: `source` is the block cons's cdr,
`((HEAD . ARGS) . ENTRIES)`; `peers` holds the gathered peer blocks; the return
value is the form the block reads to. `LayoutHandler` takes the head symbol,
the function, and optional `peers:` and `raw:` arguments. Only a
`LayoutHandler` is recognised -- a bare function bound to `layout/<head>` is
ignored.

```koskari
>>> import-from koskari.reader.layouts LayoutHandler
>>> define report-read [reader source peers]:
...   (cons (symbol "#list") (reader.fold_lines source.cdr))
>>> define layout/report (LayoutHandler (symbol "layout/report") (demand report-read))
>>> define vals:
...   report:
...     1
...     (+ 1 1)
...     3
>>> vals
#[1 2 3]
```

The handler runs at read time, so it must already be bound when the block is
read -- in an imported module, or earlier in the same file.

```{toctree}
:maxdepth: 1
:hidden:
:glob:

layout-*
```
