# Macros or syntax extension

Macros let Koskari code create Koskari code before ordinary evaluation. They are
powerful, so learn the everyday language first; use macros when a new repeated
shape is clearer than another helper function.

---

## Functions vs macros

A function receives evaluated values:

```koskari
(define add-one [x]
  (+ x 1))
```

A macro receives source forms and expands to another form. That means a macro
can decide which pieces will evaluate, introduce bindings, or build a new
control-flow shape.

## `syntax-rules`

Simple macros are often written with `syntax-rules`:

```koskari
(define-syntax compile/say-twice
  (syntax-rules []
    [(say-twice message)
     (begin
       (print message)
       (print message))]))
```

After the macro is defined, this:

```koskari
(say-twice "hello")
```

expands to a `begin` that prints the message twice.

## Why the `compile/` name?

User-facing macros are compiled forms. The implementation convention is that
the transformer for `(say-twice ...)` is registered under `compile/say-twice`.
You normally call the friendly surface name; the compiler finds the transformer.

## Inspecting expansion

Use `macroexpandq-1` for one expansion step and `macroexpandq` to repeat until
the form is no longer a macro:

```koskari
(macroexpandq-1 (say-twice "hello"))
```

Expansion tools are the best way to debug a macro. If the expanded form is easy
to read, the macro is usually easier to maintain.

## Hygiene in plain language

Macro systems need rules for introduced names so that a macro does not
accidentally capture a user’s local variable, or vice versa. Koskari’s macro
reference covers the exact matching and hygiene behavior. As a beginner rule:
keep macros small, inspect expansions, and prefer unique helper names only when
you intentionally introduce bindings.

## What you should remember

- Functions compute with values; macros rewrite forms.
- `syntax-rules` is the friendly starting point for pattern-based macros.
- Expansion should produce ordinary Koskari code you could have written by hand.
- Reach for a function first; use a macro when you need a new syntactic shape.

Next: [Types or contracts](08-types-or-contracts.md).
