# Reader and layout syntax

Normative rules for how the Koskari layout reader turns source text into form
trees. For motivation and the relationship between layout and s-expressions,
see [Layout and s-expressions](concepts/layout-and-s-expressions.md).

---

## Overview

The **layout reader** (`LayoutReader`) is the default reader for Koskari
modules. It accepts indentation-based **blocks** and **lines** at delimiter
depth 0, embeds ordinary **s-expressions** inside `(`, `[`, and `{`, and
delegates block heads to **layout handlers** (`layout/<head>`) that lower block
source into list forms for the compiler.

Reading is strict: malformed layout or punctuation fails before evaluation.

---

## Layout mode and s-expression mode

**Layout mode** applies at **delimiter depth 0** — outside all `(`, `[`, and
`{`. A module file or REPL line at the top level uses layout rules for blocks
and lines.

**S-expression mode** applies inside any open delimiter. Forms nested in
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

Layout-block detection is **suppressed** inside delimiters regardless of
physical indentation. A `:` inside nested delimiters is never the structural
block opener for the enclosing layout header.

Body forms are read recursively: each body line is either a layout line, a new
sub-block, or an s-expression form, processed through the same layout reader.

---

## Logical lines and delimiter depth

The reader tracks **delimiter depth** while scanning. Encountering `(`, `[`, or
`{` increases depth; `)`, `]`, or `}` decreases it.

A **physical newline** ends a **logical line** only when delimiter depth is
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

---

## Colon in layout mode

At delimiter depth 0, `:` is a **terminating** character for header and
layout-line scans. The **final** `:` on a header line — after which only
whitespace and/or a comment remain until the logical line ends — is
**structural**: it opens the block body. It is not part of header content.

Spacing around the structural colon does not matter: `if x:` and `if x :` are
equivalent.

Keywords such as `as:`, `x:`, and `else:` in header **arguments** remain intact
when followed by more header content. Only the colon that terminates the header
is structural:

```koskari
foo x: 1 y: 2:
  (+ x y)
```

Here `x:` and `y:` are keyword atoms in the header; the last `:` ends the header.

Because `:` is terminating at depth 0, bare keyword atoms cannot appear as
header arguments at the top level. Wrap them in a delimiter:

```koskari
with [(foo) as: x]:
  ...
```

Inside `[...]`, s-expression mode applies and `as:` is a valid keyword.

The parser uses a **single forward pass** with `:` as a structural event at
header boundaries; there is no probe or replay pass.

---

## Layout blocks

A **layout block** consists of:

1. **Header** — one or more forms read until the structural `:`
2. **Structural colon** — the final `:` on the header logical line
3. **Newline** — required; the body does not start on the same line as the header
4. **Body** — forms at an indent **strictly greater** than the header line, until
   dedent or end of file

The first header form is the **head**; remaining forms before the colon are
**args**. The head may be any form (symbol, list, etc.), not only a bare symbol.

```koskari
if x:
  y
```

Sub-blocks may appear in the body. Each sub-block is read through the full
layout path (including handler resolution and peers) before its result becomes
a body element.

### Invalid block shape

A header with no forms before the structural colon is an error. Body content
on the same physical line as the header colon is not layout block syntax — the
grammar requires a newline before the body suite.

---

## Peer blocks

Some layout handlers declare **peer** head symbols (for example `elif` and
`else` for [layout/if](layouts/layout-if.md)). After the primary block body,
the reader checks the next content at the same **outer indent** as the primary
header. If the line begins a block whose head is a declared peer, that block is
consumed as a peer and passed to the handler's `read(source, peers)` hook.

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

**Blank lines** and **comment-only lines** between a primary body and a peer
header do not prevent peer gathering.

Peer acceptance is defined per handler: only heads listed in the handler's peer
set are gathered. Syntax errors such as `elif` after `else` are rejected by
the handler (see [layout/if](layouts/layout-if.md)).

At the grammar level, peers are ordinary `BlockLine` nodes; special meaning is
entirely in the active layout handler.

---

## Layout lines

A **layout line** is a depth-0 logical line that does **not** end with a
structural block `:`. The reader collects top-level forms on the line until
`LINE_END` or EOF.

| Forms on line | Result (implicit calls enabled) |
| ------------- | --------------------------------- |
| One form | That form |
| Multiple forms | `(first . rest…)` — implicit call / fold |

Examples:

```koskari
foo bar
;; → (foo bar)

(get-callee) a b
;; → ((get-callee) a b)

if True 1 2
;; → (if True 1 2)
```

The first form may be any value — symbol, number, string, or parenthesized
form — not only a bare symbol.

When **`layout_implicit_calls`** is false, a line with more than one form is a
syntax error (`multiple forms on one layout line`).

Layout lines are distinct from layout blocks: a line like `if test then else`
does not use peer blocks and cannot express `elif` chains.

---

## Layout handler resolution

For each block, the reader builds **source** = `(header . body)` where
`header` = `(head . args)` and `body` is a list of body forms. It then resolves
a layout handler:

1. Look up `layout/<head>` in module globals, then builtins, when `head` is a
   symbol
2. If found and it is a `LayoutHandler`, call `read(source, peers)`
3. If not found, use default **`layout/block`**, which wraps the body in
   `compile/begin` and appends it after the head and args:

```koskari
foobar [x y z]:
  (print x y z)
;; → (foobar [x y z] (compile/begin (print x y z)))
```

**`layout/flat`** splices body forms into the header list with no
`compile/begin` wrapper. Many heads only need that shape. In `.ki` modules you
can alias handlers:

```koskari
define layout/my-head layout/flat
```

See [Layout handler reference](layouts/index.md) for `:block:` data,
`apply_layout`, and the per-head catalogue.

---

## Pragmas

`LayoutReader` exposes two switches (defaults both **true**):

**layout_enabled**
: When false, the reader delegates to the inner s-expression reader and layout
  block/line rules do not apply.

**layout_implicit_calls**
: When false, a layout line with more than one top-level form is an error
  instead of folding to an implicit call.

Module pragma objects may supply `layout_implicit_calls` via `reload_pragma` on
the reader. These switches are intended for tooling and future pragma-driven
reader configuration.

---

## Formal grammar

The grammar below describes **surface syntax** at layout depth 0. Layout head
resolution and peer sets are operational rules (see sections above), not encoded
in the EBNF productions.

### Terminals

- `ATOM` — lexer atom (symbol, number, string, keyword token, etc.)
- `SEXPR` — balanced delimited form parsed by the s-expression reader as one unit
- `:` — structural header terminator at depth 0
- `NL` — logical line break (physical newline at delimiter depth 0)
- `INDENT`, `DEDENT` — indentation boundaries

### Productions

```ebnf
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

- **Embedded s-expressions:** When a `HeaderPart` or `ExprPart` begins with
  `(`, `[`, or `{`, the s-expression reader consumes a complete balanced form
  as one `SEXPR`. Newlines inside that form do not end the outer logical line.
- **Layout head resolution:** For `BlockLine` with header parts `H0 H1 …`,
  when `H0` is a symbol the reader may resolve `layout/<H0>` and delegate
  lowering; otherwise default block behaviour applies.
- **Peer blocks:** Peers are `BlockLine` nodes at the same indent as the
  primary header, gathered only when the active handler declares their head
  symbols.
- **Block source shape:** Handlers receive `source` = `(header . body)` and
  `peers` = list of `(pheader . pbody)` pairs.

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

---

## See also

- [Layout and s-expressions](concepts/layout-and-s-expressions.md) — mental model and three surfaces
- [Layout handler reference](layouts/index.md) — per-head lowering (`layout/if`, …)
- [Grammar](grammar.md) — syntax hub
