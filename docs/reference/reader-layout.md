# Reader and layout syntax

Normative rules for how the Koskari layout reader turns source text into form
trees. For motivation and the relationship between layout and s-expressions,
see [Koskari and s-expressions](../guide/layout-and-s-expressions.md).

## Overview

The layout reader (`LayoutReader`) is the default reader for Koskari
modules. It accepts indentation-based blocks and lines at delimiter
depth 0, embeds ordinary s-expressions inside `(`, `[`, and `{`, and
parses colon blocks as `block:` conses and expands them through
layout handlers (`layout/<head>`) into list forms for the compiler.

Reading is strict: malformed layout or punctuation fails before evaluation.

## Layout mode and s-expression mode

Layout mode applies at delimiter depth 0 -- outside all `(`, `[`, and
`{`. A module file or REPL line at the top level uses layout rules for blocks
and lines.

S-expression mode applies inside any open delimiter. Forms nested in
parentheses, brackets, or braces follow the base reader: atoms, lists, keywords
as in s-expression mode, and no indent-sensitive block detection.

```koskari
define sign [n]:
  if (< n 0):
    "negative"
  else:
    "positive"
```

The outer `define` block uses layout mode. The condition `(< n 0)` inside the
`if` header uses s-expression mode within the header argument.

Layout-block detection is suppressed inside delimiters regardless of
physical indentation. A `:` inside nested delimiters is never the structural
block opener for the enclosing layout header.

Body forms are read recursively: each body line is either a layout line, a new
sub-block, or an s-expression form, processed through the same layout reader.

## Logical lines and delimiter depth

The reader tracks delimiter depth while scanning. Encountering `(`, `[`, or
`{` increases depth; `)`, `]`, or `}` decreases it.

A physical newline ends a logical line only when delimiter depth is
0. Inside an unclosed delimiter, a newline is treated as whitespace and does
not end the header or layout-line scan.

This allows multi-line header arguments and expressions:

```koskari
let [[a 1000]
     [b 2000]]:
  (+ a b)
```

The header is one logical line; the body begins after the structural `:` and
the following newline.

## Colon in layout mode

At delimiter depth 0, `:` is a terminating character for header and
layout-line scans. The final `:` on a header line -- after which only
whitespace and/or a comment remain until the logical line ends -- is
structural: it opens the block body. It is not part of header content.

Spacing around the structural colon does not matter: `if x:` and `if x :` are
equivalent.

Keywords such as `as:`, `x:`, and `else:` in header arguments remain intact
when followed by more header content. Only the colon that terminates the header
is structural:

```koskari
foo x: 1 y: 2:
  (+ x y)
```

Here `x:` and `y:` are keyword atoms in the header; the last `:` ends the header.

Bare keyword atoms are valid header arguments at depth 0. A keyword that is the
last header argument needs a double colon so the structural terminator
remains distinct from the keyword's own trailing `:`:

```koskari
unknown as: x:
  y
;; -> (unknown as: x y)

unknown x::
  z
;; -> (unknown x: z)
```

Wrapping a keyword in a delimiter (`[...]` or `(...)`) is also valid -- inside
delimiters, s-expression mode applies -- but wrapping is not required for
keywords in header arguments.

Header-colon recognition is a single forward pass: a predicated `:` event
fires only when the remainder of the logical line is whitespace and/or a
comment. Peer gathering and quote/quasiquote predicates may speculate on the
stream separately; that is not a replay of the header scan.

## Layout blocks

A layout block consists of:

1. Header -- one or more forms read until the structural `:`
2. Structural colon -- the final `:` on the header logical line
3. Newline -- required; the body does not start on the same line as the header
4. Body -- forms at an indent strictly greater than the header line, until
   dedent or end of file

The first header form is the head; remaining forms before the colon are
args. The head may be any form (symbol, list, etc.), not only a bare symbol.

The first body line sets the body's indent, and every later content line of
that body must start at the same column; a top-level form starts at column 0.
A deeper line is `unexpected indent`, and a shallower line that is still inside
the block is `unindent does not match the enclosing block`. Both are
`ReaderSyntaxError` under the default `reader/indent` pragma; `warn` and
`ignore` read the line as a sibling form instead (see
[Pragmas](../guide/pragma.md)). Blank lines and comment-only lines are never
measured, and a line inside an open delimiter is a continuation, not a body
line.

```koskari
if x:
  y
```

Sub-blocks may appear in the body. Each is parsed as a `block:` cons.
Peer heads declared for the sub-block's layout (for example `else` after nested
`if`) are gathered on the same spine and expanded together when the enclosing
block is expanded.

### Invalid block shape

A header with no forms before the structural colon is an error. Content after
`:` on the same physical line means that colon is not structural (the
remainder is neither empty nor a comment), so the line is read as a layout
line -- often with a keyword atom -- rather than as a layout block. A structural
colon therefore requires only whitespace and/or a comment to end of line, then
a newline before the indented body.

## Peer blocks

Some layout handlers declare peer head symbols (for example `elif` and
`else` for [layout/if](layouts/layout-if.md)). After the primary block body,
the reader checks the next content at the same outer indent as the primary
header. If the line begins a block whose head is a declared peer, that block is
consumed as a peer. Peer blocks are expanded together with the primary block
through `apply_layouts`.

Peers are separate blocks: each peer has its own header, structural `:`, newline,
and indented body.

```koskari
if x:
  a
elif y:
  b
else:
  c
```

Blank lines and comment-only lines between a primary body and a peer
header do not prevent peer gathering.

Peer acceptance is defined per handler: only heads listed in the handler's peer
set are gathered. Syntax errors such as `elif` after `else` are rejected by
the handler (see [layout/if](layouts/layout-if.md)).

At the grammar level, peers are ordinary `BlockLine` nodes; special meaning is
entirely in the active layout handler.

## Layout lines

A layout line is a depth-0 logical line that does not end with a
structural block `:`. The reader collects the elements on the line until
`LINE_END` or EOF and keeps them as the line; the fold below is applied by
the layout handler that receives the line, or by `read()` for a top-level
line, through the reader's `fold_line`.

| Elements on line | Form (implicit calls enabled) |
| ---------------- | ----------------------------- |
| One element | That element |
| Several elements | `(first . rest...)` -- implicit call / fold |

Examples:

```koskari
foo bar
;; -> (foo bar)

(get-callee) a b
;; -> ((get-callee) a b)

if True 1 2
;; -> (if True 1 2)
```

The first form may be any value -- symbol, number, string, or parenthesized
form -- not only a bare symbol.

When `layout_implicit_calls` is false, a line with more than one form is a
syntax error (`multiple forms on one layout line`).

Layout lines are distinct from layout blocks: a line like `if test then else`
does not use peer blocks and cannot express `elif` chains.

## Block representation and layout expansion

Parsing and layout expansion are separate steps.

### Parse

A colon block is read as a `block:` cons: the header's elements as one
list, then one entry per body line, each entry either a line, the list of the
elements the reader saw on it, or a nested block:

```
(block: (HEAD ARG ...) ENTRY ...)
ENTRY ::= LINE | BLOCK
LINE  ::= (ELEMENT ...)
```

Nothing on a line is folded at parse time. `else: x` is a two-element line,
`(else: x)` is a one-element line holding a pair, and a nested block is its
own entry, so the handler that expands the block can tell them apart:

```koskari
foo:
  else: x
  bar:
    y
  (else: x)
;; (block: (foo) (else: x) (block: (bar) (y)) ((else: x)))
```

Stream-collected peer blocks (when the layout declares peer heads) form a spine
of `block:` conses that are expanded as one unit.

Block quote and quasiquote prefixes (`'head:` and `` `head: ``) read raw
`block:` conses in the header and body without expanding nested layout.
They are wrapped for `layout/quote` and `layout/quasiquote` to lower at
expansion time.

### Expand

`LayoutReader.read()` calls `apply_layout` (one block) or
`apply_layouts` (primary plus peers on a spine) from
`koskari.reader.layouts`, passing itself. Each lookup resolves
`layout/<head>` in the module namespace, then builtins. Handlers are
invoked as `read(reader, source, peers)`, where `reader` is the
`LayoutReader` expanding the source and `source` is
`(header . entries)`, the cdr of the block cons, with `header` =
`(head . args)`.

The line rule lives on the reader: `reader.fold_line(entry)` returns a
one-element line's element, folds several elements to a call, or raises when
`reader/implicit-calls` is off, and lowers a block entry to its form;
`reader.fold_lines(entries)` does the same for a whole body. Before a
handler runs, every nested block whose head is not a keyword has already been
lowered into a one-element line, with its peers gathered from the entries
after it; a block whose head is a keyword is left for the handler. A handler
folds where it wants forms and reads the elements directly where it does not,
so the default [layout/block](layouts/layout-block-default.md) can splice a
keyword line or a keyword-headed block as a keyword argument while leaving a
written `(else: x)` pair intact.

Expansion that starts from a namespace rather than a reader, as the runtime
`apply-layouts` builtin does for a filled quasiquote block, resolves the
module's reader through its bound `__reader__`, rebuilding one from
`__pragma__` when the load-time facet was detached.

When no named layout matches, `layout/block` splices body forms after the
header:

```koskari
foobar [x y z]:
  (print x y z)
;; -> (foobar [x y z] (print x y z))
```

For an explicit `compile/begin` wrapper, bind
`define layout/<head> layout/block-begin` in a `.ki` module. See
[layout/block-begin](layouts/layout-block-begin.md).

See [Layout handler reference](layouts/index.md) for the per-head catalogue.

## Pragmas

`LayoutReader` exposes three switches:

layout_enabled
: Default true. When false, the reader delegates to the inner s-expression reader and layout
  block/line rules do not apply.

layout_implicit_calls
: Default true. When false, a layout line with more than one top-level form is an error
  instead of folding to an implicit call.

layout_indent
: An `IndentMismatch` value, default `ERROR`: what a content line whose
  indent does not match its block does. `WARN` emits a `SyntaxWarning` and
  reads the line as a sibling form; `IGNORE` reads it as a sibling silently.

Module pragma objects may supply these via `reload_pragma` on the reader: the
pragma keys are `reader/layout`, `reader/implicit-calls`, and
`reader/indent` (they map to `layout_enabled`, `layout_implicit_calls`,
and `layout_indent`). These switches are intended for tooling and
pragma-driven reader configuration.

`reload_pragma` carries the suffix-syntax switches too -- `reader/index-brackets`,
`reader/dot-attrs`, and `reader/string-segments`, described under
[Suffix syntax](grammar.md#suffix-syntax).

One suffix interacts with layout. A string segment's gap crosses newlines, so a
continuation line is consumed as part of the value on the line that opened it,
and the block structure is unaffected. The gap skip stops at a dedent, so a
continuation line may not sit at column 0 inside a layout block -- that column
ends the logical line before the gap can reach the segment, and the reader
reports `string segment must stay indented within its block`. Any indentation
satisfies this, including less than the block's own body indent.

## Formal grammar

The grammar below describes surface syntax at layout depth 0. Layout head
resolution and peer sets are the operational rules stated earlier on this page, not encoded
in the EBNF productions.

### Terminals

- `ATOM` -- lexer atom (symbol, number, string, keyword token, etc.)
- `SEXPR` -- balanced delimited form parsed by the s-expression reader as one unit
- `:` -- structural header terminator at depth 0
- `NL` -- logical line break (physical newline at delimiter depth 0)
- `INDENT`, `DEDENT` -- indentation boundaries

### Productions

```text
Module        ::= Suite EOF ;

Suite         ::= { Line }* ;

Line          ::= BlankLine
               |  CommentLine
               |  BlockLine
               |  ExprLine ;

BlankLine     ::= NL ;
CommentLine   ::= NL ;   (* comment elided or skipped *)

BlockLine     ::= Header ':' NL INDENT Suite DEDENT ;

Header        ::= HeaderPart { HeaderPart } ;
HeaderPart    ::= ATOM | SEXPR ;

ExprLine      ::= Expr NL ;

Expr          ::= SEXPR
               |  LayoutExpr ;

LayoutExpr    ::= ExprPart { ExprPart } ;
ExprPart      ::= ATOM | SEXPR ;
```

### Operational rules (not in EBNF)

- Embedded s-expressions: When a `HeaderPart` or `ExprPart` begins with
  `(`, `[`, or `{`, the s-expression reader consumes a complete balanced form
  as one `SEXPR`. Newlines inside that form do not end the outer logical line.
- Layout head resolution: For `BlockLine` with header parts `H0 H1 ...`,
  expansion resolves `layout/<H0>` when `H0` is a symbol; otherwise
  `layout/block` applies.
- Peer blocks: Peers are `BlockLine` nodes at the same indent as the
  primary header, gathered only when the active handler declares their head
  symbols, then expanded with the primary through `apply_layouts`.
- Handler inputs: `source` = `(header . entries)`; `peers` = list of
  `(pheader . pentries)` pairs (peer headers and bodies before folding).

### Conforming examples

```koskari
let [[x 1] [y 2]]:
  (+ x y)
```

```koskari
let [[a 1000]
     [b 2000]]:
  (+ a b)
```

```koskari
if (& a 1):
  'foo
else:
  'bar
```

## See also

- [Koskari and s-expressions](../guide/layout-and-s-expressions.md) -- the rules beside their s-expressions
- [Layout handler reference](layouts/index.md) -- per-head lowering (`layout/if`, ...)
- [Grammar](grammar.md) -- syntax hub
