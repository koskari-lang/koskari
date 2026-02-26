# Koskari Layouts — Block Syntax Reference

This document describes the layout system used by the Koskari reader for
indent-based block syntax. Layouts are resolved at read time and transform
block sources into s-expressions for the compiler. For special forms (compile-time
dispatch), see [SPECIALS.md](SPECIALS.md).

Layouts are registered via the `layout` decorator and collected in `LAYOUTS`.
The `LayoutReader` resolves layout heads from a namespace (`layout/<head>`);
handlers live in `koskari/reader/layouts/`.


## Blocks

A **block** is the basic unit of layout syntax. It consists of:

1. **Header:** forms parsed until a block opener (colon) is seen. The first form
   is the **head**; remaining forms are **args**.
2. **Body:** indented forms following the header, read until dedent or EOF.

**Block opener:** A standalone colon `:` or a keyword ending with `:` (e.g.
`else:`). The parser treats `x:` and `x :` identically.

**Top-level detection:** At top level (not inside `()`, `[]`, or `{}`), the
reader treats the input as a block: it reads a header until a colon, then reads
the indented body. Inside delimiters, layout mode is disabled and the base
s-expression reader is used.

**Resolution:** The reader looks up `layout/<head>` in the namespace. If found
and it is a `LayoutHandler`, that handler's `read(reader, source, peers)` is
invoked. If not found, the block is treated as a default block and routed
through `layout/block`.


## layout/block (default)

**Symbol:** `layout/block`

**Use-case:** The default layout for blocks whose head has no dedicated layout.
Any unrecognized block head (e.g. `foo`, `unknown`) is wrapped as
`(layout/block HEAD ARGS...)` and passed to this handler.

**Behaviour:** Produces `(HEAD ARGS (compile/begin BODY))`. The body is wrapped
in `compile/begin` for sequential evaluation. Does not accept peers.

**Syntax:**

```
HEAD ARG ... :
  BODY-FORM
  ...
```

**Examples:**

```scheme
;; foo x: ... → (foo x (compile/begin ...))
foo x:
  bar
  baz

;; unknown x: ... → (unknown x (compile/begin ...))
unknown x:
  (print x)
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_block`


## layout/begin

**Symbol:** `layout/begin`

**Use-case:** A block with no header arguments; body is lowered directly to
`compile/begin`. Useful for an indented sequence of forms without a call-like
header.

**Behaviour:** Produces `(compile/begin BODY-FORMS...)`. Does not accept peers.

**Syntax:**

```
layout/begin:
  FORM
  ...
```

**Examples:**

```scheme
layout/begin:
  (bar)
  (baz)

;; → (compile/begin bar baz)
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_begin`


## layout/if

**Symbol:** `layout/if`

**Use-case:** Multi-branch conditional with optional `elif` and `else` peers.
Lowers to `compile/cond`.

**Behaviour:** Accepts optional peer blocks `elif` and `else`. Gathers contiguous
peer blocks at the same indent level after the `if` body. Produces
`(compile/cond (TEST BODY) (TEST BODY) ... (else: BODY))`. Rules: `elif` cannot
follow `else`; `else` takes no arguments; only one `else` is allowed.

**Syntax:**

```
if COND:
  BODY
elif COND:
  BODY
else:
  BODY
```

**Examples:**

```scheme
if x:
  y
;; → (compile/cond (x y))

if x:
  a
elif y:
  b
else:
  c
;; → (compile/cond (x a) (y b) (else: c))
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_if`


## layout/try

**Symbol:** `layout/try`

**Use-case:** try/except/else/finally block syntax. Lowers to `(try ...)` for
compilation. Requires at least one `except` or `finally` peer.

**Behaviour:** Accepts peer blocks `except`, `else`, and `finally`. Produces
`(try (compile/begin BODY) CATCHES...)` where CATCHES is the list of except,
else, and finally clauses. Rules: `else` and `finally` take no arguments;
multiple `else` or `finally` are not allowed; at least one except or finally
is required. `except` blocks require an exception type; optional binding uses
`as: NAME` (e.g. `except Exception as: e`).

**Syntax:**

```
try:
  BODY
except TYPE:
  HANDLER
except TYPE as: NAME:
  HANDLER
else:
  ELSE-BODY
finally:
  FINALLY-BODY
```

**Examples:**

```scheme
try:
  x
except Exception:
  y
;; → (try (compile/begin x) ((except Exception) y))

try:
  x
finally:
  z
;; → (try (compile/begin x) (finally z))

try:
  x
except Exception:
  y
else:
  a
finally:
  z
;; → (try (compile/begin x) ((except Exception) y) (else a) (finally z))
```

**Implementation:** `koskari/reader/layouts/exceptions.layout_try`

**Tests:** `tests/reader/layouts/test_try.py`


## Peers

Some layouts declare **peer** block heads. After reading the main block's body,
the reader checks for sibling blocks at the same indent whose head matches one
of the declared peers. These are gathered contiguously and passed to the layout
handler as the `peers` argument.

`layout/if` declares `peers=(elif, else)`, so `elif` and `else` blocks at the
same indent as the initial `if` are read and passed as peers. `layout/try`
declares `peers=(except, else, finally)` for exception-handling blocks.
Layouts without peers receive an empty list.


## Adding New Layouts

1. Implement a handler with signature `(reader, source, peers)` in the
   appropriate module under `koskari/reader/layouts/`.
2. Decorate with `@layout(symbol('layout/head'), peers=(...))` if the layout
   has peer blocks.
3. Register the handler in the layout namespace (e.g. via `LAYOUTS` for tests,
   or the compiler's namespace for production).
4. Add a section to this document.
