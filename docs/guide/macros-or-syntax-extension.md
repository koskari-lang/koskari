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

## Two kinds of rule body: templates and procedures

`syntax-rules` and `syntax-match` bodies are **templates** — a shape with holes
that the matched pieces fill. `macro-rules` and `macro-match` bodies are
**procedures** — ordinary Koskari that runs at expand time and returns a form.
Reach for the procedural kind when the output depends on inspecting the input:
reordering entries, computing a name, or classifying arguments.

Inside a procedural body you work with the matched forms directly:

- `form.datum` unwraps a matched form to its underlying pair; the elements
  inside are raw symbols, keywords, and values.
- Destructure it with `define-values`: `define-values [head [tp rest]]
  form.datum` binds the head, the second element, and the tail at once; `(car
  rest)` takes the next element.
- `(keyword? x)` and `(symbol? x)` classify a token — for example telling a
  named entry from one whose body starts with a keyword.
- Build the output with quasiquote/unquote, or procedurally with variadic
  `cons` (the last argument is the tail): `(cons new-head rest)` prepends a head
  onto an existing tail.
- To emit an identifier you compute, wrap it in `datum`:
  `(datum (symbol f"job/{tp.datum}"))`. That is how a dispatcher rewrites
  `(job kafka …)` into `(job/kafka …)` and lets the compiler re-expand it.

A macro and a call to it must live in **separate compilation units** — the
macro is not registered within the same `begin` that defines it. Define macros
in a module (or an earlier top-level form) and use them from another.

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

**Go deeper:** [Macro matching and hygiene](macro-matching-and-hygiene.md)
— pattern matching at expand time, datum forms and wrappers, and how hygiene
actually prevents capture.
