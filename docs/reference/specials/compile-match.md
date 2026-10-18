# compile/match
**Symbol:** `compile/match`

**Syntax:** `(match SUBJECT [[PATTERN] BODY...] ... [else: BODY...])`

**Use-case:** Structural pattern matching against one subject value.
`SUBJECT` is evaluated once, then each arm's pattern is tried in order. The
first matching arm binds its captured names and runs its body, whose value
is the result of the form. `else:` always matches; with no matching arm and
no `else:` the form yields `None`. Pattern syntax is documented in
[Match patterns](../../guide/match-patterns.md).

**Behaviour:** Requires at least one argument (`SUBJECT`). Each non-else
clause is a list whose **first element is the pattern delimiter** -- a list
holding the pattern -- and whose remaining elements are the body. The
delimiter holds either a single pattern form (`[[x]]`, `[[5]]`,
`[[#[a b]]]`, `[[Point[x y]]]`) or the multi-token spelling of a typed
pattern (`[[x :: int]]`); any other bare multi-token content raises
`SpecialError` -- a sequence is always the explicit `#[...]` / `#(...)`
form, never bare juxtaposition.

Each arm's pattern is compiled into the enclosing function's own code.
Every name the pattern references (types, classes, predicates,
extractors, symbol mapping keys) is read from the enclosing scope at
match time, so closure-captured names work, except the seven core type
names `int`, `str`, `list`, `tuple`, `pair`, `symbol` and `keyword`,
which the pattern tests directly and never reads from the scope. An arm
that does not match passes the subject to the next arm.

An arm's pattern matches the subject as a single element. Every pattern
kind may be an arm's root, provided it can match one element; a root
that cannot, such as the zero-width `NOT` head or a `MATCH` of two
elements, raises `SpecialError` at compile time.

Each name an arm's pattern captures is stored into the variable of that
name in the enclosing scope as the pattern walks the value, and on the
first match the body runs. A name whose capture did not bind, under an
`OPTIONAL` that did not fire, leaves its variable as it was. An arm that
misses after binding some captures leaves them bound, as does an
`(or ...)` alternative that misses after binding.

A clause may carry a **guard** between the delimiter and the body:
`[[PATTERN] when: COND BODY...]` runs the body only when `COND` is truthy
after the pattern binds; `unless:` inverts the sense. A failed guard
advances to the next arm with the subject intact, and the bound captures
persist (Python-consistent).

Contracts (details in
[Match patterns](../../guide/match-patterns.md)): captures are sparse -- a
capture that did not bind leaves its variable as it was, and a variable
never bound raises `NameError` when read; bindings persist after the
form; `str` / `bytes` never destructure under sequence patterns; a
sequence pattern's length is checked before its elements are tested,
so no predicate or extractor in it is called when the subject's length
rules it out; a predicate or extractor is called from the enclosing
function, so one that awaits does so in place.

The head may be written as `match` or `compile/match`; compiler lookup
resolves `match` to `compile/match`.

**Expected lowered shape from layout/match:**

```koskari
(match SUBJECT
  [[PATTERN...] BODY...]
  [[PATTERN...] when: COND BODY...]
  [else: BODY...])
```

The layout wraps each `case` header into the clause delimiter, so the
layout and s-expression forms produce identical clauses.

**Examples:**

```koskari
;; sequence destructure with rest
(match data
  [[#[a b *: rest]] rest]
  [else: 0])
;; data = #(1 2 3)  ->  (3,)

;; whole-value typed capture, multi-token spelling
(match data
  [[x :: int] x]
  [else: "not an int"])
;; data = 7  ->  7

;; class pattern (subscript sugar) and guard
(match p
  [[Point[x y]] when: (> x y) "descending"]
  [[Point[x y]] "ascending-or-equal"]
  [else: "not a point"])

;; mapping with a literal and a bound key
(match msg
  [[#{"kind": "job" "url": u}] u]
  [[#{"kind": k}] k]
  [else: "other"])

;; extraction: call a function, destructure its result
(match path
  [[(app splitext #[base "." "ki"])] base]
  [else: path])
```
