# Macros and syntax extension

Macros let Koskari extend the language from inside the language. You do not need
to write macros to be productive, but you will benefit from recognizing them.

---

## What a macro does

A normal function receives values after its inputs are evaluated. A macro
receives code-shaped input earlier and returns new code for Koskari to compile.
That is how friendly surface forms can lower to smaller core forms.

For example, layout `if` reads as a block for humans and lowers to a conditional
form for the compiler.

---

## When to avoid a macro

Prefer a function when you can. Functions are easier to test, pass around, and
explain:

```koskari
define square [x]:
  (* x x)
```

Reach for a macro only when you need to control evaluation, introduce binding
syntax, or create a new code shape.

---

## Looking at expansions

Use macro expansion tools when you want to see what a form becomes before it is
compiled:

```koskari
(macroexpand '(when ready (print "go")))
```

Expansion is a learning tool as well as a debugging tool: it shows the boundary
between friendly syntax and the core forms underneath.

---

## Hygiene in one sentence

Koskari's macro system is designed to help generated names avoid accidental
capture. Still, write macros with small examples and check expansions while you
learn.

---

## What you should remember

Macros transform code before ordinary evaluation. Use functions first; use macros
when the shape of the code itself is the abstraction.

---

**Go deeper:** [Macro matching and hygiene](../reference/concepts/macro-matching-and-hygiene.md)
— pattern matching at expand time, datum forms and wrappers, and how hygiene
actually prevents capture.
