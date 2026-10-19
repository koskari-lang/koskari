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

An arm that does not match passes the subject to the next arm.

An arm's pattern matches the subject as a single element. Every pattern
kind may be an arm's root, provided it can match one element; a root
that cannot, such as the zero-width `NOT` head or a `MATCH` of two
elements, raises `SpecialError` at compile time.

A clause may carry a **guard** between the delimiter and the body:
`[[PATTERN] when: COND BODY...]` runs the body only when `COND` is truthy
after the pattern binds; `unless:` inverts the sense. A failed guard
advances to the next arm with the subject intact, and the bound captures
persist (Python-consistent).

Name resolution, binding, sparse captures, and how sequence patterns
test their elements are documented in
[Match patterns](../../guide/match-patterns.md).

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
