# Block Quasiquote Design

**Status:** Implemented
**Author:** Christopher O'Brien <obriencj@preoccupied.net>

**Superseded for reader layout behaviour:** [LAYOUTS.md](../LAYOUTS.md),
[docs/reference/reader-layout.md](../../docs/reference/reader-layout.md).
Macro finalize uses **`koskari.reader.layouts.apply_layout`**
(**`koskari.syntax._finalize_macro_output`**). Remaining sections are an
implementation archive; see **[SPECIALS.md](../SPECIALS.md)** §
compile/quasiquote-block for the current special.

---

## Background

Koskari's layout reader collapses indented block syntax into cons trees at
read time.  The layout handler for `if` discovers its `else` peer by peeking
at the character stream, then produces a `(compile/cond ...)` cons — all
before the compiler ever sees the form.

This is fine for ordinary code, but it made blocks opaque to quasiquote.
Writing:

```
`begin:
  ,@forms
```

previously produced:

```
(compile/begin (compile/quasiquote begin) ,@forms)
```

The backtick grabbed only the atom `begin` because `_read_quasiquote`
installed the `,` reader macro only for the duration of reading the single
next form.  The block colon and body were parsed later, outside any
quasiquote context, so `,@forms` was never recognised as an unquote-splicing
form.

The goal of this design is to allow whole blocks to be quoted and
quasi-quoted, including nested peer blocks such as `if`/`else`, while
keeping all existing non-quoted block behaviour unchanged.


---

## Approach: The `:block:` Sentinel Convention

Rather than introducing a new Python-level AST node type, we represent a
quoted block as an ordinary cons list tagged with the keyword `:block:`.
Because keywords are not callable, `:block:` can never appear as the head of
a normal call-site expression, making it a safe, unambiguous sentinel.  It is
also a public, documented protocol: user-written macros that want to
synthesize block structure may produce `:block:` cons lists directly.

### Canonical form

```
(:block: HEAD-ARGS BODY-EXPRS)
```

| Slot        | Type        | Meaning                                                   |
|-------------|-------------|-----------------------------------------------------------|
| `HEAD-ARGS` | proper list | `(head-symbol . header-args)`.  `car` is the head; `cdr` is any additional header arguments. |
| `BODY-EXPRS`| proper list | Ordered body elements.  Each element is either a normal form or another `:block:` cons (a nested sub-block). |

Both `HEAD-ARGS` and `BODY-EXPRS` are always proper lists (never bare forms),
so consumers always iterate them uniformly regardless of arity.

**Peers are not pre-grouped by the reader.**  Each block — whether a primary
block or a peer such as `else` — is encoded as an independent `:block:` cons.
Peer blocks appear as sibling elements in the enclosing body, in source order,
immediately following the block they are associated with.
`compile/apply-layout` is responsible for identifying peers by consulting
the layout's declared peer set at expansion time.

#### Examples

```
; begin:
;   foo
;   bar
(:block: (begin) (foo bar))

; if test:
;   result
(:block: (if test) (result))

; if test:
;   result
; else:
;   fallback
;
; The if and else blocks are siblings in whatever body contains them.
; Here shown as the body of an outer begin block:
(:block: (begin) ((:block: (if test) (result)) (:block: (else) (fallback))))
```

When `compile/apply-layout` processes the outer `begin` block it will walk
its body.  On encountering the `(:block: (if test) ...)` element it looks up
`layout/if`, sees that `else` is a declared peer, then checks whether the
immediately following sibling in the body is a `:block:` whose head is `else`.
If so, that sibling is consumed as a peer and removed from the body before the
`if` layout handler is invoked.

---

## Reader Behaviour

### `'head:` — block quote

A `'` immediately preceding a block head causes the entire block to be read
and encoded as a `:block:` cons wrapped in `compile/quote-block`.  Any
sub-blocks in the body are read recursively as raw `:block:` nodes without
layout expansion.

```
'begin:
  foo
  bar
```

Produces:

```
(compile/quote-block (:block: (begin) (foo bar)))
```

When compiled, `compile/quote-block` rewrites to `(compile/quote BLOCK-REPR)`
and the `:block:` cons is treated as opaque literal data, analogous to
`compile/quote` on an ordinary cons.

The `'` is registered in `LayoutReader._block_macros`; adding further
block-aware prefix characters requires only a new entry in that dict.

### `` `head: `` — block quasiquote

A `` ` `` immediately preceding a block head causes the entire block to be
read with the `,`/`,@` unquote reader macros active for all header arguments
and all body forms, including nested sub-blocks.  The result is tagged with
`compile/quasiquote-block`.

```
`begin:
  ,@forms
```

Produces:

```
(compile/quasiquote-block
  (:block: (begin) ((compile/unquote-splicing forms))))
```

A more complex example with peers:

```
`begin:
  if ,test-expr:
    ,result
  else:
    ,fallback
```

Produces:

```
(compile/quasiquote-block
  (:block:
    (begin)
    ((:block: (if (compile/unquote test-expr)) ((compile/unquote result)))
     (:block: (else)                           ((compile/unquote fallback))))))
```

The `if` and `else` blocks are siblings in the outer `begin` block's
`BODY-EXPRS`.  The reader does not group them; it simply encodes each block
independently in source order.  `compile/apply-layout` is responsible for
recognising at expansion time that `else` is a declared peer of `if` and
consuming it accordingly.

The unquote macros are activated inside `_read_quasiquote_block` via
`temporary_event_macro` before the header is replayed, so `,expr` and `,@expr`
are recognised throughout both the block header and body.

### Scoping rule

**A leading prefix applies to the whole block.**  If `'` or `` ` `` appears
as the first character of a block header line (before any other token), it
scopes the entire block including all peers.

**A prefix mid-header applies only to the immediately following form.**  If
`` ` `` appears as an argument inside a header (e.g. `` without `(who ,@hello) we: ``),
it behaves as the normal s-expression quasiquote: it wraps only the next
s-expression and the block body is not affected.

This distinction is determined by whether the prefix character is consumed
*before* `read_block_header` is called (leading case) or *inside* header
argument parsing (mid-header case).

---

## Compile Specials

### `compile/quote-block`

**Syntax:** `(compile/quote-block BLOCK-REPR)`

Where `BLOCK-REPR` is a `:block:` cons.  A purely transformative special:
rewrites to `(compile/quote BLOCK-REPR)` and returns that for normal compiler
dispatch.  The `:block:` cons is treated as opaque data — no layout expansion
takes place.

### `compile/apply-layout`

**Syntax:** `(compile/apply-layout BLOCK ...)`

Compile-time depth-first expansion of `:block:` cons literals through layout
handlers.  Peer blocks are consumed from subsequent arguments when the primary
layout declares peers.  Multiple results are wrapped in `compile/begin`.

1. Recursively expands any nested `:block:` elements in the body.
2. Looks up the layout handler for the block head via `compiler.find_layout`,
   falling back to `layout/block` with the synthetic header prepend.
3. Scans the following sibling arguments for declared peer blocks, consuming
   them.
4. Calls `layout_obj.read(source, peers)` with the expanded source cons.
5. If multiple top-level results exist, wraps them in `(compile/begin ...)`.
6. Compiles and returns the result.

### `compile/quasiquote-block`

**Syntax:** `(compile/quasiquote-block BLOCK-REPR)`

Where `BLOCK-REPR` is a `:block:` cons possibly containing
`compile/unquote` and `compile/unquote-splicing` sites.

A purely transformative special: rewrites to:

```
(compile/quasiquote (compile/apply-layout BLOCK-REPR))
```

and returns that cons for normal compiler dispatch.

#### Two-phase evaluation model

`compile/quasiquote-block` is a **macro-writing tool**.  Understanding it
requires distinguishing two compile-time moments:

**Phase 1 — slot fill-in (macro definition time).**
`compile/quasiquote` compiles the entire `(compile/apply-layout BLOCK-REPR)`
form as a quasi-quoted data structure.  When that compiled code is run (during
a macro expansion), it produces a cons:

```
(compile/apply-layout (:block: ... filled slots ...))
```

The `:block:` cons now has all `,expr` and `,@expr` sites replaced by their
runtime values.  This cons is a **source form**, not a final value.

**Phase 2 — layout expansion (macro expansion time).**
The compiler sees the produced `(compile/apply-layout ...)` cons and
dispatches it to `special_apply_layout`, which performs depth-first layout
expansion at compile time and returns the final compiled form.

This is the standard koskari "compile → run → source → compile" loop that
underlies the macro system.  `compile/quasiquote-block` does not require any
separate runtime expansion function; both phases operate within the compiler.

#### Nested quasiquote depth

`_qq_expr` recognises `compile/quasiquote-block` at depth > 0 and
reconstructs it as literal data, the same way it handles nested
`compile/quasiquote`.  This ensures nested quasiquote depth tracking works
correctly with no additional changes.

---

## Implementation

### `koskari/reader/parser_ng.py`

`LayoutReader` separates block parsing into independent phases:

- **`_build_block(stream, outer_indent, head, args)`** — builds the
  `(:block: HEAD-ARGS BODY-EXPRS)` cons without invoking layout handlers.
- **`read_block(stream, outer_indent, head, args)`** — calls `_build_block`;
  sets `_block_mode` so nested sub-blocks are also read as `:block:` nodes.
- **`_read_block_source(stream, outer_indent, head, args)`** — builds the
  `((head . args) . body)` source cons for layout handler consumption.
- **`read_layout_block(stream, outer_indent, head, args)`** — chains
  `_read_block_source` and `_apply_layout`; produces the layout-expanded form.
- **`read_form(stream)`** — unified four-branch entry point for all forms.
- **`_collect_peer_blocks(...)`** — stream-reading pass that gathers adjacent
  peer `:block:` forms by peeking ahead in the character stream.
- **`_peek_peer_block_head(...)`** — speculative peek to detect whether the
  next stream content is a peer block header.
- **`LayoutReader._block_macros`** — `{"'": _read_quote_block, "`": _read_quasiquote_block}`.

`_read_quasiquote_block` installs `,` and `,@` reader macros via
`temporary_event_macro`, replays the already-probed header forms inside that
context so unquote sites in the header are recognised, then calls `read_block`
for the body.

### `koskari/compile/specials/quasiquote.py`

Contains `special_quote_block` and `special_quasiquote_block`, both purely
transformative.  Also contains `_qq_expr` with its `compile/quasiquote-block`
depth-tracking branch.

### `koskari/compile/specials/apply_layout.py`

Contains `_is_block`, `_expand_body`, `_expand_block`, and
`special_apply_layout`.

### `koskari/compile/compiler.py`

`Compiler.find_layout` resolves `layout/<head>` symbols from the module
namespace, checking `isinstance(v, LayoutHandler)`.

### Layout handler signature

All layout handlers (`layout_block`, `layout_begin`, `layout_if`,
`layout_try`) use the `(source, peers)` signature.  Error reporting uses
`get_position(source)` / `get_position(peer)`.

---

## Tests

### Reader tests

Located in `tests/reader/layouts/test_quasiquote.py`.

- **`TestQuoteBlock`** — four cases: simple `'begin:`, empty-body quoted
  block, nested sub-blocks encoded as raw `:block:` nodes, and quoted block
  inside a normal `define` block body.
- **`TestQuasiquoteLayout`** — backtick quasiquote-block cases: simple
  `\`begin:` with unquote-splicing, peers (`if`/`else` with unquote sites in
  header and body), mid-header backtick leaving block body unaffected.

### Compiler tests

Located in `tests/compile/specials/test_apply_layout.py` and
`tests/compile/specials/test_quasiquote.py`.

**`test_apply_layout.py`**: `test_begin_block`, `test_if_block_no_peers`,
`test_if_else_blocks`, `test_unknown_head_falls_back_to_layout_block`,
`test_non_block_arg_raises`, `test_nested_body_blocks`,
`test_multiple_blocks_wrapped_in_begin`.

**`test_quasiquote.py`** — `TestQuasiquoteBlockShape`: verifies the shape
of the intermediate source cons produced by running compiled
`compile/quasiquote-block` code — confirms it is
`(compile/apply-layout (:block: ...))` with unquote slots filled.

**`test_quasiquote.py`** — `TestQuasiquoteBlockTwoPhase`: two-phase compile
— run phase 1 to get the intermediate source cons, compile and run phase 2
to get the final value.  Covers static, unquote, unquote-splicing, and
`if`/`else` peer cases with hand-built `:block:` cons inputs.

**`test_quasiquote.py`** — `TestQuasiquoteBlockFullStack`: same two-phase
flow starting from layout source text read via `LayoutReader`.

---

## Summary

| Concern                        | Before this design              | After this design                                        | Status   |
|--------------------------------|---------------------------------|----------------------------------------------------------|----------|
| Ordinary block parsing         | Reader → layout → cons          | Unchanged                                                | —        |
| `'` on a block head            | Wrapped only the head atom      | Entire block read as `:block:`, wrapped in `compile/quote-block` | **Done** |
| Backtick on a block head       | Only wraps the head atom        | Entire block read as `:block:`; `,`/`,@` active in header and body | **Done** |
| Block structure representation | Not preserved after read        | `:block:` cons (public protocol)                         | **Done** |
| Peer discovery (compile-time)  | Stream-time character peeking   | `compile/apply-layout` at compile time                   | **Done** |
| Peer discovery (macro expand)  | N/A                             | Two-phase: quasiquote produces source cons; compiler dispatches `compile/apply-layout` | **Done** |
| Layout handler signature       | `(reader, source, peers)`       | `(source, peers)`                                        | **Done** |
| `_block_macros` registry       | Hardcoded `'` check             | Extensible dict; `'` and `` ` `` pre-registered          | **Done** |
| Unquote inside block body      | Impossible                      | Natural via quasiquote-block; `,`/`,@` active throughout | **Done** |
| User macro block synthesis     | Not possible                    | Produce `:block:` cons directly                          | **Done** |
