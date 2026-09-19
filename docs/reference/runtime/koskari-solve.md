# koskari.solve

**Module:** `koskari.solve`

**Imports:** `import-from koskari.solve amb fail require solve solve-first`

**Use-case:** Nondeterministic search. `amb` chooses a value from a
sequence of choices; each choice runs the rest of the search, `require`
prunes branches that cannot work, and `solve` / `solve-first` delimit
the search and collect what survives. There is no search engine behind
it: `amb` is an ordinary function over the language's delimited
continuations, so choice points compose across function calls and nest
to any depth.

## amb

**Syntax:** `(amb CHOICES)`

Nondeterministically choose a value from `CHOICES` (any iterable). The
rest of the enclosing search runs once per choice, and each choice's
solutions are collected in order. Calling `amb` with no enclosing
`solve` or `solve-first` raises `NoActiveSearch` (see the Errors section).

## require

**Syntax:** `(require TEST)`

Prune the current branch unless `TEST` is truthy. A pruned branch
contributes no solutions; the search continues with the next choice.

## fail

**Syntax:** `(fail)`

Abandon the current branch unconditionally. `(require test)` is
`(unless test (fail))`.

## solve

**Syntax:** `(solve THUNK)`

Run `THUNK` as a search and return every solution as a list, in choice
order. The thunk's return value is one solution; branches ended by
`fail` or a failed `require` contribute nothing. An exhausted search
returns `[]`.

## solve-first

**Syntax:** `(solve-first THUNK)`

Run `THUNK` as a search and return its first solution, or `None` when
every branch fails. Once a solution is accepted, the remaining choices
at every level are skipped without running.

Enumeration is eager within a search: `solve` explores everything
before returning, and `solve-first` stops early by skipping rather than
by suspending. A lazy, one-solution-at-a-time enumerator is a
non-goal for this module: it would require yielding across choice
points, and a `shift` cannot cross a generator boundary (see the Errors section).

## Errors

The module exports its own error vocabulary, rooted under the language's
[`KoskariContinuationError`](../error-codes.md) category:

- `SearchError` -- base class for search errors raised by this module.
- `NoActiveSearch` -- `amb`, `fail`, or `require` was called with no
  enclosing `solve` or `solve-first` to delimit the search. Wrap the
  whole computation in `(solve (lambda [] ...))` or
  `(solve-first (lambda [] ...))`.

A choice point inside a generator raises `KoskariPromptUnreachable`
untranslated: the delimiter exists but the capture cannot cross the
generator boundary, and no added delimiter will help -- the generator
must be restructured out of the path between the choice point and the
search. A capture failure against a foreign (non-search) delimiter also
propagates untouched.

**Examples:**

```koskari
import-from koskari.solve amb require solve solve-first

;; every pythagorean triple with sides under 15
(solve
  (lambda []
    (define a (amb (range 1 15)))
    (define b (amb (range a 15)))
    (define c (amb (range b 15)))
    (require (== (+ (* a a) (* b b)) (* c c)))
    (#tuple a b c)))
;; => #[#(3 4 5) #(5 12 13) #(6 8 10)]

;; just the first
(solve-first
  (lambda []
    (define x (amb (#list 1 2 3)))
    (require (> x 1))
    x))
;; => 2
```

See `examples/solve.ki` for a complete program, including Dinesman's
dwelling puzzle.
