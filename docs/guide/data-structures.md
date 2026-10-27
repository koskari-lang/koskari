# Data structures

Python's containers for data, and the Lisp pair for code.

## Python containers

The collection literals build ordinary Python objects:

```koskari
#[1 2 3]              ;; list
#(1 2 3)              ;; tuple
#{"a": 1 "b": 2}      ;; dict
```

Items are separated by whitespace. A `*:` inside a list or tuple literal
splices an iterable in place, and the result is always a fresh object:

```koskari
#[*: xs 1 2]
#(1 *: xs)
```

Dict keys are expressions. A string key is quoted; a bare `name:` key
looks up the variable `name`; a parenthesized key is computed:

```koskari
#{"name": "Ada" "score": 10}
#{(key-for user): (score-for user)}
```

Access and update use the same suffixes as Python:

```koskari
xs[0]
d["a"]
set! xs[0] 9
set! d["k"] v
(item-slice xs 1)        ;; xs[1:]
(item-slice xs 0 2)      ;; xs[:2]
```

Everything a Python object does, it does here: `(xs.append 4)`,
`(d.get "k" 0)`, `(len xs)`, `(in 2 xs)`, `(sorted xs)`. Call a method
on the binding, `(d.get "k")`, rather than through the type.

## Pairs

The pair is the Lisp cons cell. `cons` builds a chain, `car` is its
first element, and `cdr` is the rest:

```koskari
(cons 1 2 3 nil)     ;; (1 2 3)
(car '(1 2 3))       ;; 1
(cdr '(1 2 3))       ;; (2 3)
(cons 0 '(1 2))      ;; (0 1 2)
```

`cons` is variadic: the last argument is the tail, and `nil` ends a
proper list. `nil?` tests for the empty list, `pair?` for a cell.
`first` and `second` are aliases for `car` and `cadr`.

`nil` is not `None`. `(nil? nil)` is true and `(None? nil)` is false;
each predicate answers only for its own value.

Pairs are the structure a program is made of: every form the reader
produces is a pair chain, and a macro takes them apart with `car` and
`cdr` and builds its output with `cons`. Use them for that, and for a
problem that is naturally one item and the rest. Use `#[...]` for data
that Python libraries will see.

## Quoting

A form in head position is a call. Quote it and it stays data:

```koskari
(quote (red green blue))
'(red green blue)
```

The single quote is the reader's shorthand for `quote`. A quoted symbol
is a symbol value, `'red`, and a quoted list is a pair chain of them.

Quasiquote is the template form. A backtick quotes the structure and a
comma evaluates one part inside it; `,@` splices a sequence:

```koskari
var name "Ada"
`(hello ,name)          ;; (hello "Ada")
`(1 ,@#[2 3] 4)         ;; (1 2 3 4)
```

Quasiquote is how a macro template is usually written; see
[Macros and hygiene](macros-and-hygiene.md).

## Symbols and keywords

A symbol is an interned name: `'foo`. A keyword is a self-evaluating
marker written with a trailing colon, `foo:`, used for keyword arguments
and as markers in forms such as `[else: ...]`. `(symbol? x)` and
`(keyword? x)` classify them. An unquoted keyword in value position
compiles to itself, which the `compiler/keyword-literals` pragma warns
about, since it is more often a typo for a variable.

## Choosing

| You have | Use |
|----------|-----|
| application data, anything a Python API will touch | `#[...]`, `#(...)`, `#{...}` |
| code as data, macro input and output | pairs, quote, quasiquote |
| a name as a value | a symbol |
| an argument label or a structural marker | a keyword |
