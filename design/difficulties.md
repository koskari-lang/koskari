# Difficulties porting term_graphics.py to Koskari

Notes from porting `koskari/cli/term_graphics.py` to
`koskari/cli/term_graphics.ki`. Everything below is a piece of syntax or
semantics that took more than one lookup against the corpus MCP to get
right, or that I got wrong on the first attempt and had to fix after
seeing a real failure (reader error or test failure). Ordered roughly
by how much trouble it caused.

## 1. Ambiently-async functions returning coroutines to Python callers

By far the biggest one. A plain `define`d Koskari function, called from
ordinary synchronous Python code (a `unittest` test, or `host.py`'s
non-async wrapper), does not return its value -- it returns an
unawaited `koskari.lib.Coroutine` object. Nothing about the function's
own syntax signals this; the first sign was every test in the file
failing with `TypeError: expected string or bytes-like object, got
'koskari.lib.Coroutine'`.

The fix is `define-demanding NAME FORMALS BODY...`, which expands to
`(define NAME (demand (lambda NAME FORMALS BODY...)))` -- `demand`
forces synchronous resolution so a caller with no event loop gets the
plain value back. All four public functions in the module needed this
instead of plain `define`. There is no syntactic marker on a *call site*
that tells you a function needs this; you only find out by having a
plain-Python caller (which is exactly the situation this whole module is
in, since it exists to be called from `host.py`).

## 2. Misunderstanding what the trailing colon actually means

I initially treated `define NAME` followed by an indented value as
unconditionally invalid, and separately treated `define NAME:` (with a
colon) as some special, uncertain form I couldn't vouch for without
running it. Both readings were wrong in the same way: I was thinking of
the colon as a block marker that opens an opaque scope, rather than
what it actually is -- a signal that the call isn't finished yet. A
header line with no trailing colon has already completed as its own
form (`define NAME` alone is `(define NAME)`, full stop), so anything
indented beneath it is a disconnected sibling form, not an argument --
that's the real trap, and I wrote it by muscle memory twice while
drafting this module (it's exactly how you'd write a multi-line
assignment in Python):

```koskari
; wrong -- (define parts) then a stray, disconnected list-each form
define parts
    list-each [start (range 0 total kitty-chunk-size)]:
      ...
```

A header line ending in `:` is exactly the opposite: it means "keep
reading arguments from the following indented lines," and each of those
lines folds into the header's call the same way, recursively, for any
header at all -- `define`, `set!`, or anything else, not just the forms
(`if`, `try`, `list-each`, ...) that happen to have dedicated layout
handlers. Once I had this the right way round, `define parts:` followed
by an indented `list-each [...]:` was no longer ambiguous: it's just
`(define parts (list-each [...] ...))`, exactly as if I'd written it
fully parenthesized, because it *is* that, mechanically -- not a
distinct block-form requiring separate confirmation.

## 3. Reaching for an explicit `begin` I didn't need

Because I was modeling the colon as "opens a block" rather than
"continues this call," I assumed a multi-statement `list-each` body
(several `define`s building a chunk, then a final expression) couldn't
be attached to an enclosing `define` without first wrapping it in an
explicit `(begin ...)` inside parens:

```koskari
; more machinery than necessary
define parts (list-each [start (range 0 total kitty-chunk-size)]
  (begin
    (define stop (+ start kitty-chunk-size))
    (define chunk (item-slice payload start: start stop: stop))
    ...
    f"\x1b_G{keys};{chunk}\x1b\\"))
```

It doesn't need that. `list-each [start (range ...)]:` followed by
several indented lines already collapses those lines into `list-each`'s
own argument list (`(list-each [start ...] arg1 arg2 arg3 ...)`); it's
`list-each`'s own macro that then folds multiple body arguments into a
`begin` internally, not something that has to be spelled out at the
call site:

```koskari
define parts:
    list-each [start (range 0 total kitty-chunk-size)]:
      define stop (+ start kitty-chunk-size)
      define chunk (item-slice payload start: start stop: stop)
      ...
      f"\x1b_G{keys};{chunk}\x1b\\"
```

The whole of layout mode reduces to one rule applied recursively --
each line, and each line's sub-lines, goes through the same "multiple
tokens on a line become an implicit call" transform -- plus a handful
of specially-defined layout rules (like `if`/`else` combining across
peer lines into a `cond`) layered on top for forms that want tighter
surface syntax. Absent one of those special rules, indentation is just
parenthesization by another name.

## 4. Chained dot-suffix calls don't do what they look like they do

`override.strip.lower` looks like "call strip, then call lower on the
result" by analogy with Python. It is not that. Per the suffix rules,
dots attach as plain attribute reads and parentheses are what turn a
value into a call -- so `override.strip.lower` reads as
`(attr (attr override strip) lower)`: an attribute lookup on the
*unbound method object* `override.strip`, never actually calling
anything. To call each step you have to parenthesize each call and let
the next dot suffix onto the *result*:

```koskari
((override.strip).lower)   ; call strip, then call lower on its result
```

The general rule ("enclosing parentheses are what make it a call") is
stated plainly in the docs, but translating a Python method chain into
the right nesting of parens is the kind of thing that's easy to get
backwards without deliberately working through each step.

## 5. Picking the right one of `getattr*` / `hasattr*` / `hasattr?`

There are three related but distinctly-shaped builtins for attribute
probing: `getattr*` (walks a whole attribute path, returns a supplied
default on `AttributeError`), `hasattr*` (same path-walking, returns a
bool, never raises), and `hasattr?` (a thin binding to Python's
`hasattr`, single name only). I initially reached for `getattr*` with a
one-element attribute list and a fallback lambda to port Python's
`getattr(stream, 'isatty', lambda: False)()`, then separately considered
`hasattr?` for a single-name check. In the end neither was needed -- the
defensive check wasn't protecting against anything that can actually
happen with a real stream object -- but sorting out which of the three
forms matched the single-attribute case took more lookups than
expected, since only the comparison table (not any individual form's
own doc) makes the distinction clear.

## 6. Whether ordinary strings support backslash escapes

The grammar reference documents `r"..."` as "raw string -- no backslash
escapes," which implies plain `"..."` and `f"..."` strings *do* support
them, but I did not find anywhere that positively confirms `\x1b`-style
hex escapes work the same as Python's. This mattered a lot here, since
both terminal protocols are built entirely out of ESC (`\x1b`) and BEL
(`\x07`) bytes embedded in f-strings. I proceeded on the inference and
it turned out correct (confirmed once the escape-sequence tests passed),
but it was an assumption, not a verified fact, for longer than I'd have
liked.

## 7. Tuple vs. list literals matter for real Python-visible behavior, not just style

`#(...)` and `#[...]` "read with the same grammar" per the docs, which
made it easy to lose track of *which* container type actually gets
built. It matters here because `str.startswith` accepts a `str` or a
*tuple* of strings, not a list -- passing a `#[...]` list where
`#(...)` was needed would fail at the Python level with a `TypeError`
that has nothing to do with Koskari. Every collection literal in this
module that crosses into a Python stdlib call (`startswith`, the
`in ... #(...)` membership checks) had to be double-checked as the
tuple form specifically.

## 8. `declare-exports`'s documented behavior is macro/facet-shaped

The reference for `declare-exports` describes it in terms of exporting
a name's "runtime name and the `compile/` and `layout/` facets of each"
-- language clearly aimed at modules that define new special forms or
macros with multiple facets. Our module only has ordinary functions
(one facet each), so it wasn't obvious from the doc alone whether
`declare-exports` was the intended mechanism for a plain-function
module or whether it was overkill machinery meant for a different kind
of module. It does work correctly for plain functions (only the runtime
facet applies, which is exactly what gets exported), but the doc reads
as though it's answering a harder question than the one being asked.

## 9. Dash-case vs. underscore-case naming has a real conflict, unaddressed in the docs

The pitfalls page states "Koskari-defined names keep their dashes" as
the idiomatic style. That's fine for private, Koskari-only names, but
this module's four public functions have to be importable from plain
Python by exact name (`from koskari.cli.term_graphics import
detect_protocol`), and Python identifiers can't contain dashes. Nothing
in the docs I found addresses this tension directly -- the resolution
(underscore-case for anything that crosses the Python import boundary,
dashes only for names that stay Koskari-internal) is a judgment call I
made from the constraint itself, not from guidance.

## 10. `list-each`/`each` comprehension bodies silently change behavior based on shape

A `list-each`/`each` body that is *just* a single `when`/`unless` form
gets rewritten into a keyword-filtered comprehension (`when:`/`unless:`
pulled out as a filter clause) -- but a body with other forms before it
(e.g. `define`s), even ending in the same `when`/`unless`, does not get
the same treatment and behaves like a plain per-item transform instead.
Both shapes are documented with worked examples, including one
explicitly tagged as a "pitfall," but the fact that *adding a harmless
local `define` changes which rewrite rule applies* is the kind of thing
that's very easy to trip over without having seen that exact comparison
first.
