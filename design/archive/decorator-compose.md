# `decorate-subject`: decorator expansion as an extension point

## Status

**Implemented and archived (2026-08-09).** `compile/decorate` peels one
decorator per expansion into `compile/decorate-subject`
([_decorator.ki:32](../../koskari/bootstrap/_decorator.ki:32)), whose default
arm is invocation and whose shape arms cover two families: the graft forms
`let` / `with` / `parameterize`, and the call-time forms `call-with` /
`call-parameterize` supported by the `decorate-wraps` builtin
([_decorator.ki:20](../../koskari/bootstrap/_decorator.ki:20)). The machinery
lives in its own bootstrap source, loaded second so every later bootstrap
file can use decorators; the `@` reader surface is gated by the
`reader/decorators` pragma (baseline on, off on a raw reader); and Function
identity metadata is writable, so `decorate-wraps` is a plain
`functools.update_wrapper`. Tests:
[tests/importer/test_decorate_subject.py](../../tests/importer/test_decorate_subject.py)
(behavior),
[tests/reader/test_layout_decorators.py](../../tests/reader/test_layout_decorators.py)
(layout lowering and the pragma gate), and
[tests/lib/test_function_metadata.py](../../tests/lib/test_function_metadata.py)
(metadata writability).

## The former expansion, and why it was replaced

The `@` layout prefix lowers decorator lines to
`(compile/decorate (deco-expr ...) INNER)`
([parser.py:3354](../../koskari/reader/parser.py:3354)). `compile/decorate`
hoists the name out of a `define` / `define-global` / `define-class` subject,
rewriting it to `function` / `class` and re-entering itself; its terminal arm
then routed through `function-compose`, placing `@d1` `@d2` over
`define foo [x] ...` at:

```koskari
(define foo ((lambda [*: args **: kwds] (d1 (d2 *: args **: kwds)))
             (function foo [x] ...)))
```

Three consequences:

1. **The function form entered through a varargs tunnel as a runtime
   argument.** It was compiled independently and handed over as a value, never
   lexically inside any decorator expression. No decorator could enclose it.
2. **A whole Function object was built, called once, and discarded** at every
   decorated definition, plus a tuple/dict pack and unpack. Paid at module
   load, and `@demand` alone appears on a great many definitions in this tree.
3. **The compose lambda occupied a frame** during decoration corresponding to
   no source the author wrote.

(1) is what the restructure removes. (2) and (3) made it worth doing on its
own. `function-compose` remains as a standalone combinator with its documented
behavior; `decorate` no longer routes through it.

## Structure

Two levels. `compile/decorate` keeps the name-hoisting arms verbatim and peels
one decorator per expansion:

```koskari
case (decorate [] fn):
  fn
;; ... the four name-hoisting arms, unchanged ...
case (decorate [d rest ...] fn):
  (decorate-subject d (decorate [rest ...] fn))
```

`compile/decorate-subject` applies **one** decorator expression to a subject
form. Its default arm is invocation, mimicking the former composition per
layer:

```koskari
case (decorate-subject deco subject):
  (deco subject)
```

`@d1` `@d2` therefore gives `(d1 (d2 (function foo [x] ...)))` through the
default arm, matching Python's `d1(d2(f))`. Outermost decorator listed first,
unchanged. Every ordinary runtime decorator (`@demand`,
`@(memoize maxsize: None)`, `@(help_for Syntax)`) goes through this arm and is
unaffected.

The division of labor: the hoisting arms guarantee the fold only ever sees
value forms, so `decorate-subject` never needs to know about `define`; and
shape arms register on the single-decorator form, so they never touch list
recursion.

## Two families

A shape arm matches the decorator expression's shape and produces a bespoke
expansion instead of an invocation. The arms fall into two families, and the
split answers the question "when does a decorator's effect happen": **the
decorator expression's head tells you.**

| Decorator | Family | Effect happens |
|---|---|---|
| `@let [[c 0]]` | graft | at definition -- lexical capture |
| `@with [(cm)]` | graft | now -- extent spans the subject's evaluation |
| `@parameterize [[p v]]` | graft | now -- extent spans the subject's evaluation |
| `@call-with [(cm)]` | call | per invocation |
| `@call-parameterize [[p v]]` | call | per invocation |

The two families exist because the correct extent differs by binding kind.
Lexical capture (`let`) belongs at definition time -- that is what a closure
*is*. A dynamic extent grafted around a `function` form opens and closes
around function *creation*, which no call ever observes; for callables the
useful dynamic extent is per invocation. Rather than declaring one reading the
winner, or forking semantics on the subject's syntax, each behavior gets its
own spelling.

Naming convention: **`call-` plus the exact form name** (`call-parameterize`,
not `call-parameterized`). `call-X` mechanically means "X, at call time" for
any extent form X, so a future extent form's call-time variant needs no naming
decision, and the base form is always recoverable by grepping off the prefix.

Two notes on the `call-` names:

- Scheme's `call-with-*` family (`call-with-values`,
  `call-with-current-continuation`) means *invoke this procedure now,
  providing X*. Koskari's `call-with` means *a wrapper that will establish X
  around each future call*. Near words, different tense; the decorator
  position disambiguates in practice.
- `call-with` and `call-parameterize` exist **only** as arm literals in
  `decorate-subject` -- they are not bound anywhere. Written outside decorator
  position they are ordinary unbound names.

## Graft family

A body-less extent form in decorator position is completed by grafting the
subject in as its body. All three arms are the same template, because each
form takes one group and then a body:

```koskari
case (decorate-subject (let bindings) subject):
  (let bindings subject)
case (decorate-subject (with binding) subject):
  (with binding subject)
case (decorate-subject (parameterize params) subject):
  (parameterize params subject)
```

Full expansion trace for `let`:

```koskari
@(let [[counter 0]])
@demand
define tick []: ...

;; reader
(compile/decorate ((let [[counter 0]]) demand) (define tick [] ...))

;; hoist arm
(define tick (decorate [(let [[counter 0]]) demand] (function tick [] ...)))

;; peel: let arm on decorate-subject
(define tick (let [[counter 0]] (decorate [demand] (function tick [] ...))))

;; peel: default arm
(define tick (let [[counter 0]] (demand (function tick [] ...))))
```

`tick` binds at module scope, because the `define` is outside the `let`.
Written by hand the same shape requires putting the definition inside a `let`
block and reaching for `define-global` to get the name back out, which inverts
the reading order: the reader scans into a block to find a top-level
definition.

Two properties, both load-bearing:

- **Only the body-less spelling is special.** `(let bindings)` matches exactly
  one subform after the head. A complete let with a body misses the arm and
  falls to the default, where it evaluates as an ordinary expression whose
  *result* is invoked on the subject. The special arm claims only the spelling
  that is meaningless as a standalone expression, so every decorator
  expression that already works keeps its meaning. The miss default is not
  merely compatibility; it is what makes shape arms safe to add at all.
- **The layout surface needs nothing new.** Decorator lines combine their
  forms into one, so the parens are optional: `@let [[counter 0]]`.

### The composing block

The graft arms make the decorator prefix a second spelling of block
composition, without the indentation:

```koskari
let [[c -1]]:
  define counter []:
    incr! c
    c

;; flattens to
@let [[c -1]]
define counter []:
  incr! c
  c
```

The subject is not required to be a definition. When `decorate`'s
name-hoisting arms miss, the subject passes through the fold as a plain form,
so any layout form composes:

```koskari
@parameterize [[verbose True]]
@with [(suppress KeyError)]
begin:
    ...

;; equivalent to
parameterize [[verbose True]]:
  with [(suppress KeyError)]:
    begin:
      ...
```

Over a *plain form* the graft's immediate extent is exactly right: the block
runs once, now, inside the extent. Over a *function-producing* subject the
dynamic-extent grafts are almost never what the author means -- see
[§The trap spelling](#the-trap-spelling).

## Call family

The call-time arms wrap the subject in a forwarding function: enter the
extent, call the original with the passed arguments, close the extent, deliver
the value.

```koskari
case (decorate-subject (call-with binding) subject):
  (let [[inner subject]]
    (decorate-wraps inner
      (lambda [*: args **: kwds]
        (with binding
          (inner *: args **: kwds)))))
case (decorate-subject (call-parameterize params) subject):
  (let [[inner subject]]
    (decorate-wraps inner
      (lambda [*: args **: kwds]
        (parameterize params
          (inner *: args **: kwds)))))
```

Properties, all verified by the tests:

- **The subject evaluates once**, at decoration time, into the
  hygiene-renamed `inner`. The function is not rebuilt per call.
- **The binding group sits inside the lambda**, so the context-manager
  expression / parameter values re-evaluate per invocation -- a fresh
  `(CM)` each call, exactly as if the `with` were written in the body.
- The extent closes before the value is delivered, and since koskari is
  ambiently async, a suspending body inside the extent already works.
- The stacked spelling reads correctly top-to-bottom:

  ```koskari
  @let [[db (connect)]]
  @call-with [lock]
  @call-parameterize [[verbose True]]
  define handle [msg]:
      ...
  ```

  One connection captured lexically at definition; the lock taken and
  released, and `verbose` established, per call.

### Why the call family must be syntax

A runtime `call-parameterize` *function* is impossible: `PUSH_PARAM`
([opcode/PUSH_PARAM.c](../../koskari/lib/opcode/PUSH_PARAM.c)) builds its
extent dict from a **compile-time names tuple** -- parameter extents are
name-keyed at compile time. A `parameterize` form inside a helper function's
body would parameterize the helper's own literal symbol, never the caller's
parameter. So the names must stay syntactic, which means macro arms.
`call-with` could have been a function, but symmetry (both families share the
`[(cm)]` group spelling) and per-call re-evaluation of the binding group argue
for the same shape.

The "real function" ingredient survives as **`decorate-wraps`**, the runtime
helper both templates call: a plain `functools.update_wrapper`, so a
delivered wrapper answers for the function it wraps -- `__name__`,
`__qualname__`, `__doc__`, `__module__`, `__annotations__`, the `__dict__`
merge, and the `__wrapped__` chain.

That works because koskari `Function` identity attributes are writable
([func.c](../../koskari/lib/func.c), implemented with this branch). Name and
qualname writes go to metadata override fields, never to the function's
semantic name: self-binding keys off the semantic name at call time and it
must stay fixed after construction, while `repr` follows the override and
tracebacks keep reading the code object's name -- the same split CPython
makes, where `wraps` renames a function without retitling its frames. The
annotations setter requires a dict and refreshes the derived `__signature__`.
`Demand` delegates identity reads to the wrapped function; `BoundMethod`
delegates them to `__func__` and refuses writes, matching CPython bound
methods. Contract tests:
[tests/lib/test_function_metadata.py](../../tests/lib/test_function_metadata.py).

## The trap spelling

`@with [(lock)]` or `@parameterize [[p v]]` directly over a `define` grafts a
dynamic extent around function *creation*, which no call will ever observe --
legal, and almost never meant. The remedy fits the lint plans rather than the
expansion: an advisory when a dynamic-extent graft decorator sits directly
over a function-producing subject, suggesting the `call-` spelling. Lexical
`@let` is exempt; definition-time is its correct meaning. See
[koskari-lint.md](../koskari-lint.md).

## Extension ordering

Shape arms must be tried before the invocation catch-all. This is guaranteed
by the mechanism, not by discipline: `Syntax.__add__`
([syntax.py:155](../../koskari/syntax.py:155)) **prepends** new transforms, so
`extend-syntax compile/decorate-subject` arms always precede the base
definition's default. `extend-syntax` is the established registration surface;
[_variables.ki](../../koskari/bootstrap/_variables.ki) extends `compile/set!`
three separate times. A third-party extent form registers mechanically:

```koskari
extend-syntax compile/decorate-subject:
  syntax-rules [my-extent]:
    case (decorate-subject (my-extent group) subject):
      (my-extent group subject)
```

## Considered and not taken

- **General macro expansion in compound-head position.** `((let [[a 1]]) fn)`
  has a pair, not a symbol, in head position, and one could imagine making
  such heads expandable by ordinary lookup. The blast radius covers every call
  form in the language; the shape dispatch lives scoped, extendable, and
  ordered inside `decorate-subject`'s own rule set instead.
- **Forking graft-vs-wrap semantics on the subject's syntax.** The peel keeps
  the literal subject visible inside the nested form, so arms *could* match
  through it and give `@with` graft meaning over blocks and wrapper meaning
  over `function` forms. Rejected: one spelling meaning two extents depending
  on the subject reads as magic; the two families put the choice in the name
  the reader sees.

## Verified behavior

- Layout lowering unchanged (pre-existing reader suite).
- Each arm dispatches as designed (macroexpand1 probes: peel, three grafts,
  two wrappers, default invocation, hoisting-before-peel).
- `@let` closure: private binding persists across calls and never leaks to
  module scope; a decorated function's self-name resolves past the grafted
  `let` to module scope (the `function` form deliberately carries no
  self-reference, [lambda_.py:102](../../koskari/compile/specials/lambda_.py:102)).
- `call-with`: per-call enter/exit, distinct context-manager instance per
  call, args/kwargs forwarded, value delivered, and the wrapper answers with
  the original's name, docstring, and `__wrapped__` chain.
- `call-parameterize`: value visible during the call, ambient restored after.
- Full regression surface green (compile, hygiene, syntax, importer, reader,
  help, cli, solve, reproducers, extras).

## Open questions

- **Symbolic matching.** The shape-arm literals match head symbols, exactly as
  the `define` / `define-class` literals in `decorate`. An aliased or
  module-qualified spelling misses the arm and falls to the default, where a
  body-less extent form evaluated as an expression errors at an odd altitude.
  Accepted; needs documenting wherever decorators are documented.
- **Subject-inspecting arms see the wrapper.** A shape arm's `subject` is the
  still-unexpanded inner `(decorate ...)` form. Splice-only arms -- everything
  shipped -- do not care; the constraint belongs in the `decorate-subject`
  extension docs.
- **`let` initializers evaluate in the enclosing scope** (the `flet`
  contrast), so `@let [[a 1] [b (+ a 1)]]` will not see `a`. Document, or
  offer a sequential variant.
- Class decoration: the `define-class` hoisting arms route through the same
  fold, so a graft over a `define-class` wraps the `class` form and a `call-`
  decorator would wrap the class in a callable (costing `isinstance`,
  subclassing, attribute access on the name). Scope the `call-` family to
  function subjects?
- Which other body-taking forms earn a base arm? `try` has clause structure
  rather than a simple body and probably stays out; `flet` / `labels` fit the
  graft template.

## Where this meets formal default modes

A binding introduced by `@let [[cache #{}]]` is shared across every call to
the decorated function. A `copy:` or `new:` default
([formal-default-modes.md](../language/formal-default-modes.md)) produces a
fresh value
per call. Both are things a mutable default value gets used for today, so a
lint that flags a mutable default has a concrete spelling to suggest for
either intent.
