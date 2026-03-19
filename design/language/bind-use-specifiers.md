# Koskari Design Note: BIND and USE Specifiers

## Purpose

This document summarizes the design discussion around **BIND** and **USE**
specifiers in Koskari syntax. The goal is to clarify how bracketed specifiers
are interpreted across language constructs and to establish a stable
normalization model for the compiler.

The design resolves ambiguity in single-element bracket forms and creates a
clear foundation for declarations, parameters, and association constructs.

---

# Core Concept

Bracketed specifiers in Koskari serve two distinct semantic roles:

* **BIND** — target-focused declaration syntax
* **USE** — expression-focused association syntax

Although both use the same bracket container (`[...]`), they are parsed and
interpreted differently.

Separating these two patterns prevents ambiguity and simplifies parsing and
normalization.

---

# BIND Specifiers

## Definition

A **BIND** specifier describes a binding target and optional metadata.

Canonical structure:

```
[target ...attributes... value: VALUE]
```

Where:

* `target` is the symbolic binding target
* optional attributes may include type annotations or flags
* `value:` represents the associated expression for the binding

The meaning of `value:` depends on the enclosing construct.

Examples:

```
a                        ;; a with no value
[a]                      ;; a with no value
[a :: int]               ;; a with no value, type decl int
[a type: int]            ;; a with no value, type decl int (synonymous to above)
[a 5]                    ;; simple name and value
[5 as: a]                ;; convenience sugar
[a value: 5]             ;; fully expanded with value
[a type: int value: 5]   ;; fully expanded with type
```

---

## Bare BIND Form

A BIND specifier has a unique shorthand form.

```
a  ==  [a]
```

A bare symbol is treated as a BIND specifier containing that symbol.

This shorthand enables concise declaration syntax.

Example:

```
(define foo [a b c] ...)
```

Equivalent conceptual normalization:

```
(define [foo] [[a] [b] [c]] ...)
```

The identifiers `foo`, `a`, `b`, and `c` are all bare BIND forms.

---

## BINDINGS (Collected BIND Forms)

Many constructs operate on **multiple bindings**.

A list of BIND specifiers is referred to as **BINDINGS**.

Example:

```
(let [[a 1]
      [a 2]]
  ...)
```

Function parameters also form BINDINGS:

```
(lambda [a b [c 5]] ...)
```

Conceptually:

```
BIND
  → single binding descriptor

BINDINGS
  → collection of BIND descriptors
```

This allows constructs like `lambda`, `let`, and `define` to share a common
binding grammar.

---

# USE Specifiers

## Definition

A **USE** specifier is expression-first and optionally introduces a binding.

Canonical structure:

```
[EXPR as: target]
```

Where:

* `EXPR` is an expression that produces a value
* `target` is an optional symbolic binding


Examples:

```
[Exception]           ;; bare expression
[Exception as: e]     ;; expression bound to name
[e Exception]         ;; expression bound to name, synonymous to above
[(resource)]          ;; bare expression
[(resource) as: r]    ;; expression bound to name
[r (resource)]        ;; expression bound to name, synonymous to above
```

---

## USE Must Always Be Bracketed

Unlike BIND, USE specifiers **cannot use a bare shorthand**.

This restriction exists because USE is expression-first and expressions may
already be list forms.

For example:

```
(with EXPR ...)     ;; incorrect
(with [EXPR] ...)   ;; correct
```

Without brackets it would be impossible to distinguish between:

* a USE specifier
* a normal expression or function call

Therefore USE specifiers must always be written explicitly:

```
(with [(resource)] ...)
(with [(resource) as: r] ...)

(except [Exception] ...)
(except [Exception as: e] ...)
```

The expression inside the brackets is interpreted **exactly as written**,
whether it is:

* a symbol
* a literal
* a call form
* a complex expression

---

# Interpretation by Language Constructs

Different constructs expect different specifier types.

| Construct           | Specifier Type |
| ------------------- | -------------- |
| `lambda` parameters | BIND           |
| `let` bindings      | BIND           |
| `define` targets    | BIND           |
| `with` contexts     | USE            |
| `except` matchers   | USE            |

Each construct consumes the normalized specifier and interprets fields
according to its own semantics.

BIND and USE intentionally allow very similar conventions. Their difference
comes entirely in what aspect is optional -- for a BIND the name is mandatory,
but the value is not. For a USE the expression is mandatory, but the name is
not.

---

# Compiler Normalization

To simplify implementation, the compiler should normalize specifiers before
evaluation.

Two helper parsers are expected:

```
parse_bind_spec(...)
parse_use_spec(...)
```

These functions convert surface syntax into canonical internal
representations.

Example structures:

```
BindSpec
    target
    type?
    attributes?
    value?
```

```
UseSpec
    expr
    target?
```

Constructs then consume these normalized structures.

---

# Design Rationale

Separating BIND and USE provides several advantages:

* resolves ambiguity in single-element bracket forms
* enables rich declaration syntax for bindings
* keeps association syntax simple and predictable
* simplifies parser implementation
* improves compiler diagnostics

Most importantly, it establishes **BIND as the declaration grammar of the
language**, while **USE remains a lightweight association grammar**.

---

# Summary

Koskari bracket specifiers now follow two distinct patterns:

## BIND

Target-focused declaration form.

```
a == [a]
[a value: X]
[a :: T]
```

Supports shorthand and forms the basis of declaration syntax.

## USE

Expression-focused association form.

```
[EXPR]
[EXPR as: TARGET]
```

Must always be bracketed.

## BINDINGS

Collections of BIND specifiers used by constructs such as `lambda`, `let`, and
`define`.

This model provides a consistent and extensible framework for Koskari's binding
and association syntax.
