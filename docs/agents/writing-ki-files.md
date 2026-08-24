# Writing Koskari source files: rules for agents

Normative parse-level rules for writing a syntactically correct `.ki`
file: the layout rules for lines, indentation, and blocks, and the
reader syntax that most often surprises a code generator. Every rule
here is about how source *reads*; a violation either fails with a
reader error or silently reads as something other than intended. For
the full syntax catalogue see [Grammar](../reference/grammar.md); for
semantic traps that parse fine, see [Pitfalls](pitfalls.md).

---

## A line is one form

In layout (the default surface of a `.ki` file), each line is exactly
one form. Multiple expressions on a line become an implicit call; a
single expression stands alone.

```koskari
print "total:" n     ; reads as (print "total:" n)
(process items)      ; a parenthesized form is itself
return               ; expands to (return) via its bare form
total                ; a lone name with no bare form is a reference
```

The statement words `return`, `continue`, `break`, `raise`, and
`yield` carry a *bare form*: the lone symbol expands to its call
form, so a `return` line returns None and a lone `raise` re-raises.
Prefer the explicit `(return)` spelling anyway -- the
`compiler/bare-control-words` pragma can make the bare spelling warn
or error. Any other lone name is an ordinary variable reference
unless a syntax was defined with the `bare-syntax` kind; see
[Name resolution](../reference/concepts/name-resolution-and-prefixes.md).

## Trailing `:` continues a call; its absence ends one

A header line ending in `:` is not complete -- it keeps reading
arguments from the following indented lines, one argument per line,
recursively (a line ending in its own `:` opens a further nested
continuation). This rule is uniform across every header: `define`,
`set!`, `if`, `try`, `list-each`, any call at all. There is no
distinct "block" construct; a colon-terminated header plus its
indented lines *is* one parenthesized form, mechanically:

```koskari
define parts:
    list-each [x (range 0 10)]:
        + x 250
```

is identical to

```koskari
(define parts (list-each [x (range 0 10)] (+ x 250)))
```

A header line with **no** trailing `:` has already completed as its
own form. An indented line beneath it is an unrelated sibling, not an
argument -- this is the trap, not the colon:

```koskari
; WRONG: reads as (compile_file src dest) then (source_filename: name);
; the keyword argument is silently dropped
compile_file src dest
    source_filename: name

; WRONG: reads as (define parts) then a disconnected list-each form
define parts
    list-each [x (range 0 10)]:
        + x 250

; correct: parentheses span lines freely regardless of any colon
(compile_file src dest
    source_filename: name)
```

`define x` (bare, no colon, nothing after it) failing to attach a
value on the next line is this same rule, not a `define`-specific
exception: `define x` is already `(define x)`; the value has to be on
that line, or the header needs the trailing `:` shown above.

## The block colon is layout-only

The continuation rule above applies only to a `:` at the end of a
layout line. Inside parentheses -- where newlines and indentation are
already insignificant -- a `:` reads as an ordinary bare symbol
instead, and corrupts the form's argument positions:

```koskari
; WRONG: `:` becomes an argument of unless
(unless (None? x): (handle x))

; correct s-expression form
(unless (None? x) (handle x))

; correct layout form
unless (None? x):
    handle x
```

## Brackets are not lists

`( )` and `[ ]` read with the same grammar into the same structure;
`[1 2 3]` is a call of `1`, not a list. Collection literals use the
hash forms.

```koskari
#[1 2 3]        ; list
#(1 2 3)        ; tuple
#{"a": 1}       ; dict
```

See [Brackets and the call convention](../reference/concepts/brackets-and-call-convention.md).

## Whitespace is the only separator

The comma is an ordinary atom character: `#[1, 2, 3]` reads three
comma-bearing atoms, not a three-element list. Never write commas
between elements or arguments.

## Strings do not concatenate by adjacency

Two adjacent string literals are two separate forms (and on one layout
line, an implicit call of the first). Joining adjacent literals uses
the segment marker `-` touching the closing quote:

```koskari
"Hello, "-
f"{name}!"      ; one joined string
```

Format-string slots take expressions; there are no `!r` / `!s`
conversion suffixes -- call `(repr x)` or `(str x)` in the slot.

## Calls on results, attributes, and refs

Dotted attribute access hangs off any expression, not only a name:
`path.stem`, `(rows.sort key: fn)`, and equally a parenthesized
result -- the dot suffix attaches to the closing paren. This is the
preferred spelling for calling a method on the result of a call:

```koskari
((Path root).joinpath "docs")   ; preferred: dot suffix on the result
(.joinpath (Path root) "docs")  ; valid, but stylistically outdated
```

Both compile the same way; write the dot-suffix form.

Chaining two method calls needs a paren around each call step, not
around the whole chain. A dot with no enclosing parens is always an
*attribute read*, never a call -- `x.strip.lower` reads as
`(attr (attr x strip) lower)`: an attribute lookup on the unbound
`strip` method object, calling nothing. Parenthesize each call and let
the next dot suffix onto that call's *result*:

```koskari
((x.strip).lower)   ; correct: call strip, then call lower on its result
(x.strip.lower)     ; WRONG: attribute access only, strip is never called
```

## Subscripts use the bracket suffix

Item access is a bracket suffix on the value, exactly as in Python:
`row["name"]`, `grid[y][x]`, `(line.split)[0]`. The suffix attaches
with no space and chains freely with the dot suffix. `(item obj key)`
is the form it reads as and is valid to write, but the suffix is the
preferred spelling:

```koskari
config["port"]                  ; preferred: bracket suffix
(item config "port")            ; valid, but spell it as a suffix
(set! counts[word] (+ 1 n))     ; a subscript is a store target too
```

Keep subscripts out of f-string slots: compute
`define port config["port"]` first and interpolate `{port}`.

Rest parameters in formals are `*:` and `**:` --
`define f [a *: rest **: kwargs]:`. Iteration binds a variable
directly: `for-each [x items]:` takes a `[var seq]` binding, not a
lambda. `&name` yields the reference cell for `name`; a plain `name`
is its value.

---

Full normative rules: [Grammar](../reference/grammar.md) and
[Reader and layout syntax](../reference/reader-layout.md). Reader
failures raise `ReaderSyntaxError`, `ReaderEOF`, or `LayoutError`; see
the [Error guide](../reference/error-codes.md).
