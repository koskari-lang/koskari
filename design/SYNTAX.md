# Koskari Layout Syntax

Formal BNF description of the koskari reader syntax, covering both the base
s-expression layer and the layout block layer.

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
                  | <format-string>
                  | <list>
                  | <bracket-list>
                  | <begin-block>
                  | <collection>
                  | <quote-form>
                  | <quasiquote-form>

<atom>          ::= <sexpr-atom>

<string>        ::= '"' <string-body> '"'
                  | '"""' <triple-body> '"""'

<raw-string>    ::= 'r"' <raw-body> '"'
                  | 'r"""' <raw-triple-body> '"""'
                  ; raw strings do not process escape sequences

<format-string> ::= 'f"' <fstring-body> '"'
                  | 'f"""' <fstring-body> '"""'

<fstring-body>  ::= ( <fstring-literal> | "{{" | "}}" | "{" <form>+ "}" )*
<fstring-literal> ::= any character except "{" and "}"

<list>          ::= "(" <form>* ( "." <form> )? ")"
                  | "[" <form>* ( "." <form> )? "]"

<begin-block>   ::= "{" <form>* "}"
                  ; desugars to: (compile/begin <form>*)

<collection>    ::= "#(" <form>* ")"        ; desugars to: (#tuple <form>*)
                  | "#[" <form>* "]"        ; desugars to: (#list  <form>*)
                  | "#{" <form>* "}"        ; desugars to: (#dict  <form>*)

<quote-form>            ::= "'" <form>      ; desugars to: (compile/quote <form>)
<quasiquote-form>       ::= "`" <form>      ; desugars to: (compile/quasiquote <form>)
                            ; inside a quasiquote, "," and ",@" become active:
<unquote-form>          ::= "," <form>      ; desugars to: (compile/unquote <form>)
<unquote-splice-form>   ::= ",@" <form>     ; desugars to: (compile/unquote-splicing <form>)
```

### Dotted pairs

Inside `<list>`, a single `.` token separates the car side from the cdr:

```
(a b . c)   ->  cons(a, cons(b, c))
```

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
                  | <colon-block>           ; bare atom followed by ":"
                  | <form>                  ; fallthrough — any base s-expression form

<paren-form>    ::= <list> | <bracket-list> | <begin-block>
```

### Colon block

In block mode `:` is always terminating, so any bare atom followed by `:` on
the same line begins a block:

```bnf
<colon-block>   ::= <block-header> <hspace>* ":" <hspace>* <comment>? <newline>
                    <block-body>
```

#### Block header

```bnf
<block-header>  ::= <block-atom> <header-arg>*
                  ; first token is the block head symbol
                  ; zero or more argument forms follow before the colon

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
                  ; desugars to: (compile/quote (:block: (<head> . <args>) <body>...))

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
| *(default)*     | none — any unknown head falls back to the default block handler |
| `begin`         | none |
| `if`            | `elif`\*, `else`? |
| `try`           | `except`+, `else`?, `finally`? |

(`*` = zero or more, `?` = zero or one, `+` = one or more)

### Default block handler

Any block head not registered as a named layout expands as:

```
head arg1 arg2 ... :
  body-form1
  body-form2
```

becomes `(head arg1 arg2 (compile/begin body-form1 body-form2))`.

### `begin` block

```
begin:
  form1
  form2
```

becomes `(compile/begin form1 form2)`.

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

**`#!` shebang lines.** A `#!` line at the very start of a file is skipped
automatically before any form is read.
