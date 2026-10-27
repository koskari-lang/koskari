# Decorators

The `@` surface from end to end: the pragma that enables it, how the
reader lowers `@` lines, how the forms expand, and how to add decorator
shapes of your own.

A decorator is a leading `@` line over a form:

```koskari
@demand
def greet [name]:
    f"hi {name}"
```

The default behaviour is Python's: each decorator expression is
evaluated and applied to the definition, outermost first, so `@d1` over
`@d2` over `foo` yields `(d1 (d2 foo))`.

Koskari adds one thing. Certain binding forms, written body-less in
decorator position, wrap the definition syntactically instead of being
called at runtime, which turns the decorator prefix into a composing
block:

```koskari
@let [[count 0]]
def tick []:
    incr! count
    count
```

`tick` is a module-level function closed over a private `count`: the
same program as a `let` block containing the definition, without the
indentation and without `global` to push the name back out.

## The pragma

The `@` surface is gated by the `reader/decorators` pragma, on by
default. A module can turn it off:

```koskari
(pragma [reader/decorators False])
```

after which a leading `@` has no layout meaning and reads through the
ordinary atom rules. A reader built directly by tooling, rather than
through a module pragma, starts with the surface off like the other
pragma-gated reader features; readers built through
`LayoutReader.from_pragma`, which is the import path, the REPL, and the
lexer, get the default.

The pragma gates the reader surface only. The s-expression spelling
`(decorate ...)` is a macro and stays available. See
[Pragmas and feature settings](pragma.md).

## Reading

In layout mode the reader collects consecutive `@` lines at one column,
reads the single layout form beneath them, and wraps the lot:

```koskari
@(let [[c 0]])
@demand
def tick []: ...

;; reads as
(decorate ((let [[c 0]]) demand) (def tick [] ...))
```

Two conveniences follow from the layout rules. Parentheses are optional
on the decorator line, since several forms on a layout line are one
call, so `@let [[c 0]]` reads as `@(let [[c 0]])`. And the subject is
any layout form: a `def`, a `class`, a `begin:` block, an
`if`, a bare s-expression.

The reader builds the `compile/decorate` form and nothing more. What a
decorator means is decided by macro expansion, which is why the
s-expression spelling works with the pragma off.

## The decorate fold

[`compile/decorate`](../reference/macros/compile-decorate.md) does two
jobs.

Name hoisting: when the subject is a `def`, `var`, `class`,
`global def` or `global class`, or the Scheme-layer `define` or `define-global`,
the name moves outside the decoration so the decorated value is what
gets bound:

```koskari
(decorate [d1] (def foo [x] x))
;; expands to
(var foo (decorate [d1] (function foo [x] x)))
```

The fold: with a value subject, `decorate` peels one decorator per
expansion step into
[`compile/decorate-subject`](../reference/macros/compile-decorate-subject.md),
outermost first:

```koskari
(decorate [d1 d2] fn)
;; expands to
(decorate-subject d1 (decorate [d2] fn))
```

`decorate-subject` is where one decorator meets its subject, and it is
the extension point.

## Applying one decorator

`decorate-subject` dispatches on the shape of the decorator expression.
The head of the decorator says when its effect happens:

| Decorator | When the effect happens |
|---|---|
| `@some-callable` | at decoration: evaluated and called on the subject |
| `@let [[c 0]]` | at definition: lexical capture around the subject |
| `@with [(cm)]` | now: the extent spans the subject's evaluation |
| `@parameterize [[p v]]` | now: the extent spans the subject's evaluation |
| `@call-with [(cm)]` | per call |
| `@call-parameterize [[p v]]` | per call |
| `@entry-point` | at definition: the function becomes the module's entry point |

The default arm covers every ordinary runtime decorator, `@demand`,
`@(memoize maxsize: None)`, `@abstractmethod`: the expression evaluates
and its value is called with the subject's value.

`@entry-point` is a marking arm: it binds the function as the module's
`__entry__`, the function the launcher calls when the module runs as a
program, and delivers it unchanged, so the `def` binds the same
function. See [`entry-point`](../reference/macros/compile-entry-point.md).

The graft arms match a body-less `let`, `with`, or `parameterize` and
complete it by splicing the subject in as its body:

```koskari
(decorate-subject (let [[c 0]]) subject)
;; expands to
(let [[c 0]] subject)
```

Only the body-less spelling is special. A complete form with a body
misses the arm and falls to the default, where it evaluates as an
expression, so every decorator expression that already worked keeps its
meaning, and the graft arms claim only spellings that mean nothing on
their own.

Grafting fits `let` over a definition, since a closure is definition-time
capture, and any extent form over a plain form that runs once, now:

```koskari
@parameterize [[verbose True]]
begin:
    ...
```

It does not fit `with` or `parameterize` directly over a `def`: the
extent would span the function's creation, which no call observes. A
per-call extent uses the `call-` spelling.

The call arms wrap the subject in a forwarding function: enter the
extent, call the original with the passed arguments, close the extent,
deliver the value. The binding group sits inside the wrapper, so the
context-manager expression or the parameter values are re-evaluated on
every call, a fresh `(cm)` per invocation, as if the `with` were written
in the body. The wrapper answers for the function it wraps:
`decorate-wraps`, which is `functools.update_wrapper`, carries over
`__name__`, `__doc__`, the annotations, the `__dict__` contents, and the
`__wrapped__` chain.

```koskari
@let [[db (connect)]]
@call-with [lock]
def handle [msg]:
    ...
```

One connection captured at definition; the lock taken and released per
call. Stacked decorator lines read top to bottom as the nested blocks
would.

## Adding a decorator shape

Shape arms are macro arms on `compile/decorate-subject`, added with
[`extend-syntax`](../reference/macros/compile-extend-syntax.md). Added
arms are tried before the built-in ones, so an extension claims its
shape ahead of the invocation default:

```koskari
extend-syntax compile/decorate-subject:
  syntax-rules [my-extent]:
    case (decorate-subject (my-extent group) subject):
      (my-extent group subject)
```

That is the whole registration for a graft-style decorator: `@my-extent
[...]` over any form now completes `my-extent` with the subject as its
body. A call-time variant follows the `call-` pattern: wrap the subject
in a `lambda` that establishes the extent and forwards `*: args **:
kwds`.

Three rules keep the decorator position readable:

- Claim only body-less shapes. Match `(my-extent group)`, one group and
  no body, so complete expressions keep their invocation meaning.
- Follow the two families. A lexical or run-now form grafts; a per-call
  form gets a `call-` name.
- Match on the form's own head. Arm literals match symbolically, like
  every `syntax-rules` literal, so an aliased or module-qualified
  spelling of the head falls through to the default arm.

## Seeing the expansion

`macroexpand-1` shows the pipeline one step at a time, the hoist, the
peel, and the arm, which is the way to see why a decorator did or did
not fire:

```koskari
(macroexpand-1 '(decorate ((let [[c 0]])) (function f [] c)))
;; (decorate-subject (let [[c 0]]) (decorate [] (function f [] c)))
```
