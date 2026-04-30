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
and it is a `LayoutHandler`, that handler's `read(source, peers)` is invoked.
If not found, the block is treated as a default block and routed through
`layout/block`.


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


## layout/call

**Symbol:** `layout/call`

**Use-case:** Block syntax for a function call: the callee is the only header
argument; each indented body form becomes an argument to **`compile/call`**.
Useful when a call has several arguments and you want them on separate lines
without nesting parentheses for the whole call.

**Behaviour:** Does not accept peers. The header must be exactly
`(layout/call FN)` — no extra forms after **`FN`** (raises `LayoutError` if
there are header arguments). Produces **`(compile/call FN ARG ...)`**. The body
is **not** wrapped in **`compile/begin`**.

Before building the call, the handler walks the body list: if a body form is a
**pair whose car is a keyword**, it is rewritten in place into **two** adjacent
arguments (the keyword, then the cdr), matching the usual **`compile/call`**
keyword-argument layout. Ordinary forms are unchanged.

For **`*:`**, splices, and the full **`compile/call`** surface, see
[SPECIALS.md](SPECIALS.md) § compile/call.

**Syntax:**

```
layout/call FN:
  ARG-FORM
  ...
```

A line such as **`NAME: VALUE`** is read as one pair form; the layout normalizes
it to keyword plus value as described above.

**Examples:**

```scheme
layout/call print:
  "hello"
;; → (compile/call print "hello")

layout/call foo:
  bar
  (baz)
;; → (compile/call foo bar (baz))

layout/call foo:
  bar
  (baz)
  alpha: beta
;; → (compile/call foo bar (baz) alpha: beta)
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_call`


## layout/class

**Symbol:** `layout/class`

**Use-case:** Class **expression** block syntax. Lowers to `(compile/class NAME SUPERS
(compile/begin BODY))`. The form evaluates to the new class object; it does **not**
by itself bind **NAME** in the module (see [SPECIALS.md](SPECIALS.md) §
compile/class). To define a named class in one step, use **`layout/define-class`**
or wrap with **`compile/define`**.

The superclass list may be omitted; when missing, the expansion uses `()` (nil).

**Behaviour:** No peers. Parses header as `(class NAME)` or `(class NAME SUPERS)`.
When supers are omitted, uses nil so the result is `(compile/class NAME () BODY)`.
When present, SUPERS is the parsed form (e.g. a single base or a parenthesized
list). Raises LayoutError if the header has fewer than two elements (missing
name) or more than three (extra forms after supers).

**Syntax:**

```
class NAME:
  BODY
;; → (compile/class NAME () (compile/begin BODY))

class NAME (Base1 Base2):
  BODY
;; → (compile/class NAME (Base1 Base2) (compile/begin BODY))
```

**Examples:**

```scheme
class C:
  x
;; → (compile/class C () (compile/begin x))

class C (Base):
  x
;; → (compile/class C (Base) (compile/begin x))

class C ():
  x
;; → (compile/class C () (compile/begin x))
```

**Implementation:** `koskari/reader/layouts/classes.layout_class`


## layout/define-class

**Symbol:** `layout/define-class`

**Use-case:** Class **definition** block syntax: lowers to
`(compile/define-class NAME SUPERS (compile/begin BODY))`, which the
**`compile/define-class`** macro expands to **`define`** plus **`class`** (see
[MACROS.md](MACROS.md) § compile/define-class). Same header shape as
**`layout/class`**, but the read result binds **NAME** to the class after
expansion.

**Behaviour:** No peers. Parses header as `(define-class NAME)` or
`(define-class NAME SUPERS)`. When supers are omitted, uses nil. Raises
LayoutError if the header has fewer than two elements (missing name) or more
than three.

**Syntax:**

```
define-class NAME:
  BODY
;; → (compile/define-class NAME () (compile/begin BODY))

define-class NAME (Base1 Base2):
  BODY
;; → (compile/define-class NAME (Base1 Base2) (compile/begin BODY))
```

**Examples:**

```scheme
define-class C:
  x
;; → (compile/define-class C () (compile/begin x))

define-class C (Base):
  x
;; → (compile/define-class C (Base) (compile/begin x))
```

**Implementation:** `koskari/reader/layouts/classes.layout_define_class`


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


## layout/switch

**Symbol:** `layout/switch`

**Use-case:** Multi-way equality dispatch in block syntax. Lowers to `switch`
clause lists consumed by `compile/switch`.

**Behaviour:** Does not accept peers. Header must be exactly
`switch VALUE:` (one value expression). Inner `case` and `else` blocks are
left as default block layouts (no dedicated `layout/case` or `layout/else`).
So children are read first by `layout/block` as:

- `case COND:` -> `(case COND (compile/begin BODY...))`
- `else:` -> `(else (compile/begin BODY...))`

`layout/switch` validates and rewrites those child forms into:

- case clause: `(COND-LIST (compile/begin BODY...))`
- else clause: `(else: (compile/begin BODY...))`

It then returns:

`(switch VALUE (COND-LIST (compile/begin ...)) ... (else: (compile/begin ...)))`

Rules:

- body entries must be `case` or `else` blocks only
- `case` requires exactly one condition argument and it is passed through
  as the case's condition list expression
- `else` takes no arguments
- only one `else` is allowed
- `case` cannot appear after `else`
- empty body is allowed and lowers to `(switch VALUE)`

**Syntax:**

```
switch VALUE:
  case COND-LIST:
    BODY
  case COND-LIST2:
    BODY
  else:
    BODY
```

**Examples:**

```scheme
switch x:
  case [1]:
    "one"
  case [2 3]:
    "two-or-three"
  else:
    "meh"
;; -> (switch x
;;      ([1] (compile/begin "one"))
;;      ([2 3] (compile/begin "two-or-three"))
;;      (else: (compile/begin "meh")))

switch x:
  case [1]:
    "one"
;; -> (switch x ([1] (compile/begin "one")))
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_switch`

**Tests:** `tests/reader/layouts/test_switch_layout.py`


## layout/try

**Symbol:** `layout/try`

**Use-case:** try/except/else/finally block syntax. Lowers to `(try ...)` for
compilation. Requires at least one `except` or `finally` peer.

**Behaviour:** Accepts peer blocks `except`, `else`, and `finally`. Produces
`(try (compile/begin BODY) CATCHES...)` where CATCHES is the list of except,
else, and finally clauses. Rules: `else` and `finally` take no arguments;
multiple `else` or `finally` are not allowed; at least one except or finally
is required.

Three forms of `except` peer are accepted:

- **Bare** — `except:` with no arguments: catches any exception without a type
  check and without binding. Emits `(except: BODY...)`.
- **Type-only** — `except [TYPEEXPR]:` with a single bracketed USE form
  containing only the type expression: checks the exception type, no binding.
  Emits `(TYPEEXPR BODY...)`.
- **Binding** — `except [SYM TYPEEXPR]:` or `except [TYPEEXPR as: SYM]:`:
  checks the type and binds the caught exception to `SYM`. Emits
  `((SYM . TYPEEXPR) BODY...)`.

All typed and binding forms use bracketed USE specifiers; `except TYPEEXPR:`
(no brackets) is invalid.

**Syntax:**

```
try:
  BODY
except:
  HANDLER
except [TYPEEXPR]:
  HANDLER
except [SYM TYPEEXPR]:
  HANDLER
except [TYPEEXPR as: SYM]:
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
except [Exception]:
  y
;; → (try (compile/begin x) [[Exception] y])

try:
  x
except [e Exception]:
  y
;; → (try (compile/begin x) [[e Exception] y])

try:
  x
except [Exception as: e]:
  y
;; → (try (compile/begin x) [[e  Exception] y])

try:
  x
except:
  y
;; → (try (compile/begin x) [except: y])

try:
  x
finally:
  z
;; → (try (compile/begin x) [finally: z])

try:
  x
except [Exception]:
  y
else:
  a
finally:
  z
;; → (try (compile/begin x) [[Exception] y] [else: a] [finally: z])
```

**Implementation:** `koskari/reader/layouts/exceptions.layout_try`

**Tests:** `tests/reader/layouts/test_try.py`


## layout/syntax-rules

**Symbol:** `layout/syntax-rules`

**Use-case:** Block syntax for the `syntax-rules` transformer within a
`define-syntax` block. Converts indented pattern/template rule blocks into the
canonical `(syntax-rules (LIT ...) ((PATTERN) TEMPLATE) ...)` form. Used as the
inner block of `layout/define-syntax`.

**Behaviour:** Accepts a literals list in the header (e.g. `[x]` or `(x y)`).
Each indented rule block has a pattern in the header and a template in the
body. Does not accept peers.

**match form:** Use `match (PATTERN):` instead of `PATTERN:` when the pattern
contains top-level keywords. Layout headers cannot parse keywords directly, but
inside parentheses the form is read in s-expr context, so keywords are valid.
Both styles expand to the same rule. Example: `ref (item obj name):` and
`match (ref (item obj name)):` produce identical output.

**Syntax:**

```
syntax-rules [LIT ...]:
  PATTERN1:
    TEMPLATE1...
  match (PATTERN-WITH-KEYWORDS):
    TEMPLATE...
  PATTERN2 ARG:
    TEMPLATE2...
```

**Examples:**

```scheme
syntax-rules [x]:
  do-something-with-x:
    (print "no args" x)
    x
  do-something-with-x y:
    (print "one arg" x y)
    (+ x y)
;; → (syntax-rules (x)
;;     ((do-something-with-x) (compile/begin (print "no args" x) x))
;;     ((do-something-with-x y) (compile/begin (print "one arg" x y) (+ x y))))

syntax-rules [item]:
  match (ref (item obj name)):
    (compile/ref-item obj name)
;; → same as ref (item obj name): ... ; use match when pattern has keywords
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_syntax_rules`


## layout/macro-rules

**Symbol:** `layout/macro-rules`

**Use-case:** Block syntax for the `macro-rules` transformer within a
`define-syntax` block. Same surface as `layout/syntax-rules` (literals list and
indented rules); lowers to `(macro-rules (LIT ...) ((PATTERN) TEMPLATE) ...)`.

**Behaviour:** Same as `layout/syntax-rules` except for the emitted head. Does
not accept peers.

**Syntax:**

```
macro-rules [LIT ...]:
  PATTERN1:
    BODY1...
  PATTERN2:
    BODY2...
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_macro_rules`


## layout/syntax-match

**Symbol:** `layout/syntax-match`

**Use-case:** Block syntax for the `syntax-match` transformer within a
`define-syntax` block. No literals list; each rule lowers to **`(PATTERN
TEMPLATE)`** (no **`rule`** wrapper).

**Behaviour:** Same per-rule header/body rules as `layout/syntax-rules` (including
`match (PATTERN):` for keywords). Does not accept peers.

**Syntax:**

```
syntax-match:
  PATTERN1:
    TEMPLATE1...
  match (PATTERN-WITH-KEYWORDS):
    TEMPLATE...
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_syntax_match`


## layout/macro-match

**Symbol:** `layout/macro-match`

**Use-case:** Block syntax for the `macro-match` transformer within a
`define-syntax` block. Same rule shape as **`layout/macro-rules`**: a pattern
header (symbol or parenthesized form) then body forms.

**Behaviour:** Does not accept peers.

**Syntax:**

```
macro-match:
  HEAD ARGS...:
    BODY1...
  (complex pat):
    BODY1
    BODY2
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_macro_match`


## layout/define-syntax

**Symbol:** `layout/define-syntax`

**Use-case:** Block syntax for defining syntax transformers (macros). Wraps a
single inner transformer block and lowers to
`(compile/define-syntax NAME (TRANSFORMER ...))`. The resulting form is
expanded by the **`compile/define-syntax`** macro, then compiled; see
[MACROS.md](MACROS.md) and [SPECIALS.md](SPECIALS.md).

**Behaviour:** Expects exactly one body form: `syntax-rules`, `macro-rules`,
`syntax-match`, or `macro-match` (each produced by the corresponding
`layout/...`). Produces `(compile/define-syntax NAME transformer-form)`. Does
not accept peers.

**Syntax:**

```
define-syntax NAME:
  syntax-rules [LIT ...]:
    PATTERN1:
      TEMPLATE1...
    PATTERN2:
      TEMPLATE2...
```

**Other inner heads:** `macro-rules`, `syntax-match`, and `macro-match` use the
same indentation style as their layout sections above.

**Examples:**

```scheme
define-syntax do-something-with-x:
  syntax-rules [x]:
    do-something-with-x:
      (print "x and no args" x)
      x
    do-something-with-x y:
      (print "x and one arg" x y)
      (+ x y)
;; → (compile/define-syntax do-something-with-x
;;     (syntax-rules (x)
;;       ((do-something-with-x) (compile/begin (print "x and no args" x) x))
;;       ((do-something-with-x y) (compile/begin (print "x and one arg" x y) (+ x y)))))
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_define_syntax`

**Tests:** `tests/reader/layouts/test_define_syntax.py`


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
