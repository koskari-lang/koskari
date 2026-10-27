# Koskari Layouts — Block Syntax Reference

This document describes the layout system used by the Koskari reader for
indent-based block syntax. Layouts are resolved at read time and transform
block sources into s-expressions for the compiler. For special forms (compile-time
dispatch), see [SPECIALS.md](SPECIALS.md).

Layouts are registered via the `layout` decorator and bound as `layout/<head>`
builtins; there is no separate collector.
The `LayoutReader` resolves layout heads from a namespace (`layout/<head>`);
handlers live in `koskari/reader/layouts/`. A **`.ki`** module can reuse an
existing handler by binding **`layout/<head>`** to another layout name (for
example **`var layout/foo layout/block-begin`**) instead of adding Python
code.


## Blocks

Normative layout syntax (blocks, lines, colon rules, peers, EBNF) is in
[docs/reference/reader-layout.md](../docs/reference/reader-layout.md).
Three authoring surfaces (layout block, layout line, s-expression) are in
[docs/guide/layout-and-s-expressions.md](../docs/guide/layout-and-s-expressions.md).

Agent summary:

- **Layout block:** header + structural `:` + **mandatory newline** + indented body
- **Layout line:** depth-0 line without structural trailing `:`; multiple forms fold to `(first . rest…)` when implicit calls are enabled (any first form, not symbol-only)
- **Parse IR:** colon blocks → **`(:#block: (HEAD ARG …) ENTRY …)`** where each entry is a line or a sub-block (see **Block IR** below); stream peers → spine of **`:#block:`** conses
- **Expand:** **`LayoutReader.read()`** → **`apply_layout`** / **`apply_layouts`**; handlers **`read(reader, source, peers)`**
- **Default fallback:** unknown heads → **`layout/block`** → **`(HEAD ARG … BODY …)`** (flat splice)
- **Opt-in begin wrap:** **`layout/block-begin`** → **`(HEAD ARG … (compile/begin BODY …))`**

## Block IR

A colon block reads to one cons:

```
(:#block: HEAD-LIST ENTRY ...)

HEAD-LIST ::= (ELEMENT ...)          the header's elements, colon removed
ENTRY     ::= LINE | BLOCK           one per body line
LINE      ::= (:#line: ELEMENT ...)  the elements the reader saw on that line
BLOCK     ::= (:#block: ...)         a nested colon block, or a peer
```

The header is always the second element. Every later element of the cons is
one body entry, and both kinds carry their tag in car, `:#line:` or
`:#block:`. Nothing is folded at parse time. The reader
records exactly what it saw, and the difference between a line that happens
to be a pair and a pair that was written stays visible:

```
foo:                (:#block: (foo)
  else: x             (:#line: else: x)                 line, two elements
  bar:                (:#block: (bar) (:#line: y))      sub-block
    y
  (else: x)           (:#line: (else: x))               line, one element, a pair
  [else: x]           (:#line: (else: x)))              line, one element, a pair
```

A peer block read from the stream is an entry after the primary block it
belongs to, so a peer spine and a body are the same shape. A decorated block
is a line, because the `@` reader macro wraps the block in a
**`(compile/decorate ...)`** form and that form is the line's one element.
Inside a quoted or quasiquoted block a line holding only an unquote-splicing
form is **`(:#splice: FORM)`**, so the filled forms spread into one line each
when the block is expanded; a splice sharing a line with other elements
splices into that line.

Handlers receive the cdr of this cons as their source, **`(HEAD-LIST . ENTRIES)`**,
and the reader that is expanding it:

```
read(reader, source, peers)
```

The line rule is the reader's, driven by **`reader/implicit-calls`**:
**`reader.fold_line(entry)`** returns a one-element line's element, folds
several elements to the call of the first (or raises when implicit calls are
off), and lowers a block entry to its form; **`reader.fold_lines(entries)`**
does the same for a whole body. A handler folds where it wants forms and
reads elements directly where it does not.

Before a non-raw handler runs, **`apply_lines`** lowers every sub-block whose
head is not a keyword into a one-element line holding the lowered form,
gathering that block's declared peers from the entries that follow it. A
sub-block whose head is a keyword is left as it is. The default
**`layout/block`** is the one handler that gives it a meaning as an entry:
a keyword line splices as the keyword and the fold of the rest, a
keyword-headed block splices as the keyword and the block's value, `begin`
over the body for `k::` and the value head's own lowering for `k: HEAD ...:`.
Any other handler folding such an entry gets the pair **`(k: VALUE)`**, which
is what **`layout/block`** returns when dispatch reaches it as the block's own
handler, and the compiler judges that form like any other.

## `:#block:` expansion

**`LayoutReader.read()`** expands block conses through
**`koskari.reader.layouts`**, passing itself:

- **`apply_layout(block, reader)`** — one **`:#block:`** cons
- **`apply_layouts(spine, reader)`** — proper list spine of forms; declared
  peer **`:#block:`** siblings after each primary are consumed and passed to
  the handler; other forms are walked for embedded blocks
- **`apply_lines(entries, reader)`** — a body: lines and sub-blocks as above

Each accepts a namespace in place of the reader and resolves the module's
reader through **`koskari.module.find_module_reader`**, which returns the
bound **`__reader__`** or rebuilds one from **`__pragma__`** and rebinds it,
the way **`get-compiler`** does for the compiler. The runtime
**`apply-layouts`** and **`apply-layout`** builtins take that path for a
filled quasiquote block, as does **`koskari.syntax._finalize_macro_output`**
for macro output.

**`LayoutReader.read_form`** returns a raw **`:#block:`** (or a spine for peer
groups) and a **`LINE`** for a plain line; **`read()`** folds a top-level line
and expands a block. Quote and quasiquote block prefixes, and their
mark-keeping forms `#'` and `` #` ``, set **`_block_mode`** so nested colons
stay raw until **`layout/quote`** / **`layout/quasiquote`** /
**`layout/quote-syntax`** / **`layout/quasiquote-syntax`** unbox at
expansion.

Some compiler paths preserve **`:#block:`** as data -- quoted and
quasiquoted blocks, and macro templates.
See [layout/quote, layout/quasiquote, and their syntax forms](#layoutquote-layoutquasiquote-and-their-syntax-forms)
and [MACROS.md](MACROS.md).

User-facing normative syntax: [docs/reference/reader-layout.md](../docs/reference/reader-layout.md).


## layout/block (default)

**Symbol:** `layout/block`

**Use-case:** Default when **`apply_layout`** / **`apply_layouts`** finds no
**`layout/<head>`** for the block head. Head symbol is preserved.
`define-syntax` and `extend-syntax` blocks take this default: the transformer
block splices in after the name, and the `layout/syntax-rules` family below
handles the inner block.

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
**`var layout/<head> layout/block-begin`** in **`.ki`**.

**Behaviour:** **`(HEAD ARG … (compile/begin BODY …))`**. No peers.

**Implementation:** `koskari/reader/layouts/basics.layout_block_begin`


## layout/inline-block

**Symbol:** `layout/inline-block`

**Use-case:** Opt-in splice of an indented body onto the header as one list
with no implicit calls at all, for heads whose block only breaks a long line.
Bind with **`var layout/<head> layout/inline-block`** in **`.ki`**.

**Behaviour:** **`(HEAD ARG … LINE-FORM …)`**. No peers.

**Syntax:**

```
X foo:
  bar (baz)
  qux
  alpha: beta
;; → (X foo bar (baz) qux alpha: beta)
```

**Implementation:** `koskari/reader/layouts/basics.layout_inline_block`


## layout/import-from

**Symbol:** `layout/import-from`

**Use-case:** Long **`import-from`** name lists. Delegates to
**`layout/inline-block`**.

**Syntax:**

```
import-from itertools:
  count cycle permutations product repeat chain
  combinations combinations_with_replacement
  groupby tee
```

**Implementation:** `koskari/reader/layouts/basics.layout_import_from`


## layout/quote, layout/quasiquote, and their syntax forms

**Symbols:** `layout/quote`, `layout/quasiquote`, `layout/quote-syntax`,
`layout/quasiquote-syntax`

**Use-case:** A **`'`**, **`` ` ``**, **`#'`**, or **`` #` ``** prefix on a
block head. The reader wraps the raw **`:#block:`** in a synthetic
**`(:#block: (quote) INNER)`** (or `quasiquote`, `quote-syntax`,
`quasiquote-syntax`), the inner block as the one body entry, for these raw
handlers to unbox.

**Behaviour:** **`(compile/quote INNER-BLOCK)`**,
**`(compile/quasiquote INNER-BLOCK)`**, **`(compile/quote-syntax
INNER-BLOCK)`**, or **`(compile/quasiquote-syntax INNER-BLOCK)`**, where
INNER-BLOCK is the raw `:#block:` cons, not layout-expanded. The syntax forms
keep the hygiene marks on the block's symbols. No peers.

**Implementation:** `koskari/reader/layouts/basics.layout_quote`,
`koskari/reader/layouts/basics.layout_quasiquote`,
`koskari/reader/layouts/basics.layout_quote_syntax`,
`koskari/reader/layouts/basics.layout_quasiquote_syntax`;
reader side `koskari/reader/layout.LayoutReader._read_quote_block` and
`_read_quasiquote_block`, reached through the `#` event macro for the
syntax forms


## layout/class

**Symbol:** `layout/class`

**Use-case:** Class **definition** block syntax. Lowers to `(class NAME SUPERS
(begin BODY))`, which the **`class`** binding macro expands to
**`(var NAME (build-class NAME SUPERS …))`** (see [MACROS.md](MACROS.md) §
compile/class), binding **NAME** in the scope the block appears in. Under
**`global`** (`global class NAME:`) the binding goes to the module.

The superclass list may be omitted; when missing, the expansion uses `()` (nil).

**Behaviour:** No peers. Parses header as `(class NAME)` or `(class NAME SUPERS)`.
When supers are omitted, uses nil so the result is `(class NAME () BODY)`.
When present, SUPERS is the parsed form (e.g. a single base or a parenthesized
list). Raises LayoutError if the header has fewer than two elements (missing
name) or more than three (extra forms after supers).

**Syntax:**

```
class NAME:
  BODY
;; → (class NAME () (begin BODY))

class NAME (Base1 Base2):
  BODY
;; → (class NAME (Base1 Base2) (begin BODY))
```

**Examples:**

```scheme
class C:
  x
;; → (class C () (begin x))

class C (Base):
  x
;; → (class C (Base) (begin x))

class C ():
  x
;; → (class C () (begin x))
```

**Implementation:** `koskari/reader/layouts/classes.layout_class`


## layout/build-class

**Symbol:** `layout/build-class`

**Use-case:** Class **expression** block syntax: lowers to
`(build-class NAME SUPERS (begin BODY))`. The form evaluates to the new class
object and binds nothing (see [SPECIALS.md](SPECIALS.md) § compile/build-class).
Same header shape and errors as **`layout/class`**.

**Syntax:**

```
build-class NAME (Base1 Base2):
  BODY
;; → (build-class NAME (Base1 Base2) (begin BODY))
```

**Examples:**

```scheme
var C:
  build-class C (Base):
    x
;; → (var C (build-class C (Base) (begin x)))
```

**Implementation:** `koskari/reader/layouts/classes.layout_build_class`


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

**Implementation:** `koskari/reader/layouts/loops.layout_for_each` and
`loops.layout_while`, each registered with `@builtin` and `@layout` over the
shared body `_layout_loop`


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
spelling like `case x :: int:` survives as one delimiter. A block case or
else body is expanded with `apply_lines` and folded; an inline body goes
through `apply_layouts`. Emits:

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


## layout/dyn-wind

**Symbol:** `layout/dyn-wind`

**Use-case:** Block surface syntax for **`dyn-wind`** with **`before`** and
**`after`** peers.

**Behaviour:** **`(compile/dyn-wind [BEFORE …] [BODY …] [AFTER …])`**. The
body is required; at least one of the peers is required.

**Syntax:**

```
dyn-wind:
  body-exprs ...
before:
  before-exprs ...
after:
  after-exprs ...
```

**Implementation:** `koskari/reader/layouts/dyn_wind.layout_dyn_wind`


## layout/syntax-rules

**Symbol:** `layout/syntax-rules`

**Use-case:** Block syntax for the `syntax-rules` transformer within a
`define-syntax` block. Converts indented pattern/template rule blocks into the
canonical `(syntax-rules (LIT ...) ((PATTERN) TEMPLATE) ...)` form. Used as the
inner block of a `define-syntax` block.

**Behaviour:** Accepts a literals list in the header (e.g. `[x]` or `(x y)`).
Each indented rule block has a pattern in the header and a template in the
body. Does not accept peers.

**case form:** Use `case (PATTERN):` instead of `PATTERN:` when the pattern
contains top-level keywords. Layout headers cannot parse keywords directly, but
inside parentheses the form is read in s-expr context, so keywords are valid.
Both styles expand to the same rule. Example: `ref (item obj name):` and
`case (ref (item obj name)):` produce identical output. Any other head is a
pattern as written: `match (P):` is the pattern `(match P)`.

**Syntax:**

```
syntax-rules [LIT ...]:
  PATTERN1:
    TEMPLATE1...
  case (PATTERN-WITH-KEYWORDS):
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
  case (ref (item obj name)):
    (compile/ref-item obj name)
;; → same as ref (item obj name): ... ; use case when pattern has keywords
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
`case (PATTERN):` for keywords). Does not accept peers.

**Syntax:**

```
syntax-match:
  PATTERN1:
    TEMPLATE1...
  case (PATTERN-WITH-KEYWORDS):
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
  case (PATTERN-WITH-KEYWORDS):
    BODY...
```

A rule header must start with a symbol; a parenthesized header is not a
block and is rejected.

**Implementation:** `koskari/reader/layouts/define_syntax.layout_macro_match`


## layout/special-rules

**Symbol:** `layout/special-rules`

**Use-case:** Block syntax for the `special-rules` transformer within a
`define-syntax` block. Same block shape as `layout/macro-rules`; lowers to
`(special-rules (LIT ...) (PATTERN . BODY...) ...)`.

**Behaviour:** Same as `layout/macro-rules` except for the emitted head. Does
not accept peers.

**Implementation:** `koskari/reader/layouts/define_syntax.layout_special_rules`


## layout/special-match

**Symbol:** `layout/special-match`

**Use-case:** Block syntax for the `special-match` transformer within a
`define-syntax` block. Same rule shape as `layout/macro-match`; lowers to
`(special-match (PATTERN . BODY...) ...)`.

**Behaviour:** Same as `layout/macro-match` except for the emitted head. Does
not accept peers.

**Implementation:** `koskari/reader/layouts/define_syntax.layout_special_match`


## layout/bare-syntax

**Symbol:** `layout/bare-syntax`

**Use-case:** Block syntax for a `bare-syntax` transformer. The header names
the called-by slot, with an optional leading name; the body is the template,
expanded like any block body (`apply_lines`, then folded) before wrapping.

**Behaviour:** **`(bare-syntax SLOT TMPL)`**, or **`(bare-syntax NAME SLOT
TMPL)`** with a leading name; a multi-form template body is wrapped in
`begin`. Does not accept peers.

**Syntax:**

```
bare-syntax SLOT:
  TEMPLATE-FORM...
```

**Implementation:** `koskari/reader/layouts/define_syntax.layout_bare_syntax`


## Peers

Some layouts declare **peer** block heads. After parsing the primary block body,
the reader checks for sibling blocks at the same indent whose head matches one
of the declared peers. These are gathered as **`:#block:`** conses on a spine and
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
2. **`compile/begin`** wrap: **`var layout/<head> layout/block-begin`** in
   **`.ki`**, or implement a custom handler.
3. Otherwise implement **`read(reader, source, peers)`** under
   `koskari/reader/layouts/`, decorate with `@layout(symbol('layout/head'),
   peers=(...))` when peer blocks are needed, and register in the layout
   namespace.
4. Add a section to this document and a page under `docs/reference/layouts/`.
