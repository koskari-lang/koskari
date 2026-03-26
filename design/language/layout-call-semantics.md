# Koskari layout call semantics (bare calls + dot escape)

## Overview

Layout bodies support a call-oriented surface syntax where symbol-headed lines
implicitly form calls, with an explicit escape for raw expressions.

A layout line is only eligible for implicit call lowering if it begins with a
bare symbol. Otherwise it is treated as a normal expression line.

---

## Line classification

For a non-block layout line (that is, no trailing `:` at depth 0):

### 1. Symbol-headed: implicit call

If the first top-level form is a bare symbol:

```scheme
foo bar baz   -> (foo bar baz)
foo           -> (foo)
```

- All top-level forms on the line are collected into a list
- Zero-arg calls are allowed (`foo` lowers to `(foo)`)

### 2. Dot-prefixed: raw expression escape

If the line begins with `.`:

```scheme
. x           -> x
. (foo bar)   -> (foo bar)
. [a b c]     -> [a b c]
```

Rules:

- `.` suppresses implicit call formation
- Exactly one expression must follow
- This is the only way to express a bare symbol value in layout mode

```scheme
x     -> (x)
. x   -> x
```

Invalid:

```scheme
. foo bar   ; error (more than one expression)
```

### 3. Non-symbol-headed: ordinary expression

If the line does not start with a symbol or `.`:

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
3. Raw expression escape: `. expr`
4. Single expression line (non-symbol-headed): `expr`

---

## Design invariant

Only symbol-headed lines participate in implicit call formation. All other
forms remain structurally unchanged unless explicitly escaped with `.`.

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
  . x
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

- Bare symbol lines are treated as zero-argument calls by default
- The dot escape is intentionally minimal and strict to avoid introducing a
  secondary grammar
- This model keeps layout as a reader-level transformation while preserving
  core s-expression semantics

