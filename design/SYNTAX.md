# Koskari Layout Syntax

Formal BNF description of the koskari reader syntax, covering both the base
s-expression layer and the layout block layer.

Compile-time structure on special forms and macros (matcher composites such as
**DECL**, **BINDINGS**, **FORMALS**, hygiene, and **`Datum`** captures) is
documented in [MATCHER.md](MATCHER.md).

---

## Layers

The reader is built in two layers:

1. **`Reader`** — base s-expression syntax, active at all nesting depths.
2. **`LayoutReader`** — extends `Reader` with indent-sensitive block syntax,
   active only at depth 0 (outside all parentheses/brackets/braces).

---

## Two Lexical Modes

The colon character (`:`) behaves differently depending on lexical mode.

### S-expression mode

Active inside the base `Reader`, and inside any delimiter (`(`, `[`, `{`) in
`LayoutReader`.

- `:` is a **non-terminating** character — it is absorbed into atom text.
- Both leading-colon and trailing-colon keyword atoms are valid:
  `:foo` produces `keyword('foo')` and `foo:` also produces `keyword('foo')`.
- Keyword atoms may appear anywhere in s-expression context.

### Block mode

Active in `LayoutReader` at depth 0 (outside all delimiters).

- `:` is a **terminating** character — it always fires a `COLON` event
  immediately, never becoming part of an adjacent atom.
- Neither leading-colon (`:foo`) nor trailing-colon (`foo:`) keyword atoms
  can form at depth 0: `:` fires before any atom can include it.
- To pass a keyword as a block header argument it must be wrapped in a
  delimiter: `foo [:key val]:` rather than `foo :key val:`.
- Inside any `(`, `[`, or `{` within a block header, s-expression mode
  applies as normal and keywords are valid.

---

## Lexical Terminals

```bnf
<newline>       ::= "\n" | "\r\n"
<hspace>        ::= " " | "\t"
<ws>            ::= ( <hspace> | <newline> )+
<comment>       ::= ";" <any-char-except-newline>* <newline>
<ignored>       ::= ( <ws> | <comment> )*

<digit>         ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"
<hex-digit>     ::= <digit> | "a"-"f" | "A"-"F"
<atom-char>     ::= any Unicode character except <hspace> <newline>
                    "(" ")" "[" "]" "{" "}" ";" "\""
```

### S-expr mode atoms

In s-expression mode `:` is non-terminating, so atom text may include it:

```bnf
<sexpr-atom>    ::= <number> | <keyword-atom> | <symbol-atom>

<keyword-atom>  ::= ":" <atom-char>+    ; leading-colon keyword
                  | <atom-char>+ ":"    ; trailing-colon keyword

<symbol-atom>   ::= <atom-char>+        ; fallthrough when no numeric/keyword pattern matches
```

### Block mode atoms

In block mode `:` is terminating, so it can never be part of an atom:

```bnf
<block-atom>    ::= <number> | <symbol-atom>
                  ; keyword atoms are not possible at depth 0
```

### Shared literals

```bnf
<integer>       ::= "-"? <digit>+
<binary>        ::= "0b" ( "0" | "1" )+
<octal>         ::= "0o" ( "0"-"7" )+
<hex>           ::= "0x" <hex-digit>+
<float>         ::= "-"? ( ( <digit>* "." <digit>+ | <digit>+ "." <digit>* )
                              ( "e" "-"? <digit>+ )? "f"?
                          | <digit>+ "e" "-"? <digit>+ "f"?
                          | <digit>+ "f" )
<decimal>       ::= "-"? ( <digit>* "." <digit>+ | <digit>+ "." <digit>* | <digit>+ ) "d"
<fraction>      ::= "-"? <digit>+ "/" <digit>+
<complex>       ::= "-"? <digit>* "."? <digit>+ "+" <digit>* "."? <digit>* ( "i" | "j" )

<number>        ::= <integer> | <binary> | <octal> | <hex>
                  | <float> | <decimal> | <fraction> | <complex>

<escape-seq>    ::= "\" ( "\" | "'" | '"' | "a" | "b" | "f" | "n" | "r" | "t" | "v"
                        | ( "0"-"7" ){1,3}
                        | "x" <hex-digit>{2}
                        | "u" <hex-digit>{4}
                        | "U" <hex-digit>{8}
                        | "N{" <unicode-name> "}" )
<string-body>   ::= ( <escape-seq> | any-char-except-'"'-and-'\' )*
<triple-body>   ::= ( <escape-seq> | any-char )* ; terminated by first unescaped '"""'
```

---

## Base S-Expression Grammar

These rules apply inside `(`, `[`, and `{` at any nesting depth, and also at
depth 0 in `LayoutReader` when no block-header colon is found on the line.

```bnf
<form>          ::= <ignored> <form-inner>

<form-inner>    ::= <atom>
                  | <string>
                  | <raw-string>
                  | <byte-string>
                  | <format-string>
                  | <list>
                  | <bracket-list>
                  | <thunk-form>
                  | <collection>
                  | <quote-form>
                  | <quasiquote-form>

<atom>          ::= <sexpr-atom>

<string>        ::= '"' <string-body> '"'
                  | '"""' <triple-body> '"""'

<raw-string>    ::= 'r"' <raw-body> '"'
                  | 'r"""' <raw-triple-body> '"""'
                  ; raw strings do not process escape sequences

<byte-string>   ::= 'b"' <string-body> '"'
                  | 'b"""' <triple-body> '"""'
                  ; same escape rules as <string>; yields a bytes value;
                  ; non-ASCII characters in the literal are rejected (ASCII only)

<format-string> ::= 'f"' <fstring-body> '"'
                  | 'f"""' <fstring-body> '"""'

<fstring-body>  ::= ( <fstring-literal> | "{{" | "}}" | "{" <form>+ "}" )*
<fstring-literal> ::= any character except "{" and "}"

<list>          ::= "(" <form>* ( "." <form> )? ")"
                  | "[" <form>* ( "." <form> )? "]"

<thunk-form>    ::= "{" <form>* "}"
                  ; desugars to: (compile/gensym-thunk <form>*)
                  ; see "Thunk syntax" below

<collection>    ::= "#(" <form>* ")"        ; desugars to: (#tuple <form>*)
                  | "#[" <form>* "]"        ; desugars to: (#list  <form>*)
                  | "#{" <form>* "}"        ; desugars to: (#dict  <form>*)

<ref-call-form>         ::= "&(" <form>* ")"  ; desugars to: (compile/ref (<form>*))
                            ; only "&" immediately followed by "(";
                            ; otherwise see the "&" atom pattern below

<quote-form>            ::= "'" <form>      ; desugars to: (compile/quote <form>)
<quasiquote-form>       ::= "`" <form>      ; desugars to: (compile/quasiquote <form>)
                            ; inside a quasiquote, "," and ",@" become active:
<unquote-form>          ::= "," <form>      ; desugars to: (compile/unquote <form>)
<unquote-splice-form>   ::= ",@" <form>     ; desugars to: (compile/unquote-splicing <form>)

```

### Parentheses and brackets: the call/data convention

`( )` and `[ ]` are the **same list grammar** (`<list>` above). A `[ ]` form is
**not** a vector or array literal — it reads into a `pair` chain exactly like
`( )`, and **by default compiles identically**. In particular, a non-empty list
in head/operator position compiles to a *call*, regardless of which bracket
delimited it: `[f 1 2]` compiles the same as `(f 1 2)` — a call to `f`.

What differs is a marker. The reader records the bracket origin by producing a
**`bracket-pair`** (and `bracket-nil`) — a `pair` subtype that is value-equal to
and structurally interchangeable with an ordinary pair, but carries the fact
that the source used `[ ]`. That marker exists so tooling and an *opt-in*
compiler check can act on it; it does not change evaluation by itself.

The **convention** — and by default it is only a convention — is:

- **`( )` indicates a call/apply form**: the first element is applied to the
  rest. Some forms evaluate every argument; others treat selected arguments as
  data rather than expressions.
- **`[ ]` indicates a non-call data fragment**: a binding list, a clause, a
  pattern row — something the enclosing special consumes as structure, never as
  a call. Nobody reading `(let [[a 1]] (+ a 100))` expects `a` to be *called*
  with `1`; the brackets make that intent visible.

Because such data positions are consumed by their specials and never reach
`compile_pair`, they are immune for free. The payoff comes when a bracketed
fragment accidentally lands in call position — e.g. a typo `ler` for `let`:

```
(ler [[a 1]] (+ a 100))
```

This expands to a call to the value `ler` whose first argument is
`(call (call a 1))` — clearly not intended. With the bracket marker, the
compiler can diagnose it. That diagnosis is gated (see below) and defaults to
silence, so the unannotated language stays a pure convention.

#### Gating the bracket-in-call-position check

Two independent settings, both **module-scoped** and set via the pragma system
(see [PRAGMA.md](PRAGMA.md)):

- `reader/brackets` (**reader**) — whether `[ ]` reads into the distinct
  `bracket-pair` / `bracket-nil` values (default on) or into plain pairs. When
  off, the origin marker is never produced.
- `compiler/call-brackets` (**compiler**) — policy when a `bracket-pair` reaches
  call position: `ignore` (default, pure convention), `warn` (emit a
  `SyntaxWarning`), or `error` (raise at compile time).

```
(pragma [compiler/call-brackets 'error])   ; make stray brackets-in-call a hard error
```

### Thunk syntax

**Thunk syntax** is the curly-brace reader macro `{` … `}` in the base
s-expression grammar (`<thunk-form>` above). It desugars to
`(compile/gensym-thunk BODY …)`, which infers parameters from
placeholders (`%1`, `%2`, … and `...` for rest) and lowers to
`compile/lambda`. Nested `{…}` forms read as nested `(compile/gensym-thunk …)`.

See [SPECIALS.md](SPECIALS.md) (`compile/gensym-thunk`) and
[archive/brace-lambdas-gensym-partial.md](archive/brace-lambdas-gensym-partial.md).

### Reader macros and atom expansions

The default reader applies these transformations. These may be gated behind a
pragma in future.

**Ref (ampersand) — atom pattern.** An atom matching `&` followed by one or
more non-whitespace characters expands. The `&` is stripped and the remainder
is processed by the atom expander (e.g. `foo.bar` becomes `(compile/attr foo
bar)`); the result is wrapped in `(compile/ref ...)`.

| Input      | Expansion                               |
|-----------|------------------------------------------|
| `&foo`    | `(compile/ref foo)` → compile/ref expands to refq |
| `&foo.bar`| `(compile/ref (compile/attr foo bar))` → ref-attr |
| `&` alone | No match — parses as symbol `&`         |
| `(& 8 1)` | List with symbol `&` as head            |

**Ref (ampersand) — compound reader macro.** A separate reader macro fires
when `&` is *immediately* followed by an opening parenthesis `(`. It wraps the
single trailing parenthesized form in `(compile/ref ...)`. This is distinct
from the atom pattern above and never triggers on `&[...]`, `&{...}`, a
keyword, the bare `&`, or a space between `&` and `(`.

| Input             | Expansion                          |
|-------------------|------------------------------------|
| `&(item xs 2)`    | `(compile/ref (item xs 2))`        |
| `& (item xs 2)`   | Symbol `&` followed by `(item xs 2)` (space — no trigger) |

**Dotted attribute — atom pattern.** An atom that is a dotted identifier (e.g.
`foo.bar`, `obj.foo.bar`) expands to a nested `(compile/attr ...)` form,
left-associative:

```
foo.bar      ->  (compile/attr foo bar)
obj.foo.bar  ->  (compile/attr (compile/attr obj foo) bar)
```

### Dotted pairs

Inside `<list>`, a single `.` token separates the car side from the cdr:

```
(a b . c)   ->  cons(a, cons(b, c))
```

---

## Declarations, return types, and function docstrings

These are not separate reader forms: they use the same atoms, lists, and
keywords as the base grammar. They are called out here because they affect how
`compile/define`, `compile/define-global`, and `compile/lambda` are written and
what metadata the compiler records. The compiler recognizes these shapes via the
**DECL**, **FORMALS**, and related matcher composites; see
[MATCHER.md](MATCHER.md).

### DECL (alternative first argument to `compile/define` and `compile/define-global`)

The first subform may be a **declaration** — a name with no value — as a single
list or symbol:

- A **symbol** (`x`), or
- A **bracket (or parenthesis) list** with a name and optional **type**
  attributes only: `[x]`, `[x :: T]`, `[x type: T]`, or the `(EXPR as: NAME)`
  spelling for types.

**Not allowed** in a DECL: `value:`, or a two-element `(SYM EXPR)` default
(same “no RHS” rules as valueful bindings). Full behaviour and examples:
[SPECIALS.md](SPECIALS.md) (compile/define).

### Inline return type on `compile/define` and `compile/define-global`

Optional **return** type may appear **after the name** and **before** formals,
the value expression, or nothing (declare-only), using the same keywords as
`compile/lambda`:

- `NAME :: RET` or `NAME type: RET`

`RET` is one expression. For function shorthand it becomes
`__annotations__['return']` on the defined function. For declare-only or
non-function bindings the compiler currently treats the type as parse-only
(metadata); see [SPECIALS.md](SPECIALS.md) (compile/define).

### Return type on `compile/lambda`

Optional **return** type appears after an optional function **name** and before
**formals**:

- `NAME :: RET` or `NAME type: RET` when the lambda is named
- `:: RET` or `type: RET` when the lambda is anonymous (no name)

`RET` is one expression; it is **not** a formal parameter. It becomes
`__annotations__['return']` on the function (and `code.extras['return']` on the
`Code` object). See [SPECIALS.md](SPECIALS.md) (compile/lambda).

Lexically, `::` and `type:` are **keyword atoms** in s-expression mode (see
[Two Lexical Modes](#two-lexical-modes) and `<keyword-atom>`).

### Function docstrings (`compile/lambda`)

If the **first** form in the lambda body is a **string literal**, the compiler
treats it as the function’s docstring (`discover_docstring` in
`koskari/compile/helpers.py`). That string is stored as `code.extras['doc']` and
exposed on the callable as `__doc__`. If the body begins with
`(compile/begin ...)`, the implementation may peel that wrapper **only** to look
for a leading string; the body is still compiled in full.

### By-ref formals

A **formal** may be marked **by-ref** by prefixing its name with `&`. At
application the binder installs the **caller-supplied `Ref` as the slot's
storage** (rather than minting a fresh slot and copying the value in), so a
`set!` inside the callee writes through to the caller's binding. The body still
sees a plain-valued local; it just shares storage with the caller.

```
define add-a [a &result]:
    (set! result (+ a 1))

define value 100
(add-a value &value)    ; value is now 101
```

The leading `&` here is **not** the ref reader macro — it is a marker on the
formal name recognized by the **FORMALS** matcher composite (parsed into the
`by_refs` tuple on `Formals`; see [MATCHER.md](MATCHER.md) and
[SPECIALS.md](SPECIALS.md), `compile/lambda`). The call site `&value` *is* the
ordinary ref reader form. By-ref is permitted on **positional and keyword**
formals only — never on `*` (`*:`) or `**` (`**:`).

---

## Layout Block Grammar

Recognised by `LayoutReader` **only at depth 0** (outside all delimiters).
The governing rule is indentation: a block body consists of all lines whose
indent column is **strictly greater than** the indent column of the block head.

```bnf
<layout-form>   ::= <ignored> <layout-form-inner>

<layout-form-inner>
                ::= <paren-form>            ; first char in "([{" — plain s-expression
                  | <block-quote-form>      ; "'" followed by a block header and ":"
                  | <quasiquote-block-form> ; "`" followed by a block header and ":"
                  | <colon-block>           ; header line ending in structural ":"
                  | <form>                  ; fallthrough — any base s-expression form

<paren-form>    ::= <list> | <bracket-list> | <thunk-block>
                  ; thunk syntax: see "Thunk syntax" under Base S-Expression Grammar
```

### Colon block

In block mode `:` is always terminating, so a **header line** that ends with a
structural `:` (after zero or more header forms) begins a block. The first
form on that line is the **head**; it need not be a symbol.

This mirrors **lists** in the base grammar: several sibling forms in one list are
read as an implicit **call** shape (the compiler lowers multi-head application
with `compile/call`). At layout depth 0, a block whose head does **not** name
a registered layout handler is still a well-formed block: expansion uses the
default **`layout/block`** handler, which splices body forms after the head and
header arguments—i.e. an implicit **block** shape analogous to that implicit
**call** shape for lists.

```bnf
<colon-block>   ::= <block-header> <hspace>* ":" <hspace>* <comment>? <newline>
                    <block-body>
```

#### Block header

```bnf
<block-header>  ::= <header-head> <header-arg>*
                  ; first form is the block head (any <form> at depth 0)
                  ; zero or more additional argument forms follow before the colon

<header-head>   ::= <form>
                  ; typically a symbol naming a layout handler, but may be any
                  ; form (e.g. a parenthesised callee); resolution uses the head
                  ; value to select a layout handler, else layout/block

<header-arg>    ::= <form>
                  ; any form legal in base grammar, including multi-line s-exprs
                  ; the colon terminates only at depth 0; inside "(" "[" "{"
                  ; s-expression mode applies and keywords are valid
```

#### Block body

```bnf
<block-body>    ::= <layout-form>*
                  ; forms whose indent column is > outer_indent
                  ; ends on the first line whose indent column is <= outer_indent,
                  ; or at EOF

<outer_indent>  ::= column index of the block head token
```

### Block-macro prefix forms

When `'` or `` ` `` appears at depth 0 and a colon follows somewhere on the
same line at depth 0, the prefix introduces a block-macro form. Otherwise the
prefix is treated as a plain `<quote-form>` or `<quasiquote-form>`.

```bnf
<block-quote-form>
                ::= "'" <block-header> <hspace>* ":" <hspace>* <comment>? <newline>
                    <block-body>
                  ; reads raw (:block: (<head> . <args>) <body>...); layout/quote
                  ; lowers to (compile/quote (:block: ...)) at expansion

<quasiquote-block-form>
                ::= "`" <block-header> <hspace>* ":" <hspace>* <comment>? <newline>
                    <block-body>
                  ; desugars to: (compile/quasiquote-block (:block: (<head> . <args>) <body>...))
                  ; unquote ("," and ",@") is active in both the header and body
```

### Peer blocks

Certain layout heads declare **peer** head symbols. After the primary block body
closes, the reader checks the next content line. If that line begins at exactly
`outer_indent` columns and its leading token matches a declared peer symbol, the
line is consumed as an additional peer block at the same nesting level.

```bnf
<primary-block> ::= <colon-block> <peer-block>*

<peer-block>    ::= <peer-head> <header-arg>* <hspace>* ":" <hspace>* <comment>? <newline>
                    <block-body>
                  ; peer-head must be at exactly outer_indent columns
                  ; peer-head must appear in the layout handler's declared peers list

<peer-head>     ::= <block-atom>
```

---

## Known Layout Handlers

| Head symbol     | Peer heads allowed (in order)              |
|---|---|
| *(default)*     | none — any unknown head falls back to layout/block |
| `class`         | none |
| `define-syntax` | none |
| `dyn-wind`      | `before`, `body`, `after` |
| `if`            | `elif`\*, `else`? |
| `syntax-rules`  | none |
| `try`           | `except`+, `else`?, `finally`? |

(`*` = zero or more, `?` = zero or one, `+` = one or more)

**`define-syntax`** and **`syntax-rules`** layout blocks lower to macro
transformer specs whose rule patterns use the same matcher language as compile
specials; see [MATCHER.md](MATCHER.md) and [MACROS.md](MACROS.md).

### Parse and expand

Colon blocks parse as **`:block:`** conses. **`LayoutReader.read()`** calls
**`apply_layout`** or **`apply_layouts`** to invoke **`layout/<head>`** handlers
as **`read(namespace, source, peers)`**.

### Default block handler (`layout/block`)

Any block whose head does not resolve to a **named** layout handler (including
when the head is a **non-symbol** form) is expanded by **`layout/block`**:

```
head arg1 arg2 ... :
  body-form1
  body-form2
```

becomes `(head arg1 arg2 body-form1 body-form2)`.

Opt-in **`compile/begin`** wrapping uses **`layout/block-begin`**
(**`define layout/<head> layout/block-begin`** in **`.ki`**).

### `begin` block

```
begin:
  form1
  form2
```

becomes `(begin form1 form2)` via default **`layout/block`** (flat splice).

### `if` / `elif` / `else`

```
if cond:
  then-form
elif cond2:
  elif-form
else:
  else-form
```

becomes `(compile/cond (cond then-form) (cond2 elif-form) (else: else-form))`.

Rules:
- Exactly one condition argument required for `if` and each `elif`.
- `else` takes no arguments.
- `elif` may not follow `else`.
- Multiple `else` peers are an error.

### `try` / `except` / `else` / `finally`

```
try:
  body-form
except [SomeError as: e]:
  handler-form
else:
  else-form
finally:
  cleanup-form
```

becomes `(try (compile/begin body-form) ((e . SomeError) handler-form) (else: else-form) (finally: cleanup-form))`.

Rules:
- At least one `except` or one `finally` is required.
- `except` requires at least an exception type argument.
- `else` and `finally` take no arguments.
- Multiple `else` or multiple `finally` peers are an error.

---

## Notes

**Colon is always terminating in block mode.** At layout depth 0, `:` always
fires the block-opener event regardless of surrounding whitespace. There is no
probe or replay mechanism: the parser reads forward in a single pass, and the
first bare `:` ends the header.

**Keyword arguments in block headers.** Because `:` is terminating in block
mode, keyword atoms cannot appear as bare header arguments at depth 0. Wrap
them in a delimiter: `foo [:key val]:` rather than `foo :key val:`. Inside the
brackets s-expression mode applies and `:foo` / `foo:` are valid keywords.

**Multi-line header arguments.** S-expressions in the header (e.g. a `[...]`
spanning multiple lines) are consumed in full before the colon terminates the
header. A `:` inside a nested delimiter is not treated as the block opener.

**Depth guard.** Layout-block detection is suppressed when inside any `()`,
`[]`, or `{}`. Forms inside delimiters always use plain `Reader` rules,
regardless of indentation.

**Layout lines.** At delimiter depth 0, a logical line without a structural
trailing `:` may contain several top-level forms; when implicit calls are
enabled, they fold to `(first . rest…)`. Any first form may head the fold. See
[docs/reference/reader-layout.md](../docs/reference/reader-layout.md) § Layout lines.

**`#!` shebang lines.** A `#!` line at the very start of a file is skipped
automatically before any form is read.
