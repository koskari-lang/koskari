# Koskari Layouts — Block Syntax Reference

This document describes the layout system used by the Koskari reader for
indent-based block syntax. Layouts are resolved at read time and transform
block sources into s-expressions for the compiler. For special forms (compile-time
dispatch), see [SPECIALS.md](SPECIALS.md).

Layouts are registered via the `layout` decorator and collected in `LAYOUTS`.
The `LayoutReader` resolves layout heads from a namespace (`layout/<head>`);
handlers live in `koskari/reader/layouts/`. A **`.ki`** module can reuse an
existing handler by binding **`layout/<head>`** to another layout name (for
example **`define layout/foo layout/block-begin`**) instead of adding Python
code.


## Blocks

Normative layout syntax (blocks, lines, colon rules, peers, EBNF) is in
[docs/reference/reader-layout.md](../docs/reference/reader-layout.md).
Three authoring surfaces (layout block, layout line, s-expression) are in
[docs/reference/concepts/layout-and-s-expressions.md](../docs/reference/concepts/layout-and-s-expressions.md).

Agent summary:

- **Layout block:** header + structural `:` + **mandatory newline** + indented body
- **Layout line:** depth-0 line without structural trailing `:`; multiple forms fold to `(first . rest…)` when implicit calls are enabled (any first form, not symbol-only)
- **Parse IR:** colon blocks → **`(:block: (HEAD ARG …) (BODY …))`**; stream peers → spine of **`:block:`** conses
- **Expand:** **`LayoutReader.read()`** → **`apply_layout`** / **`apply_layouts`**; handlers **`read(namespace, source, peers)`**
- **Default fallback:** unknown heads → **`layout/block`** → **`(HEAD ARG … BODY …)`** (flat splice)
- **Opt-in begin wrap:** **`layout/block-begin`** → **`(HEAD ARG … (compile/begin BODY …))`**

## `:block:` conses and layout expansion

Colon blocks parse as **`:block:`** conses. **`LayoutReader.read()`** expands
them through **`koskari.reader.layouts`**:

- **`apply_layout(block, namespace)`** — one **`:block:`** cons
- **`apply_layouts(spine, namespace)`** — proper list spine; declared peer
  **`:block:`** siblings after each primary are consumed and passed to the
  handler

**`LayoutReader.read_form`** returns raw **`:block:`** (or a spine for peer
groups). **`read()`** performs expansion. Quote and quasiquote block prefixes
set **`_block_mode`** so nested colons stay raw until **`layout/quote`** /
**`layout/quasiquote`** unbox at expansion.

Some compiler paths preserve **`:block:`** as data — quoted blocks,
**`compile/quasiquote-block`**, and macro templates. **`koskari.syntax._finalize_macro_output`**
calls **`apply_layout`** on macro and **`syntax-rules`** output before hygiene.
See [SPECIALS.md](SPECIALS.md) § compile/quasiquote-block and [MACROS.md](MACROS.md).

User-facing normative syntax: [docs/reference/reader-layout.md](../docs/reference/reader-layout.md).


## layout/block (default)

**Symbol:** `layout/block`

**Use-case:** Default when **`apply_layout`** / **`apply_layouts`** finds no
**`layout/<head>`** for the block head. Head symbol is preserved.

**Behaviour:** Splices body onto header: **`(HEAD ARG … BODY-FORM …)`**. No
peers.

**Examples:**

```scheme
foo x:
  bar
  baz
;; → (foo x bar baz)

begin:
  (bar)
  (baz)
;; → (begin (bar) (baz))
```

**Implementation:** `koskari/reader/layouts/basics.layout_block`


## layout/block-begin

**Symbol:** `layout/block-begin`

**Use-case:** Opt-in **`compile/begin`** wrap. Bind with
**`define layout/<head> layout/block-begin`** in **`.ki`**.

**Behaviour:** **`(HEAD ARG … (compile/begin BODY …))`**. No peers.

**Implementation:** `koskari/reader/layouts/basics.layout_block_begin`


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


## layout/while and layout/for-each

**Symbols:** `layout/while`, `layout/for-each`

**Use-case:** Block syntax for `while` and `for-each` loops with an optional
`else` peer. Both names share one handler (`layout_loop`); the read result keeps
the block head (`while` or `for-each`) and is compiled by `compile/while` or
`compile/for-each`.

**Behaviour:** Body forms are appended to the header list (flat splice via
default **`layout/block`** on inner heads where applicable). Accepts at most one optional peer
`else`. When present, the else peer body is appended as a final
`(else: BODY...)` clause (not merged into the main loop body). Rules: only
`else` is allowed as a peer; `else` takes no arguments; at most one `else` peer.

**Syntax:**

```
while TEST:
  BODY
else:
  BODY

for-each [BINDING EXPR]:
  BODY
else:
  BODY
```

**Examples:**

```scheme
while (some_test 123):
  loopbody...
else:
  elsebody...
;; → (while (some_test 123) loopbody... (else: elsebody...))

while x:
  y
;; → (while x y)

for-each [i items]:
  (compile/call append result i)
else:
  "finished"
;; → (for-each [i items] (compile/call append result i) (else: "finished"))
```

**Implementation:** `koskari/reader/layouts/loops.layout_loop` (registered as
`layout/while` and `layout/for-each` via `add_builtin`)


## layout/switch

**Symbol:** `layout/switch`

**Use-case:** Multi-way equality dispatch in block syntax. Lowers to `switch`
clause lists consumed by `compile/switch`.

**Behaviour:** Does not accept peers. Header must be exactly
`switch VALUE:` (one value expression). Inner `case` and `else` blocks are
left as default block layouts (no dedicated `layout/case` or `layout/else`).
Inner `case` and `else` children are expanded by default **`layout/block`**
(flat splice). `layout/switch` validates each child and emits clause lists:

`(switch VALUE (COND-LIST BODY...) ... (else: BODY...))`

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
;; -> (switch x ([1] "one") ([2 3] "two-or-three") (else: "meh"))

switch x:
  case [1]:
    "one"
;; -> (switch x ([1] "one"))
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_switch`

**Tests:** `tests/reader/layouts/test_switch_layout.py`


## layout/match

**Symbol:** `layout/match`

**Use-case:** Structural pattern matching in block syntax. Lowers to the
clause lists consumed by `compile/match`
([SPECIALS.md](SPECIALS.md) § compile/match; pattern dialect in
[MATCHER.md](MATCHER.md) § Runtime match/case).

**Behaviour:** Does not accept peers. Header must be exactly
`match VALUE:` (one value expression). A **raw** layout: `case` and `else`
children arrive as block sources, so each case header — however many
tokens — stays structurally separate from its body. The whole header tail
becomes the clause's pattern delimiter, which is how a multi-token
spelling like `case x :: int:` survives as one delimiter. Case and else
bodies are expanded with `apply_layouts`. Emits:

`(compile/match VALUE [[PATTERN...] BODY...] ... [else: BODY...])`

— the same clause shape an s-expr author writes directly.

A `when` / `unless` marker in a case header splits it into pattern and
guard: `case PATTERN when COND:` emits `[[PATTERN] when: COND BODY...]`
(bare marker symbol in the header, keyword in the clause).

Rules:

- body entries must be `case` or `else` blocks only
- `case` requires a pattern and a non-empty body
- a guard marker needs a pattern before it and exactly one expression
  after it
- `else` takes no arguments, requires a non-empty body, is allowed once,
  and no `case` may follow it
- empty body is allowed and lowers to `(compile/match VALUE)`

**Syntax:**

```
match VALUE:
  case PATTERN:
    BODY
  case PATTERN when COND:
    BODY
  else:
    BODY
```

**Examples:**

```scheme
match (get-value):
  case #[a b c]:
    (use a b c)
  case x :: int:
    (use x)
  case Point[x y] when (> x y):
    (use x y)
  else:
    (other)
;; -> (compile/match (get-value)
;;      [[#[a b c]] (use a b c)]
;;      [[x :: int] (use x)]
;;      [[Point[x y]] when: (> x y) (use x y)]
;;      [else: (other)])
```

**Implementation:** `koskari/reader/layouts/conditionals.layout_match`

**Tests:** `tests/reader/layouts/test_match_layout.py`


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

**Use-case:** Block surface syntax for **`define-syntax`**. With no dedicated
handler registered, default **`layout/block`** splices the transformer after the
name: **`(define-syntax NAME TRANSFORMER …)`**.

**Behaviour:** Transformer choice and validation happen in the
**`compile/define-syntax`** macro at compile time (see **compile/define-syntax**
in [MACROS.md](MACROS.md)). Does not accept peers.

**Syntax:**

```
define-syntax NAME:
  syntax-rules [LIT ...]:
    PATTERN1:
      TEMPLATE1...
    PATTERN2:
      TEMPLATE2...
```

**Other inner heads:** `macro-rules`, `syntax-match`, `macro-match`,
`special-rules`, and `special-match` use the same indentation style as their
layout sections below.

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
;; → (define-syntax do-something-with-x
;;     (syntax-rules (x)
;;       ((do-something-with-x) (compile/begin (print "x and no args" x) x))
;;       ((do-something-with-x y) (compile/begin (print "x and one arg" x y) (+ x y)))))
```

**Implementation:** default **`layout/block`** (`koskari/reader/layouts/basics.layout_block`)

**Tests:** `tests/reader/layouts/test_define_syntax.py`


## layout/extend-syntax

**Symbol:** `layout/extend-syntax`

**Use-case:** Block surface syntax for **`extend-syntax`**. Default **`layout/block`**
splices the transformer: **`(extend-syntax NAME TRANSFORMER …)`**.

**Behaviour:** Compile-time behaviour is **`compile/extend-syntax`** (see
**compile/extend-syntax** in [MACROS.md](MACROS.md)). Does not accept peers.

**Syntax:**

```
extend-syntax NAME:
  syntax-rules [LIT ...]:
    PATTERN:
      TEMPLATE...
```

**Implementation:** default **`layout/block`** (`koskari/reader/layouts/basics.layout_block`)


## Peers

Some layouts declare **peer** block heads. After parsing the primary block body,
the reader checks for sibling blocks at the same indent whose head matches one
of the declared peers. These are gathered as **`:block:`** conses on a spine and
expanded with the primary through **`apply_layouts`**, then passed to the
handler as the `peers` argument.

`layout/if` declares `peers=(elif, else)`, so `elif` and `else` blocks at the
same indent as the initial `if` are read and passed as peers. `layout/try`
declares `peers=(except, else, finally)` for exception-handling blocks.
`layout/while` and `layout/for-each` declare `peers=(else)` for an optional
loop else clause (same handler object for both heads).
Layouts without peers receive an empty list.


## Adding New Layouts

1. Flat splicing only (header followed by body forms in one list): no handler
   needed — default **`layout/block`** applies.
2. **`compile/begin`** wrap: **`define layout/<head> layout/block-begin`** in
   **`.ki`**, or implement a custom handler.
3. Otherwise implement **`read(namespace, source, peers)`** under
   `koskari/reader/layouts/`, decorate with `@layout(symbol('layout/head'),
   peers=(...))` when peer blocks are needed, and register in the layout
   namespace.
4. Add a section to this document and a page under `docs/reference/layouts/`.
