# Brackets and the call convention

Koskari reads `( )` and `[ ]` with the same grammar. A `[ ]` form is not a
vector, an array, or a list literal. Both read into pair chains, and by
default both compile the same way.

## Same grammar, same compilation

A non-empty list in head position compiles to a call, whichever bracket
delimited it:

```koskari
(f 1 2)   ; call f with 1 and 2
[f 1 2]   ; the same call
```

`[a 1]` is a call to `a` with argument `1`. A reader arriving from a
language where `[...]` builds a list has this one thing to unlearn.
Collection values are the `#(...)`, `#[...]`, and `#{...}` reader forms:
tuple, list, dict.

## What the brackets carry

The reader records that a form came from `[ ]` by producing a
bracket-pair, and `bracket-nil` for the empty `[]`. A bracket-pair is
value-equal to a pair, every pair operation works on it, and it
remembers its bracket origin. That memory does nothing on its own; it is
there for tooling and for the compiler's call-position check.

## The convention

By default the compiler points the convention out rather than enforcing
it, with a `SyntaxWarning` and no change to compilation:

- `( )` is a call. The first element is applied to the rest. Some forms
  evaluate every argument; others treat certain arguments as data.
- `[ ]` is a data fragment that the enclosing form consumes: a binding
  list, a `cond` clause, a pattern row.

The convention is a reading aid first. Nobody reads

```koskari
(let [[a 1]] (+ a 100))
```

as a call of `a` with `1`; the brackets say "binding" at a glance. A
binding list is consumed as data by `let` and never reaches the call
compiler, whatever the convention says.

## Catching a typo

Because brackets mark data positions, a bracketed fragment that reaches
call position is a sign of a mistake. With `ler` typed for `let`:

```koskari
(ler [[a 1]] (+ a 100))
```

`ler` is not a special form, so the form compiles as a call of the value
`ler` whose first argument is `[[a 1]]`, that is, `(call (call a 1))`.
The bracket marker lets the compiler see a `[ ]` fragment in call
position and say so.

## Changing the behaviour

Two settings, changed only by a [`pragma`](pragma.md) near the top of a
module; there is no global switch and no per-form syntax:

| Setting | Affects | Values | Default | Meaning |
|---|---|---|---|---|
| `reader/brackets` | reader | on, off | on | whether `[ ]` reads into a bracket-pair or a plain pair |
| `compiler/call-brackets` | compiler | `ignore`, `warn`, `error` | `warn` | what happens when a bracket-pair reaches call position |

```koskari
;; a [ ] fragment in call position is a compile error
(pragma [compiler/call-brackets 'error])

;; the check is off; brackets are documentation
(pragma [compiler/call-brackets 'ignore])

;; no marker is produced, so no check can fire
(pragma [reader/brackets False])
```

With `error`, the `ler` typo above is caught at compile time. A pragma
is forward-only and scoped to its module: it affects the code after it
and never the modules it imports.

## See also

- [Koskari and s-expressions](layout-and-s-expressions.md): layout beside
  the s-expressions the brackets appear in.
- [Grammar](../reference/grammar.md): the reader grammar for lists and
  brackets.
