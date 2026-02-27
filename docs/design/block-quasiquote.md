# Block Quasiquote Design

**Status:** Proposed
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
`compile/expand-layouts` is responsible for identifying peers by consulting
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

When `compile/expand-layouts` processes the outer `begin` block it will walk
its body.  On encountering the `(:block: (if test) ...)` element it looks up
`layout/if`, sees that `else` is a declared peer, then checks whether the
immediately following sibling in the body is a `:block:` whose head is `else`.
If so, that sibling is consumed as a peer and removed from the body before the
`if` layout handler is invoked.

---

## New Reader Behaviour: `compile/quote-block` and `compile/quasiquote-block`

### `'head:` — block quote

A `'` immediately preceding a block head (i.e. the `'` is the first
character on the block's header line, before any symbol) causes the entire
block to be read and encoded as a `:block:` cons tagged with
`compile/quote-block`.  Any following peer blocks (e.g. `else`) are read as
independent `:block:` nodes and appear as subsequent siblings in whatever body
contains the quoted block — exactly as they would appear in source order.

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
independently in source order.  `compile/expand-layouts` is responsible for
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

### `compile/quote-block`

**Syntax:** `(compile/quote-block BLOCK-REPR)`

Where `BLOCK-REPR` is a `:block:` cons.  Emits code that constructs the
`:block:` cons structure as a literal data value at runtime, analogous to
`compile/quote`.  No layout expansion takes place.

### `compile/quasiquote-block`

**Syntax:** `(compile/quasiquote-block BLOCK-REPR)`

Where `BLOCK-REPR` is a `:block:` cons possibly containing
`compile/unquote` and `compile/unquote-splicing` sites.

This special expands in two phases:

**Phase 1 — slot fill-in.**  Emit code equivalent to:

```
(compile/quasiquote (compile/expand-layouts BLOCK-REPR))
```

The existing `compile/quasiquote` machinery handles all unquote and
unquote-splicing recursion through the `:block:` cons structure, filling in
runtime values wherever `,expr` and `,@expr` appear.  The result at runtime
is a fully-substituted `:block:` cons with all slots replaced by their
runtime values.

**Phase 2 — layout expansion.**  The `compile/expand-layouts` special
(described below) is called at runtime with the filled-in `:block:` cons.
It walks the structure, applies layout handlers, and returns the expanded
cons form.

Because phase 1 uses standard `compile/quasiquote`, nested quasiquote depth
tracking (level > 0) works correctly with no additional changes.

### `compile/expand-layouts`

**Syntax:** `(compile/expand-layouts EXPR)`

A runtime function that accepts any value.  If the value is not a `:block:`
cons, it is returned unchanged.  If it is a `:block:` cons, the following
algorithm is applied:

1. Destructure: extract `HEAD-ARGS` and `BODY-EXPRS`.
2. `head = car(HEAD-ARGS)`.  `header-args = cdr(HEAD-ARGS)`.
3. Look up `layout/<head>` using the standard layout namespace lookup.
4. **Peer extraction:** if the resolved layout has a non-empty `peers`
   declaration, walk `BODY-EXPRS` left to right.  Each time an element is a
   `:block:` cons, check whether its head appears in the current layout's
   declared peer set:
   - If it does **not** match a peer, it belongs to the current block's body.
     Recursively apply `expand-layouts` to it and add the result to the
     working body list.
   - If it **does** match a peer, stop adding to the body; switch to consuming
     peers.  Continue scanning: each subsequent `:block:` element whose head
     is in the peer set is itself recursively expanded (its own body may
     contain further sub-blocks) and appended to the peer list.  Stop when
     the end of `BODY-EXPRS` is reached or an element is encountered that is
     not a matching peer block.
   This mirrors the stream-based peer discovery the reader performs today, but
   operates over an in-memory cons list.  Non-block body elements are always
   treated as body forms and are never consumed as peers.
5. Build a source cons matching the form layout handlers currently receive:
   `((head . header-args) . body-forms...)`.  Each peer is similarly
   structured as `((peer-head . peer-args) . peer-body-forms...)`.
6. Invoke the layout handler: `layout_obj.read(reader_proxy, source, peers)`.
   A `reader_proxy` is a lightweight stand-in that satisfies whatever the
   layout handler accesses on the reader (currently only `_layout_stream`
   for error reporting; this can be a null stream returning a sentinel
   position).
7. Return the handler's result.

If no layout handler is found for `head`, the default `layout/block`
fallback is applied, exactly as the reader does today.

Note that `expand-layouts` is a plain Python function invoked at runtime (not
a compile-time macro), because the `:block:` structure it processes is
produced by quasiquote slot-filling and is not statically known at compile
time.

---

## Implementation Touchpoints

### `koskari/reader/parser.py`

#### The raw-block reading gap

The current `read_block_body` always calls `self.read_block(...)` when it
detects a sub-block, and `read_block` always applies layout expansion.  There
is no existing path that reads a block and returns a raw `:block:` cons
instead.  This gap must be filled before the rest of the design can work.

**New method: `LayoutReader.read_raw_block(stream, outer_indent, head, args)`**

Mirrors `read_block` but never invokes a layout handler and never calls
`_read_declared_peers`.  Instead it:

1. Calls `_read_block_source` to collect `(header . body)` exactly as
   `read_block` does today.
2. Recursively reads each body element that turns out to be a sub-block using
   `read_raw_block` rather than `read_block`, so the entire nested tree is
   encoded as `:block:` nodes with no layout expansion at any level.
3. Returns `(:block: HEAD-ARGS BODY-EXPRS)`.

**New method: `LayoutReader.read_raw_block_body(stream, outer_indent)`**

Mirrors `read_block_body` but, in the sub-block detection branch, calls
`read_raw_block` instead of `read_block`.  Also activates the `,`/`@` reader
macros if the enclosing context is a quasiquote-block (passed as a flag or
via a reader instance variable).

These two methods form the raw reading path.  All existing methods are
unchanged.

#### Detecting the leading prefix

**`LayoutReader.read_layout_form`** — before calling `read_block_header`,
peek at the first non-whitespace character.  If it is `'` or `` ` ``,
consume it, record which prefix was seen, and route to `read_raw_block`
rather than `read_block`.

**Sub-block detection in `LayoutReader.read_raw_block_body`** — the same
speculative probe logic as `read_block_body`, but when a colon is found, it
calls `read_raw_block` instead of `read_block`.  It also peeks for a leading
`'` or `` ` `` before the sub-block head token so that explicitly-quoted
sub-blocks within a raw body are handled (though this is an edge case; most
sub-block content inside a quoted outer block should simply be raw).

#### Assembling the result

When `read_raw_block` has collected `HEAD-ARGS` and `BODY-EXPRS`, it wraps
them in `(:block: HEAD-ARGS BODY-EXPRS)`.  The caller (`read_layout_form` or
`read_raw_block_body`) then wraps that in `(compile/quote-block ...)` or
`(compile/quasiquote-block ...)` at the outermost level only.  Inner
`:block:` nodes (sub-blocks of the quoted block) are stored bare inside
`BODY-EXPRS` — they are not individually wrapped in quote/quasiquote-block,
because the outer wrapper already covers the whole tree.

No changes are required to any layout handler.  No changes are required to
`read_block`, `read_block_body`, or `read_block_header` for the ordinary
(non-quoted) case.

### `koskari/compile/specials/quasiquote.py`

Add `special_quote_block`, `special_quasiquote_block`, and
`special_expand_layouts`.

`special_quasiquote_block` emits code equivalent to invoking
`compile/quasiquote` on `(compile/expand-layouts BLOCK-REPR)`.  It can be
implemented by constructing that wrapper cons and delegating to
`special_quasiquote`, keeping the implementation small.

`special_expand_layouts` is a runtime special: it compiles its single
argument, emits a call to the `expand_layouts` Python function, and returns
the result fragment.  Alternatively it can be implemented as a macro that
expands at compile time if the `:block:` structure is statically known —
but this optimisation is not required for correctness.

`expand_layouts` (a plain Python function called at runtime) implements the
algorithm described in the `compile/expand-layouts` section above, using the
same `LayoutHandler` objects and `LAYOUTS` registry already in place.

### `koskari/compile/specials/__init__.py`

Import and register the three new specials.

---

## Test Plan

### Reader tests

Located in `tests/reader/layouts/test_quasiquote.py`.

1. **`test_quote_block`** (existing, currently failing) — verify that
   `` `begin: \n  ,@hello `` parses to
   `(compile/quasiquote-block (:block: (begin) ((compile/unquote-splicing hello))))`.
2. **`test_quasiquote_block_with_peers`** — verify that the `if`/`else`
   example from this document parses to a flat sibling representation: the
   `if` and `else` `:block:` nodes appear as consecutive elements of the
   outer `begin` block's `BODY-EXPRS`, not nested inside one another.
3. **`test_block_quote`** — verify that `'begin: \n  foo` parses to
   `(compile/quote-block (:block: (begin) (foo)))`.
4. **`test_mid_header_backtick_unaffected`** — verify that
   `` without `(who ,@hello) we: `` still parses as today (mid-header
   backtick wraps the s-expression only, does not enter block-quote mode).
5. **`test_raw_block_body_sub_blocks`** — verify that sub-blocks nested
   inside a quoted outer block are themselves encoded as raw `:block:` nodes
   (via `read_raw_block`) rather than being layout-expanded, and that they
   appear as siblings rather than being grouped by peer relationship.

### Compiler tests

Located in `tests/compile/specials/` (new file `test_quasiquote_block.py`).

1. **`test_expand_layouts_passthrough`** — `compile/expand-layouts` on a
   non-`:block:` value returns it unchanged.
2. **`test_expand_layouts_simple_block`** — `compile/expand-layouts` on a
   `(:block: (begin) (foo bar))` returns `(compile/begin foo bar)`.
3. **`test_expand_layouts_if_else`** — `compile/expand-layouts` on the
   `if`/`else` `:block:` structure returns a `(compile/cond ...)` form.
4. **`test_quasiquote_block_runtime`** — a function using `` `begin: ,@forms ``
   evaluates correctly when called with a list of forms.

---

## Open Questions

1. **Reader proxy for error reporting.**  Layout handlers currently access
   `reader._layout_stream` for position information when raising `LayoutError`.
   `compile/expand-layouts` runs at runtime without a stream.  The proposed
   solution (a null-stream proxy returning a sentinel position) is sufficient
   for now, but error messages will lack source location.  A future refinement
   could attach position metadata to `:block:` nodes.

2. **`:block:` in non-quoted positions.**  If a user accidentally places a raw
   `:block:` cons in non-quoted source (e.g. hand-assembled data passed to
   `compile/expand-layouts` outside of a quasiquote context), the compiler
   will process it.  This is intentional — it is the open user protocol — but
   should be documented clearly.

3. **`compile/quote-block` optimisation.**  `compile/quote-block` could be
   implemented as a simple alias for `compile/quote` since a `:block:` cons is
   just data.  The distinction is kept as a separate special to allow clearer
   error messages and potential future optimisations (e.g. stripping position
   metadata from quoted blocks).

4. **Nested quasiquote depth.**  The `_qq_expr` function already tracks nesting
   depth to handle `(compile/quasiquote ...)` forms correctly at depth > 0.
   `compile/quasiquote-block` at depth > 0 (i.e. a quasi-block inside a
   quasi-quote) should reconstruct the `compile/quasiquote-block` form as
   literal data rather than expanding it.  This requires `_qq_expr` to
   recognise `compile/quasiquote-block` the same way it currently recognises
   `compile/quasiquote`.  This is a small, localised change.

---

## Summary

| Concern                       | Before this design              | After this design                        |
|-------------------------------|---------------------------------|------------------------------------------|
| Ordinary block parsing        | Reader → layout → cons          | Unchanged                                |
| Backtick on a block head      | Only wraps the head atom        | Wraps entire block; `,`/`,@` active      |
| Block structure representation| Not preserved after read        | `:block:` cons (public protocol)         |
| Peer discovery                | Stream-time character peeking   | Expansion-time, from sibling `:block:` nodes |
| Unquote inside block body     | Impossible                      | Natural via standard quasiquote          |
| Layout expansion of QQ result | N/A                             | `compile/expand-layouts` at runtime      |
| User macro block synthesis    | Not possible                    | Produce `:block:` cons directly          |
