# Grammar

Koskari source combines **layout syntax** (indentation-based blocks and lines
in `.ki` files) with **s-expressions** (parenthesized forms). The reader turns
both into list structure; the compiler then expands macros and dispatches on
special forms.

---

## S-expression forms

Inside any `(`, `[`, or `{`, and at the top level of a form, the base reader
applies. It reads **atoms** and **lists** and a handful of **prefix** and
**collection** syntaxes. Everyday shapes are covered in
[Forms and combining](../guide/forms-and-combining.md); this section is the
normative catalogue.

**Whitespace is the only separator.** Tokens are split on spaces, tabs, and
newlines (and on the delimiter characters). Punctuation that other languages
use to separate items — most notably the **comma** — is an ordinary atom
character here, so `#[1, 2, 3]` reads three symbols `1,` `2,` `3`, not three
integers. Separate list items with spaces.

### Atoms

An atom is one whitespace- or delimiter-bounded token. The reader matches each
token against the literal patterns below; a token that matches **none of them**
is a **symbol**.

**Numbers.** Each numeric shape is a distinct literal:

| Written | Reads as | Notes |
|---|---|---|
| `42`, `-7` | integer | |
| `0xff`, `0o17`, `0b1010` | integer | hex digits are lowercase |
| `3.14`, `.5`, `6.` | float | |
| `1e9`, `2.5e-3` | float | exponent form |
| `3f`, `1.0f` | float | trailing `f` forces float |
| `1/2` | fraction | `fractions.Fraction` |
| `9.99d`, `5d` | decimal | trailing `d`, `decimal.Decimal` |
| `3+4j`, `2.0+1i` | complex | trailing `i` or `j` |

**Strings** are double-quoted, with optional single-character prefixes attached
directly to the opening quote:

| Written | Meaning |
|---|---|
| `"text"` | plain string |
| `f"x is {x}"` | format string — interpolates `{…}` expressions |
| `r"a\d+"` | raw string — no backslash escapes |
| `b"bytes"` | bytes literal — ASCII text plus escapes for any byte (`b"\xff"`); a literal non-ASCII character is an error |

A prefix is recognised only when it sits immediately before the quote (`f"…"`,
not `f "…"`). A trailing hyphen continues a string into the next quoted segment;
see [Suffix syntax](#suffix-syntax).

**Symbols** are names: any token that is not a number, string, keyword, or
singleton. Symbols may contain letters, digits, `-`, `_`, `/`, and most
punctuation (`+`, `<`, `list->vector`, `None?` are all symbols). A leading digit
generally makes a number instead, so names start with a non-digit.

**Keywords** are self-evaluating name markers written with a colon on either
end. `:foo` and `foo:` both read as the keyword `foo`; the trailing-colon
spelling is what carries keyword arguments in calls and headers.

**Singletons and constants:**

| Written | Reads as |
|---|---|
| `True`, `False` | Python booleans |
| `None` | Python `None` |
| `...` | `Ellipsis` |
| `()`, `[]` | the empty list, `nil` |

`nil` is both the empty list and the terminator of every proper list.

### Lists

Parentheses group forms into a **pair chain** (a cons list):

```koskari
(+ 1 2 3)         ;; a three-argument call
(a b (c d) e)     ;; nested lists
()                ;; the empty list, nil
```

A **dotted pair** places a value directly in the tail with `.`:

```koskari
(k . v)           ;; one cons cell: car k, cdr v
(1 2 . 3)         ;; improper list ending in 3
```

Square brackets `[ ]` read with the **same grammar** as `( )` and produce a
value-equal chain; the reader records the bracket style so a call-position lint
can flag it, but the compiler treats the two the same. See
[Brackets and the call convention](concepts/brackets-and-call-convention.md).

### Collection literals

A `#` prefix on a delimiter reads a container literal whose items are
whitespace-separated:

| Written | Reads as | Builds |
|---|---|---|
| `#(1 2 3)` | `(#tuple 1 2 3)` | a tuple |
| `#[1 2 3]` | `(#list 1 2 3)` | a list |
| `#{"a": 1 "b": 2}` | `(#dict (#tuple "a" 1) (#tuple "b" 2))` | a dict |

A `#dict` spine accepts alternating `key value` pairs, dotted `(key . value)`
entries, or `key:` keyword keys (`#{name: n}`). Keys are **ordinary
expressions**, so a `key:` key lowers to the *name* `key`, not the string
`"key"` — quote or write the string explicitly (`#{"name": n}`) when you want a
literal string key.

### Prefix reader sugar

Four prefixes wrap the form that follows them:

| Written | Reads as |
|---|---|
| `'form` | `(quote form)` |
| `` `form `` | `(quasiquote form)` |
| `,form` | `(unquote form)` |
| `,@form` | `(unquote-splicing form)` |

`,` and `,@` are meaningful only inside a quasiquote. One more prefix, `&form`,
reads as `(ref form)` for by-reference passing; a bare `&` with nothing
attached is the ordinary `&` symbol.

### Comments

A `;` begins a line comment that runs to the end of the line (the doubled `;;`
is a common style, still just a line comment). Comments may appear between any
tokens and are skipped by the reader.

---

## Suffix syntax

Three pieces of syntax attach to the **value just read**, wrapping it rather
than starting something new. They read left to right and chain freely with each
other:

| Written | Reads as |
|---|---|
| `foo[0]` | `(item foo 0)` |
| `grid[y][x]` | `(item (item grid y) x)` |
| `obj.value` | `(attr obj value)` |
| `(get-fn x).baz` | `(attr (get-fn x) baz)` |
| `("hello".upper)` | a method call on a literal |
| `foo[0].bar` | `(attr (item foo 0) bar)` |
| `"a"- " b"` | `(#str "a" " b")` |

**Attachment is strict.** The suffix character must follow the value with no
space between them. A space ends the value instead, which is what keeps
`foo [a]` two separate forms and leaves the leading-dot method call
`(.baz obj)` alone.

**Index brackets** (`foo[k]`) read an item access, and a group may carry
several keys (`foo[1 2]`). An empty group is an error.

**Dots** read an attribute. This is the same expansion whether the dot sits
inside a single name (`obj.value`) or attaches to a form or literal
(`(get-fn x).baz`, `"".join`) — the dot is the attribute read, and enclosing
parentheses are what make it a call. A dot with no name after it is an error.

**A trailing hyphen on a string** continues that literal into the next quoted
segment, so a long string can be broken across lines without restructuring the
call around an explicit join:

```koskari
(print "This is one"-
       " long string broken"-
       " up across lines")
```

Segments may be separated by any whitespace, including newlines and blank
lines, and comments in between are skipped. Each segment carries its own
prefix, so an `f` segment interpolates and an `r` segment stays raw. All-literal
chains fold to a single constant, so the join costs nothing at run time.

Each of the three is governed by its own setting — `reader/index-brackets`,
`reader/dot-attrs`, and `reader/string-segments`. See
[Pragmas and feature settings](concepts/pragma.md) for turning them off and for
the exact rules at the edges.

---

## Layout forms

Layout is the default surface at the top level of a module. Rules for blocks,
lines, colon handling, and handler resolution live in dedicated reference pages:

- [Koskari and s-expressions](concepts/layout-and-s-expressions.md) — the rules beside their s-expressions
- [Reader and layout syntax](reader-layout.md) — normative syntax rules

---

## Layout handlers

Per-head read-time lowering (`layout/if`, `layout/block`, …) is documented in
the [Layout handler reference](layouts/index.md).
