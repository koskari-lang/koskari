# Macros and hygiene

A macro is a function from forms to forms that runs at compile time.
The compiler hands it a use of its name, it returns the code that use
stands for, and the compiler continues on the result. That is how
`if`, `when`, `set!`, `define-class`, and the `-each` family exist: each
lowers to a smaller core form.

## What a macro is for

A function receives values after its arguments are evaluated. A macro
receives the argument forms unevaluated, so it can decide whether, when,
and how many times they run, and it can introduce bindings around them.
Reach for one when you need a new binding shape, control over
evaluation, or boilerplate that must wrap a body you wrote. When the
inputs are values, write a function; it is easier to test, pass around,
and explain.

## Defining one

`define-syntax` binds a name to a transformer. `syntax-rules` matches the
use against patterns and fills a template:

```koskari
define-syntax unless:
  syntax-rules []:
    unless test body ...:
      when (not test):
        (begin body ...)
```

An ellipsis in a pattern matches zero or more forms and the same
ellipsis in the template splices them back. A template is read by the
layout reader before the ellipsis is expanded, so a line holding only
`body ...` would read as the call `(body ...)`; keep `body ...` on the
line of the form it belongs to, or spell it `(begin body ...)`.

The macro and its first use must be in separate compilation units: an
earlier top-level form in the same file, or an imported module. A
`begin` that defines a macro and uses it in the same block fails,
because the macro is registered when its defining form finishes
compiling.

## Templates and procedures

`syntax-rules` and `syntax-match` bodies are templates: a shape with
holes that the matched pieces fill. `macro-rules` and `macro-match`
bodies are procedures: ordinary Koskari that runs at expand time and
returns a form. Use the procedural kind when the output depends on
inspecting the input: reordering entries, computing a name, classifying
arguments.

Inside a procedural body you work with the matched forms directly:

- `form.datum` unwraps a matched form to its pair; the elements inside
  are raw symbols, keywords, and values.
- `define-values [head [tp rest]] form.datum` destructures it; `(car rest)`
  takes the next element.
- `(keyword? x)` and `(symbol? x)` classify a token.
- Build the output with quasiquote, or with variadic `cons`, whose last
  argument is the tail.
- To emit an identifier you compute, wrap it in `datum`:
  `(datum (symbol f"job/{tp.datum}"))`. That is how a dispatcher rewrites
  `(job kafka ...)` into `(job/kafka ...)` and lets the compiler re-expand it.

## Seeing an expansion

`macroexpand` expands a form fully and `macroexpand-1` takes one step:

```koskari
koskari> (macroexpand '(unless ready (print "go")))
(cond [ready None] [else (print "go")])
```

Keep a parenthesized use beside a layout use while writing a macro; the
s-expression is the tree the macro sees.

## Two senses of match, two senses of datum

Two ideas share the word **match**: structural matching of input forms,
which this page is about, and the runtime `match` form over values,
which is [Match patterns](match-patterns.md). **Datum** also has two
senses: a macro helper form and a hygiene wrapper around captured
syntax.

## Match (macro and special transformers)

In reference docs, **match** usually means: given a source form, test it against
a **pattern** and run the matching arm's template.

Mechanisms include:

- **`syntax-rules`** / **`macro-rules`** / **`special-rules`** -- clause lists
- **`syntax-match`** / **`macro-match`** / **`special-match`** -- named transformer
  specs (see [Macros index](../reference/macros/index.md) and [Layouts index](../reference/layouts/index.md))
- Compiler **`@special(..., patterns=...)`** arms -- same matcher engine over
  spine subjects

The matcher is implemented in native code with patterns built in Python; API
detail lives in [`koskari.matcher`](../reference/api/koskari-matcher.md) and
[`koskari.hygiene`](../reference/api/koskari-hygiene.md).

This is **not** the same as:

- **`compile/switch`** -- value dispatch on runtime data
- The runtime **`match`** form over values, in [Match patterns](match-patterns.md)
- Exception matching at the VM layer

When the [Glossary](../reference/glossary.md) says **match**, it means macro/special
pattern matching unless context says otherwise.

### Brackets and parens match the same shape

A `[…]` and a `(…)` read to the same pair structure (see [Brackets and the call
convention](brackets-and-call-convention.md)), so a pattern cannot tell one from
the other. `[formals]` matches any one-element form, including a plain `(f x)`
call -- so you cannot write two arms that branch on "is this a bracket or a
call." A form that must carry a literal bracket, such as lambda formals, has to
require it in every arm (spell out an explicit `[]` for the empty case) rather
than treat it as optional.

## Resolution order: macros expand before names resolve

Macros and special forms are resolved at **compile time**, before ordinary name
lookup happens. When the compiler sees a form in call position, it first checks
whether the head is a macro or special; if so it expands, and only the expanded
result is subject to name resolution. **A macro always wins over a same-named
value binding in call position** -- a local `let` / `define` / `labels` / `flet`
binding, a module binding, or an import cannot displace it.

```koskari
(let []
  (define caddr (lambda [n] 999))
  (caddr (cons 1 2 3 4 nil)))
;; ⇒ 3, not 999
```

`caddr` is a macro (it expands to nested `car` / `cdr`), so `(caddr …)` expands
at compile time and the local `define` is simply never consulted in that call
position. Names bound to plain **functions** have no such precedence and shadow
normally:

```koskari
(let []
  (define first (lambda [n] (* n 100)))
  (first 3))
;; ⇒ 300 -- first is a runtime function, so the local binding wins
```

Practical consequences:

- Choosing a helper name (in `labels`, `flet`, `let`, or a parameter) that
  collides with a macro or special form (`if`, `when`, a `cad*r` accessor, …)
  will not rebind it in call position -- the macro still expands. Rename the
  helper if you need it callable.
- The colliding name is still usable as a **value** (passed as an argument,
  stored in a variable); only the call-position head goes through macro
  expansion.

## Datum (macro form)

Inside macro bodies, **`(datum expr)`** or **`(datum expr pos)`** captures an
expression together with **source position** for hygiene. It expands to a call
that records where the fragment came from.

- Macro: [`compile/datum`](../reference/macros/compile-datum.md)
- Related: **`datumq`**, **`parse-match-patternq`**, **`parse-rule-patternq`**
  in bootstrap syntax helpers

Authors use **`datum`** so renamers and binder collection do not treat
substituted user fragments as freshly introduced identifiers.

## Datum (hygiene wrapper)

At expand time, captured pattern-variable values are often wrapped in a
**Datum** object (syntax box): a value plus optional position and flags. Tools
that walk expanded code treat those boxes as opaque for renaming when the capture
came from the input form, not from the template.

Macro expansion **finalize** steps peel `Datum` wrappers before compile. The
Python/C types are documented under
[`koskari.hygiene`](../reference/api/koskari-hygiene.md) and
[`koskari.lib.Datum`](../reference/api/koskari-lib.md).

| Sense | Role |
|-------|------|
| **`(datum …)` form** | Macro author syntax to attach position |
| **Datum wrapper** | Runtime/expansion representation of a captured fragment |

## Practical flow

```text
Source form
    → matcher selects arm (match)
    → template may use (datum …) for hygiene
    → expanded ordinary forms
    → compile as usual
```

## See also

- [Macros](../reference/macros/index.md): `define-syntax`,
  `extend-syntax`, and the rule forms
- [compile/let-syntax](../reference/macros/compile-let-syntax.md):
  a local compile-time binding
- [compile/datum](../reference/macros/compile-datum.md)
