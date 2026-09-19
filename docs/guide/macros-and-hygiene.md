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
      if test:
        None
      else:
        (begin body ...)
```

Two rules govern where the pieces may sit. An ellipsis in a pattern
matches zero or more forms and the same ellipsis in the template splices
them back, but the template is read by the layout reader first, so a line
holding only `body ...` reads as the call `(body ...)`. And a macro and
its first use must be in separate compilation units, because the macro is
registered when its defining form finishes compiling.
[Macros](../tutorials/from-python/macros-and-dsls.md) works both rules
through an example.

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

## Hygiene

A name the template introduces on its own is kept apart from every
binding at the use site. A name the caller supplies is bound as written.

Each expansion stamps the symbols its template wrote with a mark, and
the compiler binds and resolves a stamped name under its own spelling,
`tmp#1a` for the `tmp` of one expansion. A stamped name that nothing
bound falls back to the plain name, so a template's `cons` is the
caller's `cons`. A stamped name passed into another macro keeps its
stamp, so a binder written through a nested macro is still the
template's own, and a stamped name inside `quote` comes out plain.
A printed form shows stamped names as written, whether through
`print`, `str`, or `macroexpand` at the REPL; the stamp shows only in
`str` of the stamped symbol itself.

`(datum expr)` is the author's control in a procedural macro: it marks
a fragment as having come from the input form rather than from the
template, so it is never stamped. At expand time, captured
pattern-variable values are carried in `Datum` boxes, which the finalize
step peels before compilation. The Python and C types are under
[`koskari.hygiene`](../reference/api/koskari-hygiene.md) and
[`koskari.lib`](../reference/api/koskari-lib.md).

A function body a template writes sees every definition the same
expansion makes around it, in either order, so a template may define
mutually recursive functions or use `labels` and `letrec` as written
source does.

## Brackets and parens match the same shape

A `[...]` and a `(...)` read to the same pair structure (see [Brackets
and the call convention](brackets-and-call-convention.md)), so a pattern
cannot tell one from the other. `[formals]` matches any one-element form,
including a plain `(f x)` call, so no two arms can branch on whether a
group was written with brackets or parentheses. A form that must carry a
literal bracket, such as lambda formals, has to require it in every arm,
spelling out an explicit `[]` for the empty case rather than treating it
as optional.

## Macros expand before names resolve

Macros and special forms are resolved at compile time, before ordinary
name lookup. When the compiler sees a form in call position it first
checks whether the head is a macro or special; if so it expands, and only
the expanded result is subject to name resolution. A macro always wins
over a same-named value binding in call position -- a local `let`,
`define`, `labels`, or `flet` binding, a module binding, or an import
cannot displace it.

```koskari
(let []
  (define caddr (lambda [n] 999))
  (caddr (cons 1 2 3 4 nil)))
;; -> 3, not 999
```

`caddr` is a macro that expands to nested `car` and `cdr`, so the call
expands at compile time and the local `define` is never consulted in that
position. A name bound to a plain function has no such precedence and
shadows normally:

```koskari
(let []
  (define first (lambda [n] (* n 100)))
  (first 3))
;; -> 300; first is a runtime function, so the local binding wins
```

A helper name that collides with a macro or special (`if`, `when`, a
`cad*r` accessor) will not rebind it in call position. Rename the helper
if you need it callable. The colliding name is still usable as a value,
passed as an argument or stored in a variable; only the call-position
head goes through expansion.

## Seeing an expansion

`macroexpand` expands a form fully and `macroexpand-1` takes one step:

```koskari
>>> (macroexpand '(unless ready (print "go")))
(cond [ready None] [else: (print "go")])
```

## See also

- [Macros](../reference/macros/index.md): `define-syntax`,
  `extend-syntax`, and the rule forms
- [compile/let-syntax](../reference/macros/compile-let-syntax.md):
  a local compile-time binding
- [compile/datum](../reference/macros/compile-datum.md)
- [Glossary](../reference/glossary.md): what match and datum mean in
  compile-time documentation
