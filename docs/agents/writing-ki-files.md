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

## A bare form never continues onto the next line

An indented line under a header is a *child* form, never a
continuation. To span lines, open a parenthesis -- inside `(`, `[`,
or `{`, newlines and indentation are insignificant.

```koskari
; WRONG: reads as (compile_file src dest) then (source_filename: name);
; the keyword argument is silently dropped
compile_file src dest
    source_filename: name

; correct: parentheses span lines freely
(compile_file src dest
    source_filename: name)
```

The same applies to `define`: `define x` followed by an indented value
line reads as `(define x)` plus a separate expression, failing with
`SyntaxMatchError` at compile.

## The block colon is layout-only

A `:` opens an indented block only at the end of a layout line. Inside
parentheses it reads as a bare symbol and corrupts the form's argument
positions.

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
