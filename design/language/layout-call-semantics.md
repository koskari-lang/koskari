# Koskari layout call semantics (single-symbol values)

## Overview

Layout bodies support a call-oriented surface syntax where multi-form
symbol-headed lines implicitly form calls.

A layout line is only eligible for implicit call lowering if it begins with a
bare symbol. Otherwise it is treated as a normal expression line.

---

## Line classification

For a non-block layout line (that is, no trailing `:` at depth 0):

### 1. Symbol-headed

If the first top-level form is a bare symbol:

```scheme
foo bar baz   -> (foo bar baz)
foo           -> foo
```

- If the line has more than one top-level form, it lowers to a call
- A single symbol remains a value expression
### 2. Non-symbol-headed: ordinary expression

If the line does not start with a symbol:

```scheme
(foo bar)   -> (foo bar)
[a b c]     -> [a b c]
#{a b}      -> #{a b}
123         -> 123
"abc"       -> "abc"
```

Rules:

- The line is treated as ordinary expression syntax
- No implicit call is performed
- Multiple expressions on a single line are allowed and behave exactly as they
  would outside of layout mode (no special grouping or transformation is
  applied)

---

## Block lines

A layout block line is:

```scheme
symbol ... :
```

- Must begin with a bare symbol
- Terminates at first depth-0 `:`
- Body is collected by indentation

---

## Summary rule

A layout line must be one of:

1. Block line: `symbol ... :`
2. Implicit call line: `symbol ...`
3. Single expression line (non-symbol-headed): `expr`

---

## Design invariant

Only multi-form symbol-headed lines participate in implicit call formation.
All other forms remain structurally unchanged.

This keeps layout semantics:

- Predictable (shape-based)
- Local (no deep reinterpretation)
- Compatible with existing s-expression rules

---

## Example

```scheme
define foo [x]:
  print x
  + x 1
  x
  (bar x)
```

Lowers to:

```scheme
(define foo [x]
  (compile/begin
    (print x)
    (+ x 1)
    x
    (bar x)))
```

---

## Notes

- Bare symbol lines are treated as value expressions by default
- This model keeps layout as a reader-level transformation while preserving
  core s-expression semantics

