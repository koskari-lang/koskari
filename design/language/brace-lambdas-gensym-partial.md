# Koskari Brace Lambdas (gensym-partial)

## Overview

Brace forms `{...}` provide a concise syntax for constructing anonymous callable closures.

> `{BODY}` is syntactic sugar for `(gensym-partial BODY)`.

The `gensym-partial` form performs placeholder-driven parameter inference and lowers to a standard `lambda`.

This gives `{}` a single, unified meaning:

* Always produces a callable closure
* Supports positional placeholders
* Supports variadic argument capture
* Supports zero-arity thunks

---

## Reader Macro

The reader macro for `{...}`:

1. Reads nested forms until the matching `}`
2. Produces a single form:

```scheme
{BODY}  =>  (gensym-partial BODY)
```

Notes:

* No substitution happens at read time
* Nested `{...}` forms are preserved as nested `gensym-partial` forms

---

## gensym-partial Semantics

`gensym-partial` performs a transformation over its BODY:

### 1. Placeholder Detection

Recognized placeholders:

* `%1`, `%2`, ..., `%n` → positional arguments
* `...` → variadic (rest) argument

### 2. Parameter Inference

* The highest `%n` determines positional arity
* All lower positional indices must be present (no gaps)
* Repeated usage of `%n` refers to the same parameter
* Repeated `...` refers to the same rest parameter (one rest gensym shared by every `...` site)

### 3. Gensym Binding

* Each positional placeholder maps to a unique gensym
* `...` maps to a single gensym representing rest arguments (multiple `...` in BODY use that same gensym)

Example mapping:

```scheme
{list %2 %1}
;; => (lambda [g1 g2] (list g2 g1))
```

### 4. Substitution

Within BODY:

* `%n` is replaced with its gensym
* `...` is replaced with the rest-arg gensym
* Splice sites use `*:` syntax

Example:

```scheme
{print ... file: sys.stderr}
;; => (lambda [*: gs] (print *: gs file: sys.stderr))
```

### 5. Scope Boundaries

* `gensym-partial` does not recurse into nested `gensym-partial` forms
* Placeholders bind to the nearest enclosing brace form

---

## Zero-Arity Case

If no placeholders are present:

```scheme
{print "hi"}
;; => (lambda [] (print "hi"))
```

This allows `{}` to function as a concise thunk syntax.

---

## Valid Forms

```scheme
{+ %1 1}
;; unary lambda

{list %1 %2}
;; two-arg lambda

{print ...}
;; variadic lambda

{foo %1 ...}
;; positional + variadic

{print "hi"}
;; thunk

{foo ... ...}
;; rare but valid: both `...` are the same rest binding
```

---

## Invalid Forms

```scheme
{foo %2}
;; error: skipped positional (%1 missing)
```

---

## Design Invariants

* `{}` always produces a callable closure
* Placeholder presence determines arity
* No implicit partial application based on missing arguments
* All semantics lower to standard `lambda`
* Nested brace forms are independent scopes

---

## Rationale

This design provides:

* A concise alternative to `lambda` for small adapters
* Explicit and predictable parameter inference
* No ambiguity around arity or argument omission
* Clean integration with existing call semantics

It avoids:

* Implicit partial application heuristics
* Arity guessing based on callee
* Overloading `{}` with unrelated meanings (e.g. `begin`)

---

## Example

```scheme
map {+ %1 1} items
filter {> %1 10} items
sort key={get-name %1}

with-lock mylock {
  print "hello"
  flush!
}
```

---

## Summary

Brace forms provide a compact, expressive, and well-defined shorthand for anonymous functions in Koskari, powered by `gensym-partial` and placeholder-driven inference.
