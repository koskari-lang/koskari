# Match patterns

`match` destructures a value against a series of case patterns: the
first arm whose pattern matches binds its captured names and runs its
body. It is the sibling of
[`switch`](../reference/specials/compile-switch.md), which dispatches on
equality. `match` is for the shape of a value; `switch` compares it
against candidates.

```koskari
match msg:
  case #[(tag :: keyword) (id :: int) *: args]:
    (handle tag id args)
  case #{"kind": "job" "url": u}:
    (fetch u)
  case Point[x y]:
    (plot x y)
  else:
    (ignore msg)
```

The form itself is [`compile/match`](../reference/specials/compile-match.md)
and [`layout/match`](../reference/layouts/layout-match.md).

## Patterns are classified by head, not by bracket

`()` and `[]` are value-equal, so a pattern's kind is never decided by
its bracket. Each kind announces itself with a head symbol or marker,
and the reader sugars `#[...]`, `#{...}`, and `Name[...]` produce those
heads. A bare symbol is a capture; a bare atom is a literal; everything
structured says what it is.

## Element vocabulary

| Pattern | Written | Meaning |
|---------|---------|---------|
| capture | `x` | bind this position, or the whole subject, to `x` |
| literal | `5`, `"s"`, `foo:`, `'sym` | the position must equal the value |
| typed capture | `x :: T` | bind `x`; the value must satisfy the type or predicate `T` |
| type check | `(:: T)` | check only, no binding |
| sequence | `#[a b c]` or `#(a b c)` | destructure a sequence by position |
| rest | `#[a *: rest]` | bind the remaining elements to `rest` |
| mapping | `#{KEY: PAT ...}` | the value must be a mapping; see below |
| class | `Point[x y]` | an isinstance check plus field extraction |
| class, keyword fields | `Point[x: 0 y]` | attribute `x` must equal 0; bind attribute `y` |
| extraction | `(app f PAT)` | call `f` on the value and match `PAT` against the result |
| predicate | `(? pred)` | `pred` called on the value must be truthy |
| alternation | `(or PAT PAT ...)` | the first alternative that matches |
| negation | `(not PAT)` | the value must not match `PAT` |

Patterns nest: a sequence element may be a class pattern, a mapping
value may be a sequence, an extraction result may be destructured, as
in `(app splitext #[base "." "ki"])`.

The matcher heads from [`koskari.matcher`](../reference/api/koskari-matcher.md),
`FORM`, `ONEOF`, `REST`, `CLASS`, and the rest, are also accepted in
pattern position; the sugared forms above lower to them.

### Sequences

A sequence pattern is the explicit `#[...]` or `#(...)` form; a bare
list is never a sequence. The length must agree unless a `*: rest`
marker captures the remaining elements. Inside a sequence a typed
element is parenthesized: `#[(x :: int) y]`.

Only sequences destructure. A sequence pattern matches lists, tuples,
and other `collections.abc.Sequence` instances such as `range`, the
same rule as Python's PEP 634. `str`, `bytes`, and `bytearray` never
destructure into characters; mappings never destructure into their keys,
which belong to `#{...}` arms; sets are not sequences; and an iterator
that reaches a sequence arm is not consumed. Each of these is a
no-match, so a string still matches as a whole value, as a literal, a
capture, or `s :: str`, and a dict still matches mapping arms.

### Classes

`Point[x y]` checks `isinstance(value, Point)` and extracts fields.
Positional fields resolve through the class's `__match_args__`, which
dataclasses and named tuples provide on Python 3.10 and later; on 3.9,
which predates `__match_args__`, Koskari synthesizes the equivalent for
dataclasses and namedtuples, and any other class needs an explicit
`__match_args__`. Keyword fields, `Point[x: 0 y]`, read attributes and
work on any class. Field patterns nest: `Point[(x :: int) y]`. For an
isinstance check with no field extraction, write the type-check pattern
`(:: Point)`; an empty bracket group is an index suffix to the reader,
not a pattern.

### Mappings

`#{KEY: PAT ...}` matches a `dict` or any `collections.abc.Mapping`.
Every listed key must be present and its value must match the paired
pattern; extra keys are ignored. A literal key is written as a literal,
`#{"kind": "job"}`. A symbol key is resolved from the enclosing scope at
match time, so keys can be computed:

```koskari
define field "url"

match msg:
  case #{field: u}:      ; the key is whatever field holds, "url"
    (fetch u)
```

### Names resolve from the enclosing scope

A name a pattern references, the `T` in `x :: T`, the class in
`Point[...]`, the `pred` in `(? pred)`, the `f` in `(app f ...)`, a
symbol mapping key, is an ordinary binding resolved from the enclosing
scope at match time. Closure-captured and local names work as they
would in the body.

Seven type names are the exception: `int`, `str`, `list`, `tuple`,
`pair`, `symbol` and `keyword` in a pattern always mean the builtin
types. The pattern tests them directly, so a match arm reads nothing
from the namespace for them, and rebinding one of those names in the
enclosing scope does not change what the pattern tests:

```koskari
define int float            ; does not reach the pattern below

match 2.5:
  case x :: int:
    "unreached: int in a pattern is the builtin"
  case x:
    "taken"
```

To test against your own type, bind it under another name.

### Callables are called as functions

A predicate or extractor is an ordinary callable, and a plain Koskari
`define` works without wrapping:

```koskari
define splitext [name]:
    (return (name.rpartition "."))

match path:
  case (app splitext #[base "." "ki"]):
    (compile-source base)
```

The call is a call from the function the `match` is in, so a
predicate or extractor that awaits does so in place, as a call in the
arm body would. A predicate or extractor that raises propagates the
error rather than counting as a no-match.

## Guards

A `when` or `unless` guard runs after the pattern has matched and bound,
with the captured names in scope. A failed guard moves to the next arm:

```koskari
match v:
  case #[a b] when (> a b):
    "descending"
  case #[a b]:
    "ascending-or-equal"
```

Captured names stay bound when a guard fails, as after Python's
`case ... if ...`.

## Binding

- Captured names are bound in the enclosing scope and stay visible after
  the `match` form, like every other binding and like Python's `match`.
  Arms are not scopes.
- Captures are sparse: a slot that did not bind, an `OPTIONAL` arm that
  did not fire, is left unbound, and reading it raises `NameError`. Bind
  a default before the match, or guard on the shape, when a slot may be
  absent.
- Arms are tried in order and the first match wins. With no matching arm
  and no `else:`, the value is `None`.

## See also

- [`compile/match`](../reference/specials/compile-match.md): the special form
- [`layout/match`](../reference/layouts/layout-match.md): the `match:` block
- [`compile/switch`](../reference/specials/compile-switch.md): equality dispatch
- [`koskari.matcher`](../reference/api/koskari-matcher.md): the matcher these patterns compile to
- [Macros and hygiene](macros-and-hygiene.md): the same matcher matching source forms at compile time
