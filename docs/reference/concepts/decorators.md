# Decorators

The `@` decorator surface from end to end: the pragma that enables it, how
layout lines lower, how the s-expression forms expand, and how to add
decorator syntax of your own.

---

## Introduction

A decorator is a leading `@` line over a form:

```koskari
@demand
define greet [name]:
    f"hi {name}"
```

Python readers will recognize the shape, and the default behavior matches
Python exactly: each decorator expression is evaluated and applied to the
definition, outermost first — `@d1` `@d2` over `foo` yields `(d1 (d2 foo))`.

Koskari's decorators go further in one deliberate way: certain *binding
forms*, written body-less in decorator position, wrap the definition
**syntactically** instead of being called at runtime. That turns the
decorator prefix into a composing block:

```koskari
@let [[count 0]]
define tick []:
    incr! count
    count
```

`tick` is a module-level function closed over a private `count` — the same
program as a `let` block containing the definition, without the indentation
and without `define-global` to push the name back out.

The rest of this page follows one decorated definition through the pipeline:
pragma, reader, expansion, extension.

---

## Stage 0: the pragma

The `@` surface is gated by the `reader/decorators` pragma (baseline **on**).
A module can declare it off:

```koskari
(pragma [reader/decorators False])
```

after which a leading `@` has no layout meaning and reads through the
ordinary atom rules. A **directly constructed** reader — one built by
tooling rather than through a module pragma — starts with the surface off,
like the other pragma-gated reader features; readers built via
`LayoutReader.from_pragma` (the import path, the REPL, the lexer) get the
baseline.

The pragma gates only the reader surface. The s-expression spelling
`(compile/decorate ...)` is a macro and remains available regardless of the
setting. See [Pragmas and feature settings](pragma.md).

---

## Stage 1: reading

In layout mode, the reader collects consecutive `@` lines at one column,
then reads the single layout form beneath them, and wraps the lot:

```koskari
@(let [[c 0]])
@demand
define tick []: ...

;; reads as
(compile/decorate ((let [[c 0]]) demand) (define tick [] ...))
```

Two reader conveniences fall out of existing layout rules:

- **Parens are optional on the decorator line.** Multiple forms on a layout
  line combine into one call form, so `@let [[c 0]]` reads identically to
  `@(let [[c 0]])`.
- **The subject is any layout form** — a `define`, a `define-class`, a
  `begin:` block, an `if`, a bare s-expression. Decorators are not
  restricted to definitions.

Nothing about decorator *meaning* lives in the reader; it only builds the
`compile/decorate` form. Everything after this point is ordinary macro
expansion, which is why the s-expr spelling works with the pragma off.

---

## Stage 2: the decorate fold

[`compile/decorate`](../macros/compile-decorate.md) does two jobs.

**Name hoisting.** When the subject is a `define` / `define-class`, the name
moves outside the decoration so the decorated *value* is what gets bound:

```koskari
(compile/decorate (d1) (define foo [x] x))
;; expands to
(define foo (compile/decorate (d1) (function foo [x] x)))
```

**The fold.** With a value-form subject, decorate peels one decorator per
expansion step into
[`compile/decorate-subject`](../macros/compile-decorate-subject.md),
outermost decorator first:

```koskari
(decorate [d1 d2] fn)
;; expands to
(decorate-subject d1 (decorate [d2] fn))
```

`decorate-subject` is where a single decorator meets its subject, and it is
the extension point.

---

## Stage 3: applying one decorator

`decorate-subject` dispatches on the **shape of the decorator expression**.
The rule of thumb: *the decorator's head tells you when its effect happens.*

| Decorator | When the effect happens |
|---|---|
| `@some-callable` | at decoration — evaluated and invoked on the subject |
| `@let [[c 0]]` | at definition — lexical capture around the subject |
| `@with [(cm)]` | now — extent spans the subject's evaluation |
| `@parameterize [[p v]]` | now — extent spans the subject's evaluation |
| `@call-with [(cm)]` | per invocation |
| `@call-parameterize [[p v]]` | per invocation |

**The default arm** covers every ordinary runtime decorator (`@demand`,
`@(memoize maxsize: None)`, `@abstractmethod`): the expression evaluates,
and its value is called with the subject's value.

**The graft arms** match a body-less `let` / `with` / `parameterize` and
complete it by splicing the subject in as the form's body:

```koskari
(decorate-subject (let [[c 0]]) subject)
;; expands to
(let [[c 0]] subject)
```

Only the body-less spelling is special. A complete form with a body misses
the arm and falls to the default, where it evaluates as an ordinary
expression — so every decorator expression that already worked keeps its
meaning, and the graft arms claim only spellings that are meaningless as
standalone expressions.

Grafting is exactly right for `let` over a definition (a closure is
definition-time capture) and for any extent form over a **plain form** that
runs once, now:

```koskari
@parameterize [[verbose True]]
begin:
    ...
```

It is almost never right for `with` / `parameterize` directly over a
`define`: the extent would span function *creation*, which no call
observes. For a per-call extent, use the `call-` spelling.

**The call arms** wrap the subject in a forwarding function: enter the
extent, call the original with the passed arguments, close the extent,
deliver the value. The binding group sits inside the wrapper, so the
context-manager expression or parameter values re-evaluate on every call —
a fresh `(cm)` per invocation, exactly as if the `with` were written in the
body. The wrapper answers for the function it wraps: `decorate-wraps`
(a `functools.update_wrapper`) carries over `__name__`, `__doc__`, the
annotations, `__dict__` contents, and the `__wrapped__` chain.

```koskari
@let [[db (connect)]]
@call-with [lock]
define handle [msg]:
    ...
```

One connection captured lexically at definition; the lock taken and
released per call. Stacked decorator lines read top-to-bottom exactly as
the equivalent nested blocks would.

---

## Extending: your own decorator syntax

Shape arms are ordinary macro arms on `compile/decorate-subject`, added with
[`extend-syntax`](../macros/compile-extend-syntax.md). Registered arms are
tried before the built-in arms, so an extension can claim its shape ahead of
the invocation default:

```koskari
extend-syntax compile/decorate-subject:
  syntax-rules [my-extent]:
    case (decorate-subject (my-extent group) subject):
      (my-extent group subject)
```

That is the entire registration for a graft-style decorator: `@my-extent
[...]` over any form now completes `my-extent` with the subject as its body.
A call-time variant follows the `call-` pattern — wrap the subject in a
`lambda` that establishes the extent and forwards `*: args **: kwds`.

Guidelines that keep the decorator position readable:

- **Claim only body-less shapes.** Match `(my-extent group)` — exactly one
  group, no body — so complete expressions keep their invocation meaning.
- **Follow the two families.** A lexical or run-now form grafts; a per-call
  form gets a `call-` name. Resist inventing a third timing.
- **Match on your form's own head.** The arm literals match symbolically,
  like every `syntax-rules` literal; an aliased or module-qualified spelling
  of the head falls through to the default arm.

---

## Debugging the expansion

`macroexpand-1` shows each stage of the pipeline one step at a time — the
hoist, the peel, and the arm — which is usually the fastest way to see why
a decorator did or didn't fire:

```koskari
(macroexpand-1 '(compile/decorate ((let [[c 0]])) (function f [] c)) ns)
;; -> (decorate-subject (let [[c 0]]) (compile/decorate [] (function f [] c)))
```
