# Match patterns

`match` destructures a value against a series of case patterns: the first
arm whose pattern matches binds its captured names and runs its body. It
is the value-matching sibling of [`switch`](../specials/compile-switch.md)
(equality dispatch) — reach for `match` when you care about the *shape* of
a value, and `switch` when you only compare it against candidates.

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

This page is the pattern-language reference. The `match` form itself is
documented at [`compile/match`](../specials/compile-match.md) and
[`layout/match`](../layouts/layout-match.md).

## Patterns are classified by head, never by bracket

`()` and `[]` are value-equal in koskari, so a pattern's *kind* is never
decided by which bracket surrounds it. Instead, each kind announces itself
with a **head symbol or marker** — and the reader sugars (`#[…]`, `#{…}`,
`Name[…]`) produce exactly those heads. A bare symbol is always a capture;
a bare atom is always a literal; everything structured says what it is.

## Element vocabulary

| Pattern | Written | Meaning |
|---------|---------|---------|
| capture | `x` | bind this position (or the whole subject) to `x` |
| literal | `5`, `"s"`, `foo:`, `'sym` | position must equal the value |
| typed capture | `x :: T` | bind `x`; value must satisfy type/predicate `T` |
| type check | `(:: T)` | check only, no binding |
| sequence | `#[a b c]` or `#(a b c)` | destructure a sequence positionally |
| rest | `#[a *: rest]` | bind the remaining elements to `rest` |
| mapping | `#{KEY: PAT ...}` | value must be a mapping; see below |
| class | `Point[x y]` | isinstance check plus field extraction |
| class, keyword fields | `Point[x: 0 y]` | attribute `x` must equal 0; bind attribute `y` |
| extraction | `(app f PAT)` | call `f` on the value, match `PAT` against the result |
| predicate | `(? pred)` | `pred` called on the value must be truthy |
| alternation | `(or PAT PAT ...)` | first alternative that matches |
| negation | `(not PAT)` | value must not match `PAT` |

Patterns nest freely: a sequence element may be a class pattern, a mapping
value may be a sequence, an extraction result may be destructured —
`(app splitext #[base ".ki"])`.

The canonical matcher heads (`FORM`, `ONEOF`, `REST`, `CLASS`, …) from
[`koskari.matcher`](../../api/koskari-matcher.md) are also accepted in
pattern position for advanced use; the sugared forms above lower to them.

### Sequences

A sequence pattern is always the explicit `#[…]` / `#(…)` form — a bare
list is never silently a sequence. Length must agree unless a `*: rest`
marker captures the remaining elements. Inside a sequence, a typed element
must be parenthesized: `#[(x :: int) y]`.

**Only real sequences destructure.** A sequence pattern matches lists,
tuples, and other `collections.abc.Sequence` instances (`range`, for
example) — the same rule as Python's PEP 634. `str`, `bytes`, and
`bytearray` never destructure into characters; mappings never
destructure into their keys (they belong to `#{...}` arms); sets are
not sequences; and an iterator flowing past a sequence arm is never
silently consumed. All of these fall through as a soft no-match — a
string still matches as a whole value (a literal, a capture, or
`s :: str`), and a dict still matches mapping arms.

### Classes

`Point[x y]` checks `isinstance(value, Point)` and extracts fields:
positional fields resolve through the class's `__match_args__`
(dataclasses and named tuples provide it automatically on Python 3.10+;
on 3.9, which predates `__match_args__`, koskari synthesizes the
equivalent for dataclasses and namedtuples, and any other class needs an
explicit `__match_args__`); keyword fields
(`Point[x: 0 y]`) read attributes directly and work on any class. A bare
`Point[]` is an isinstance check alone. Field patterns nest:
`Point[(x :: int) y]`.

### Mappings

`#{KEY: PAT ...}` matches a `dict` (or any `collections.abc.Mapping`).
Every listed key must be present and its value must match the paired
pattern; **extra keys are ignored**. A literal key is written literally
(`#{"kind": "job"}`); a **symbol key is resolved from the enclosing
scope at match time**, so computed keys work:

```koskari
define field "url"

match msg:
  case #{field: u}:      ; looks up whatever `field` holds -- "url"
    (fetch u)
```

### Names resolve from the enclosing scope

Every name a pattern references — the `T` in `x :: T`, the class in
`Point[…]`, the `pred` in `(? pred)`, the `f` in `(app f …)`, a symbol
mapping key — is an ordinary binding resolved from the enclosing scope at
match time. Closure-captured and locally-bound names work exactly as they
would in the body.

### Callables run on the demand path

Predicates and extractors are invoked synchronously through the demand
mechanism, so a plain koskari `define` works directly — no wrapping:

```koskari
define splitext [name]:
    (return (name.rpartition "."))

match path:
  case (app splitext #[base "." "ki"]):
    (compile-source base)
```

A callable that genuinely suspends (a real `await`) raises
`KoskariSyncViolation` — patterns cannot pause. A raising predicate or
extractor propagates as an error; it is never treated as a non-match.

## Guards

A `when` / `unless` guard runs **after** the pattern matches and binds,
with the captured names in scope; a failed guard advances to the next arm:

```koskari
match v:
  case #[a b] when (> a b):
    "descending"
  case #[a b]:
    "ascending-or-equal"
```

Captured names remain bound even when the guard fails — the same behaviour
as Python's `case ... if ...`.

## Binding behaviour

- Captured names are bound in the **enclosing scope** and remain visible
  after the `match` form, like every other koskari binding (and like
  Python's `match`). Arms are not isolated scopes.
- Captures are **sparse**: a slot that did not bind (an `OPTIONAL` arm
  that did not fire) is left unbound, and reading it raises `NameError`.
  Bind defaults before the match, or guard on the shape, when a slot may
  be absent.
- Arms are tried in order; the first match wins. With no matching arm and
  no `else:`, the form yields `None`.

## See also

- [`compile/match`](../specials/compile-match.md) — the match special form
- [`layout/match`](../layouts/layout-match.md) — the `match:` block layout
- [`compile/switch`](../specials/compile-switch.md) — equality dispatch
- [`koskari.matcher`](../../api/koskari-matcher.md) — the matcher API these
  patterns compile to
- [Macro matching and hygiene](macro-matching-and-hygiene.md) — the same
  engine matching source forms at compile time
