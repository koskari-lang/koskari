# Grammar

Koskari source combines layout syntax (indentation-based blocks and lines
in `.ki` files) with s-expressions (parenthesized forms). The reader turns
both into list structure; the compiler then expands macros and dispatches on
special forms.

## S-expression forms

Inside any `(`, `[`, or `{`, and at the top level of a form, the base reader
applies. It reads atoms and lists and a handful of prefix and
collection syntaxes. The everyday shapes are in
[Model of evaluation](../guide/model-of-evaluation.md).

Whitespace is the only separator. Tokens are split on spaces, tabs, and
newlines (and on the delimiter characters). Punctuation that other languages
use to separate items -- most notably the comma -- is an ordinary atom
character here, so `#[1, 2, 3]` reads the symbols `1,` and `2,` followed by
the integer `3`, not three integers. Separate list items with spaces.

### Atoms

An atom is one whitespace- or delimiter-bounded token. The reader matches each
token against the literal patterns below; a token that matches none of them
is a symbol.

Numbers. Each numeric shape is a distinct literal:

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

Strings are double-quoted, with optional single-character prefixes attached
directly to the opening quote:

| Written | Meaning |
|---|---|
| `"text"` | plain string |
| `f"x is {x}"` | format string -- interpolates `{...}` expressions |
| `r"a\d+"` | raw string -- no backslash escapes |
| `b"bytes"` | bytes literal -- ASCII text plus escapes for any byte (`b"\xff"`); a literal non-ASCII character is an error |

A prefix is recognised only when it sits immediately before the quote (`f"..."`,
not `f "..."`). A trailing hyphen continues a string into the next quoted segment;
see [Suffix syntax](#suffix-syntax).

Symbols are names: any token that is not a number, string, keyword, or
singleton. Symbols may contain letters, digits, `-`, `_`, `/`, and most
punctuation (`+`, `<`, `list->vector`, `None?` are all symbols). A token
that matches a numeric pattern is a number; any other token is a symbol,
so `2x` and `1st` are symbols.

Keywords are self-evaluating name markers written with a colon on either
end. `:foo` and `foo:` both read as the keyword `foo`; the trailing-colon
spelling is what carries keyword arguments in calls and headers.

Singletons and constants:

| Written | Reads as |
|---|---|
| `True`, `False` | Python booleans |
| `None` | Python `None` |
| `...` | `Ellipsis` |
| `()`, `[]` | the empty list, `nil` |

`nil` is both the empty list and the terminator of every proper list.

### Lists

Parentheses group forms into a pair chain (a cons list):

```koskari
(+ 1 2 3)         ;; a three-argument call
(a b (c d) e)     ;; nested lists
()                ;; the empty list, nil
```

A dotted pair places a value directly in the tail with `.`:

```koskari
(k . v)           ;; one cons cell: car k, cdr v
(1 2 . 3)         ;; improper list ending in 3
```

Square brackets `[ ]` read with the same grammar as `( )` and produce a
value-equal chain; the reader records the bracket style so a call-position lint
can flag it, but the compiler treats the two the same. See
[Brackets and the call convention](../guide/brackets-and-call-convention.md).

### Collection literals

A `#` prefix on a delimiter reads a container literal whose items are
whitespace-separated:

| Written | Reads as | Builds |
|---|---|---|
| `#(1 2 3)` | `(#tuple 1 2 3)` | a tuple |
| `#[1 2 3]` | `(#list 1 2 3)` | a list |
| `#{"a": 1 "b": 2}` | `(#dict (#tuple "a" 1) (#tuple "b" 2))` | a dict |

A `#dict` spine is a sequence of entries. Four entry forms are accepted:

| Entry | Example | Reads as |
|---|---|---|
| `key: value` | `#{name: n}` | `(#dict (#tuple name n))` |
| `KEY : VALUE` | `#{"name": n}`, `#{(str 1) : "one"}` | `(#dict (#tuple "name" n))` |
| `**: EXPR` | `#{**: other}` | `(#dict **: other)` |
| `(key . value)` | `#{("a" . 1)}` | `(#dict ("a" . 1))` |

Keys are **ordinary expressions**, so a `key:` key lowers to the name `key`,
not the string `"key"` -- quote or write the string explicitly (`#{"name": n}`)
when you want a literal string key. The `KEY : VALUE` spelling takes any key
expression, with the `:` standing alone as its own token. A dotted entry
evaluates both halves the same way, and its value must be an atom:
`("a" . (+ 1 2))` is the proper list `("a" + 1 2)`, which compiles as a call
to `"a"`.

Alternating `key value` items are not an entry form. `#{"a" 1}` reads as
`(#dict "a" 1)`, and the call raises `ValueError: build_dict items must be
(key, value) pairs`.

Classification is all-or-nothing: one entry the reader cannot classify leaves
the whole spine raw, so mixing a dotted entry with `key: value` entries fails
even though each form works on its own.

### Thunk braces

Braces read their contents as the body of a thunk:

| Written | Reads as |
|---|---|
| `{(+ %1 %2)}` | `(gensym-thunk (+ %1 %2))` |

The body is a form sequence, so the two-argument thunk is spelled
`{(+ %1 %2)}` -- the inner parentheses are the call, and the braces only
delimit the body. The `%N` placeholders become the formals. See
[`gensym-thunk`](specials/compile-gensym-partial.md).

```koskari
>>> (define add {(+ %1 %2)})
>>> (add 3 4)
7
>>> (list (map {(* %1 %1)} #[1 2 3]))
#[1 4 9]
```

### Prefix reader sugar

Seven prefixes wrap the form that follows them:

| Written | Reads as |
|---|---|
| `'form` | `(quote form)` |
| `` `form `` | `(quasiquote form)` |
| `#'form` | `(quote-syntax form)` |
| `` #`form `` | `(quasiquote-syntax form)` |
| `,form` | `(unquote form)` |
| `,@form` | `(unquote-splicing form)` |
| `-'form` | `(datumq form)` |

In layout mode the four quoting prefixes also take a block head: `'head:`,
`` `head: ``, `#'head:`, and `` #`head: `` quote the indented block as its
`#block:` cons; see [Reader and layout syntax](reader-layout.md).

`,` and `,@` are meaningful only inside a quasiquote. `-'form` writes `form`
raw: in a macro template it is emitted unmarked, and `-'-'form` nests one level
per template; see [Hygiene](../guide/hygiene.md#writing-a-name-the-use-site-shares). `&form` reads as
`(ref form)` for by-reference passing; a bare `&` with nothing attached is the
ordinary `&` symbol.

The unary operators `-`, `+`, and `~` are prefixes too. Attached to a name, a
digit, or an opening delimiter, each reads as the one-argument call of its
operator: `-x` is `(- x)`, `+foo.bar` is `(+ (attr foo bar))`, `~(f y)` is
`(~ (f y))`, and `~5` is `(~ 5)`. The operand is read as a whole form, so
`-foo["a"]` is `(- (item foo "a"))`. The one-argument builtins call the Python
unary methods, so `+x` reaches `__pos__`, which is how a `Decimal` rounds to
its context and a `Counter` drops its non-positive counts. A `-` before a
digit is a signed number, so `-1` and `-.5` are literals rather than calls. An
operator standing alone is its bare symbol, and one followed by any other
character, `->`, `-=`, `--x`, and `+=` included, is an ordinary symbol; a `-`
before a quote is the `-'` prefix above, whatever the pragma. The unary
prefixes are governed by the `reader/unary` pragma.

### Fenced literals

A triple-backtick fence captures its body as raw text and reads as a
`compile/apply-fence` form naming the format given on the opener line:

```text
(apply-fence NAME RAW META)
```

The compiler resolves `fence/<name>` in the module namespace and compiles the
source the handler returns in place of the fence. The `reader/fences` pragma
governs the syntax; with it off, a backtick keeps only its quasiquote reading.

````koskari
define shout ```bash
echo -n "$1" | tr a-z A-Z
```

(print (shout "koskari").text)
````

See [Fence handler reference](fences/index.md) for the handlers that ship and
for defining new ones.

### Comments

A `;` begins a line comment that runs to the end of the line; the doubled
`;;` is a common style and reads the same. Comments may appear between
any tokens and are skipped by the reader.

## Suffix syntax

Three pieces of syntax attach to the value just read, wrapping it rather
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

Attachment is strict. The suffix character must follow the value with no
space between them. A space ends the value instead, which is what keeps
`foo [a]` two separate forms and leaves the leading-dot method call
`(.baz obj)` alone.

Index brackets (`foo[k]`) read an item access, and a group may carry
several keys (`foo[1 2]`). An empty group is an error.

Dots read an attribute. This is the same expansion whether the dot sits
inside a single name (`obj.value`) or attaches to a form or literal
(`(get-fn x).baz`, `"".join`) -- the dot is the attribute read, and enclosing
parentheses are what make it a call. A dot with no name after it is an error.

A trailing hyphen on a string continues that literal into the next quoted
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

Each of the three is governed by its own setting -- `reader/index-brackets`,
`reader/dot-attrs`, and `reader/string-segments`. See
[Pragmas and feature settings](../guide/pragma.md) for turning them off.

## Decorator lines

A layout line beginning with `@` is a decorator line. One or more `@` lines,
followed by the form they decorate, read as
`(compile/decorate (DECORATOR ...) FORM)`:

```koskari
define loud [f]:
  lambda [*: args]:
    (print "calling")
    (f *: args)

@loud
define greet [who]:
  (print "hello" who)

(greet "world")
```

The call prints `calling`, then `hello world`.

Every `@` line must begin at the same column as the decorated form; a
mismatched column is the error `decorator lines must align with the decorated
form`. An `@` with nothing after it on the line is `empty decorator line`.

The `reader/decorators` pragma governs this layout surface only. With it off,
the [`compile/decorate`](macros/compile-decorate.md) macro is still available
in its s-expression spelling.

## Layout handlers

Per-head read-time lowering (`layout/if`, `layout/block`, ...) is documented in
the [Layout handler reference](layouts/index.md).

See also: [Koskari and s-expressions](../guide/layout-and-s-expressions.md) for
the layout rules beside their s-expressions, and
[Reader and layout syntax](reader-layout.md) for the normative syntax rules.
