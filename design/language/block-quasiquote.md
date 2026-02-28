# Block Quasiquote Design

**Status:** Partially Implemented
**Author:** Christopher O'Brien <obriencj@preoccupied.net>

---

## Background

Koskari's layout reader collapses indented block syntax into cons trees at
read time.  The layout handler for `if` discovers its `else` peer by peeking
at the character stream, then produces a `(compile/cond ...)` cons — all
before the compiler ever sees the form.

This is fine for ordinary code, but it makes blocks opaque to quasiquote.
Writing:

```
`begin:
  ,@forms
```

today produces:

```
(compile/begin (compile/quasiquote begin) ,@forms)
```

The backtick grabbed only the atom `begin` because `_read_quasiquote`
installs the `,` reader macro only for the duration of reading the single
next form.  The block colon and body were parsed later, outside any
quasiquote context, so `,@forms` was never recognized as an unquote-splicing
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

## Implementation Status

### Completed

#### Reader: raw block reading and `_block_macros` registry

`LayoutReader` now separates block parsing into two independent phases:

- **`_read_block_source`** — parse characters into `((head . args) . body)`
- **`_apply_layout`** — dispatch the parsed source cons through its layout
  handler and collect declared peer blocks

**`read_layout_block`** chains both phases.  **`read_block`** calls only the
first phase (via `_build_block`) and returns the `:block:` cons
without invoking any layout handler.

`LayoutReader` carries a **`_block_macros`** dict mapping prefix characters
to wrapper functions `(raw_block, position) -> cons`.  The `'` character is
pre-registered, mapping to a function that wraps the raw block in
`(compile/quote-block ...)`.  Adding `` ` `` for quasiquote-block requires
only one additional entry — no hardcoded character checks needed.

**`_block_mode`** is a boolean instance flag.  When `read_block` sets
it, `read_form` routes any detected sub-block through `read_block`
instead of `read_layout_block`, so the entire nested tree is encoded as `:block:`
nodes without layout expansion at any level.  `read_raw_block_body` has been
eliminated; `read_block_body` delegates uniformly to `read_form` and
adapts based on the flag.

#### Reader: unified `read_form`

`read_form` is the single dispatch entry point for all forms.  It has
four branches:

1. **S-expression** (`c in '([{'`) — delegates to `_read` with no block
   detection.
2. **Block-macro prefix** (`c in _block_macros`) — speculatively probes for
   `head:` syntax.  If a colon is found, calls `read_block` and passes
   the result to the registered wrapper function.  Falls back to `_read` for
   normal quoting if no colon follows.
3. **Keyword atom** (`is_keyword(form1)`) — handles the case where the atom
   reader consumed the trailing colon as part of the atom (e.g. `begin:`
   becomes `keyword('begin')`).  Converts back to a symbol and dispatches to
   the appropriate block reader directly.
4. **Bare atom with speculative probe** — reads `form1`, calls
   `skip_whitespace`, checks for dedent, then probes for a colon to
   distinguish a sub-block head from a plain form.

`read_block_body` is now a simple loop that calls `read_form` for each
body element, removing roughly 80 lines of duplicated probe logic.

#### Compile special: `compile/quote-block`

`special_quote_block` is registered in `koskari/compile/specials/quasiquote.py`
and exported from `koskari/compile/specials/__init__.py`.

**Syntax:** `(compile/quote-block BLOCK-REPR)`

Emits code that constructs the `:block:` cons structure as literal data at
runtime by reusing `_qq_expr` at quasiquote depth 0.  Analogous to
`compile/quote` on an ordinary cons; no layout expansion takes place.

#### Compile special: `compile/apply-layout`

`special_apply_layout` is implemented in
`koskari/compile/specials/apply_layout.py` and exported.

**Syntax:** `(compile/apply-layout BLOCK ...)`

Performs a depth-first expansion of one or more `:block:` cons literals
through the layout handler system at compile time:

1. Recursively expands any nested `:block:` elements in the body.
2. Looks up the layout handler for the block head via `compiler.find_layout`,
   falling back to `layout/block` with the synthetic header prepend.
3. Scans the following sibling arguments for declared peer blocks, consuming
   them.
4. Calls `layout_obj.read(source, peers)` with the expanded source cons.
5. If multiple top-level results exist, wraps them in `(compile/begin ...)`.
6. Compiles and returns the result.

`Compiler.find_layout` was added to mirror `find_compiled`, looking up
`layout/<head>` in the module namespace and checking `isinstance(v, LayoutHandler)`.

#### Layout handler signature: drop `reader`

All layout handlers (`layout_block`, `layout_begin`, `layout_if`,
`layout_try`) were updated from `(reader, source, peers)` to `(source, peers)`.
Error reporting now uses `get_position(source)` or `get_position(peer)` to
derive location data from cons pairs directly.  The `_layout_stream` instance
variable and all `reader`/proxy pass-through code were removed from the parser
and `apply_layout.py`.

### Not Yet Implemented

#### `compile/quasiquote-block`

The `` ` `` prefix reader macro entry in `_block_macros` has not been added.
The `compile/quasiquote-block` special has not been implemented.

When implemented, registering the macro requires only:

```python
self._block_macros["`"] = lambda raw, pos: cons(_symbol_quasiquote_block, raw, nil)
```

`special_quasiquote_block` should emit code equivalent to:

```
(compile/quasiquote (compile/apply-layout BLOCK-REPR))
```

The existing `compile/quasiquote` machinery handles all unquote and
unquote-splicing recursion through the `:block:` cons structure.  At runtime
the filled-in `:block:` cons is passed to `compile/apply-layout` (or an
equivalent runtime expander) to perform layout expansion.

#### Runtime expansion path

The design originally described `compile/expand-layouts` as a runtime special
(evaluated after quasiquote slot-filling).  The compile-time
`compile/apply-layout` covers the static case (no unquote sites).  The
runtime path — needed when `:block:` structures carry unquoted slots that are
only known at runtime — has not been implemented.

---

## New Reader Behaviour: `compile/quote-block` and `compile/quasiquote-block`

### `'head:` — block quote *(implemented)*

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

When compiled, `compile/quote-block` emits code that constructs the
`:block:` cons structure as literal data at runtime — analogous to
`compile/quote` on an ordinary cons.

The `'` is registered in `LayoutReader._block_macros`; adding further
block-aware prefix characters requires only a new entry in that dict.

### `` `head: `` — block quasiquote *(not yet implemented)*

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

### Scoping rule

**A leading prefix applies to the whole block.**  If `'` or `` ` `` appears
as the first character of a block header line (before any other token), it
scopes the entire block including all peers.

**A prefix mid-header applies only to the immediately following form.**  If
`` ` `` appears as an argument inside a header (e.g. `` without `(who ,@hello) we: ``),
it behaves as today: it wraps only the next s-expression.  The block body is
not affected.

This distinction is determined by whether the prefix character is consumed
*before* `read_block_header` is called (leading case) or *inside* header
argument parsing (mid-header case).

---

## New Compile Specials

### `compile/quote-block` *(implemented)*

**Syntax:** `(compile/quote-block BLOCK-REPR)`

Where `BLOCK-REPR` is a `:block:` cons.  Emits code that constructs the
`:block:` cons structure as a literal data value at runtime, analogous to
`compile/quote`.  No layout expansion takes place.

### `compile/apply-layout` *(implemented)*

**Syntax:** `(compile/apply-layout BLOCK ...)`

Compile-time depth-first expansion of `:block:` cons literals through layout
handlers.  Peer blocks are consumed from subsequent arguments when the primary
layout declares peers.  Multiple results are wrapped in `compile/begin`.

This is the compile-time analogue of the runtime `compile/expand-layouts`
described below.  The quasiquote-block special will emit an
`compile/apply-layout` call once slot-filling is complete.

### `compile/quasiquote-block` *(not yet implemented)*

**Syntax:** `(compile/quasiquote-block BLOCK-REPR)`

Where `BLOCK-REPR` is a `:block:` cons possibly containing
`compile/unquote` and `compile/unquote-splicing` sites.

This special expands in two phases:

**Phase 1 — slot fill-in.**  Emit code equivalent to:

```
(compile/quasiquote BLOCK-REPR)
```

The existing `compile/quasiquote` machinery handles all unquote and
unquote-splicing recursion through the `:block:` cons structure, filling in
runtime values wherever `,expr` and `,@expr` appear.  The result at runtime
is a fully-substituted `:block:` cons with all slots replaced by their
runtime values.

**Phase 2 — layout expansion.**  The filled-in `:block:` cons is passed to a
runtime call to `compile/apply-layout` (or an equivalent runtime expander).
It walks the structure, applies layout handlers, and returns the expanded cons
form.

Because phase 1 uses standard `compile/quasiquote`, nested quasiquote depth
tracking (level > 0) works correctly with no additional changes.

### `compile/expand-layouts` *(not yet implemented)*

**Syntax:** `(compile/expand-layouts EXPR)`

A runtime function that accepts any value.  If the value is not a `:block:`
cons, it is returned unchanged.  If it is a `:block:` cons, applies the same
depth-first expansion algorithm as `compile/apply-layout` but operating over
an in-memory cons list rather than a stream.

This is required for the runtime path used by `compile/quasiquote-block`,
where the `:block:` structure is only fully materialised at runtime after
quasiquote slot-filling.

---

## Implementation Touchpoints

### `koskari/reader/parser.py` *(implemented)*

The two-phase split, `_block_macros` registry, unified `read_form`,
and `_block_mode` flag are all in place.  Key methods:

- **`_apply_layout(stream, outer_indent, source)`** — extracted from
  `read_layout_block`; applies layout handler + peer collection to an already-parsed
  source cons.
- **`read_block(stream, outer_indent, head, args)`** — reads a block
  without layout expansion; sets `_block_mode` so nested sub-blocks are
  also read as blocks.
- **`_build_block(stream, outer_indent, head, args)`** — builds the
  `(:block: HEAD-ARGS BODY-EXPRS)` cons.
- **`read_layout_block(stream, outer_indent, head, args)`** — chains
  `_read_block_source` and `_apply_layout`; produces the layout-expanded form.
- **`read_form(stream)`** — unified four-branch entry point; delegates
  `read_block_body` body elements here, removing all duplicated probe logic.
- **`LayoutReader._block_macros`** — `{"'": _read_quote_block, "\`": _read_quasiquote_block}`.

No changes are required to `read_block_header`.  The quasiquote-block path
is handled by `_read_quasiquote_block`, which installs the `,`/`,@`
reader macros before calling `read_block`.

### `koskari/compile/specials/apply_layout.py` *(implemented)*

Contains `_is_block`, `_expand_body`, `_expand_block`, and
`special_apply_layout`.

### `koskari/compile/specials/quasiquote.py` *(partially implemented)*

`special_quote_block` is implemented.  `special_quasiquote_block` is not.

### `koskari/compile/compiler.py` *(implemented)*

`Compiler.find_layout` resolves `layout/<head>` symbols from the module
namespace with a lazy import of `LayoutHandler` to avoid circular imports.

### `koskari/reader/layouts/conditionals.py`, `exceptions.py` *(implemented)*

All layout handlers updated to `(source, peers)` signature; error reporting
uses `get_position(source)` / `get_position(peer)`.

### `koskari/compile/specials/__init__.py` *(implemented)*

Exports `special_apply_layout` and `special_quote_block`.

---

## Test Plan

### Reader tests *(partially implemented)*

Located in `tests/reader/layouts/test_quasiquote.py`.

- **`TestQuoteBlock`** *(implemented)* — four cases: simple `'begin:`,
  empty-body quoted block, nested sub-blocks encoded as raw `:block:` nodes,
  and quoted block inside a normal `define` block body.
- **`skipme_test_quote_block`** — backtick quasiquote-block case; left skipped
  pending `compile/quasiquote-block` implementation.
- **`test_quasiquote_block_with_peers`** *(not yet written)* — verify the
  `if`/`else` sibling representation.
- **`test_mid_header_backtick_unaffected`** *(not yet written)* — verify that
  a mid-header backtick wraps only the next s-expression.

### Compiler tests *(implemented)*

Located in `tests/compile/specials/test_apply_layout.py`.

- `test_begin_block`, `test_if_block_no_peers`, `test_if_else_blocks`,
  `test_unknown_head_falls_back_to_layout_block`, `test_non_block_arg_raises`,
  `test_nested_body_blocks`, `test_multiple_blocks_wrapped_in_begin` — all
  passing.

### Compiler tests *(not yet written)*

- `test_quote_block_compile` — verify that `compile/quote-block` emits code
  producing a `:block:` cons at runtime.
- `test_quasiquote_block_runtime` — a function using `` `begin: ,@forms ``
  evaluates correctly when called with a list of forms.

---

## Open Questions

1. **Runtime expansion for `compile/quasiquote-block`.**  The compile-time
   `compile/apply-layout` covers the static case.  The runtime path needs
   a Python-level `expand_layouts` function (or equivalent) that the emitted
   code can call after quasiquote slot-filling.  The function would use the
   same `LayoutHandler` objects and `LAYOUTS` registry already in place.

2. **Activating unquote macros inside `read_block`.**  Currently
   `read_block` reads sub-blocks without activating `,`/`,@`.  For
   quasiquote-block, the reader installs the unquote macros before
   entering `read_block` and restores them afterwards via the
   `temporary_event_macro` context manager.  This is handled by
   `_read_quasiquote_block`.

3. **`:block:` in non-quoted positions.**  If a user places a raw `:block:`
   cons in non-quoted source (e.g. hand-assembled data passed to
   `compile/apply-layout` outside of a quasiquote context), the compiler
   will process it.  This is intentional — it is the open user protocol —
   but should be documented clearly.

4. **`compile/quote-block` vs `compile/quote`.**  `compile/quote-block` is
   kept as a distinct special (rather than aliasing `compile/quote`) to allow
   clearer error messages and potential future optimisations.

5. **Nested quasiquote depth.**  `_qq_expr` already tracks nesting depth.
   `compile/quasiquote-block` at depth > 0 should reconstruct the
   `compile/quasiquote-block` form as literal data rather than expanding it.
   This requires `_qq_expr` to recognise `compile/quasiquote-block` the same
   way it currently recognises `compile/quasiquote`.

---

## Summary

| Concern                        | Before this design              | After this design                        | Status       |
|--------------------------------|---------------------------------|------------------------------------------|--------------|
| Ordinary block parsing         | Reader → layout → cons          | Unchanged                                | —            |
| `'` on a block head            | Wrapped only the head atom      | Entire block read as `:block:`, wrapped in `compile/quote-block` | **Done** |
| Backtick on a block head       | Only wraps the head atom        | *(not yet)* Wraps entire block; `,`/`,@` active | Pending |
| Block structure representation | Not preserved after read        | `:block:` cons (public protocol)         | **Done** |
| Peer discovery (compile-time)  | Stream-time character peeking   | `compile/apply-layout` at compile time   | **Done** |
| Peer discovery (runtime)       | N/A                             | *(not yet)* `compile/expand-layouts`     | Pending |
| Layout handler signature       | `(reader, source, peers)`       | `(source, peers)`                        | **Done** |
| `_block_macros` registry       | Hardcoded `'` check             | Extensible dict; `'` pre-registered      | **Done** |
| Unquote inside block body      | Impossible                      | *(not yet)* Natural via quasiquote       | Pending |
| User macro block synthesis     | Not possible                    | Produce `:block:` cons directly          | **Done** |
