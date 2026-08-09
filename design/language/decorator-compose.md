# `decorate-subject`: decorator expansion as an extension point

## Status

**Proposal (2026-08-09).** Not implemented.

Restructure `compile/decorate`'s terminal expansion from `function-compose`
routing into a fold over a new single-decorator macro, `compile/decorate-subject`,
whose default arm is literal invocation and whose extension arms may match the
decorator expression's *shape*. Three shape arms ship in the base definition --
`let`, `with`, and `parameterize` -- turning the decorator prefix into a
composing block: any extent form can wrap a definition (or any layout form)
without a level of indentation.

## Today's expansion

The `@` layout prefix lowers decorator lines to
`(compile/decorate (deco-expr ...) INNER)`
([parser.py:3354](../../koskari/reader/parser.py:3354)). `compile/decorate`
([_functions.ki:109](../../koskari/bootstrap/_functions.ki:109)) hoists the name
out of a `define` / `define-global` / `define-class` subject, rewriting it to
`function` / `class` and re-entering itself; its terminal arm then routes
through `function-compose`:

```koskari
case (decorate [decorators ...] fn):
    ((function-compose decorators ...) fn)
```

and `compile/function-compose`
([_functions.ki:42](../../koskari/bootstrap/_functions.ki:42)) documents its own
expansion, so `@d1` `@d2` over `define foo [x] ...` lands at:

```koskari
(define foo ((lambda [*: args **: kwds] (d1 (d2 *: args **: kwds)))
             (function foo [x] ...)))
```

Three consequences:

1. **The function form enters through a varargs tunnel as a runtime argument.**
   It is compiled independently and handed over as a value, so it is not
   lexically inside any decorator expression. No decorator can enclose it.
2. **A whole Function object is built, called once, and discarded** at every
   decorated definition, plus a tuple/dict pack and unpack. Paid at module
   load, and `@demand` alone appears on a great many definitions in this tree.
3. **The compose lambda occupies a frame** during decoration that corresponds
   to no source the author wrote.

(1) is what this proposal is about. (2) and (3) are why the restructure is
worth making on its own.

## Proposed structure

Two levels. `compile/decorate` keeps the name-hoisting arms verbatim and
changes only its terminal case, peeling one decorator per expansion:

```koskari
define-syntax compile/decorate:
  syntax-rules [define-class define compile/define-class compile/define]:
    case (decorate [] fn):
      fn

    ;; the four existing name-hoisting arms, unchanged: rewrite
    ;; define->function / define-class->class, re-enter decorate
    case (decorate [decorators ...] (define name formals body ...)):
      (define name (decorate [decorators ...] (function name formals body ...)))
    ;; ... define-class, compile/define, compile/define-class ...

    ;; terminal fold: peel one decorator per expansion
    case (decorate [d rest ...] fn):
      (decorate-subject d (decorate [rest ...] fn))
```

`compile/decorate-subject` applies **one** decorator expression to a subject
form. Its default arm is invocation, mimicking today's composition per layer:

```koskari
define-syntax compile/decorate-subject:
  """
  Apply one decorator expression to a subject form. Special arms may
  match the decorator expression's shape; the default is invocation.
  """

  syntax-rules []:
    case (decorate-subject deco subject):
      (deco subject)
```

`@d1` `@d2` therefore gives `(d1 (d2 (function foo [x] ...)))` through the
default arm, matching Python's `d1(d2(f))`. Outermost decorator listed first,
unchanged from today. The subject of an outer `decorate-subject` may itself be
a `decorate` / `decorate-subject` form, down to the final literal subject.

The division of labor: the hoisting arms guarantee the fold only ever sees
value forms, so `decorate-subject` never needs to know about `define`; and
extensions register on the single-decorator form, so they never touch list
recursion.

`function-compose` stays as a standalone combinator with its documented
behavior; `decorate` stops routing through it.

## Shape arms: `let`, `with`, `parameterize`

A special arm matches the decorator expression's shape and produces a bespoke
expansion instead of an invocation. A body-less extent form in decorator
position is completed by grafting the subject in as its body. Three ship in the
base definition, and all three are the same template, because each form takes
one group and then a body:

```koskari
case (decorate-subject (let bindings) subject):
  (let bindings subject)
case (decorate-subject (with binding) subject):
  (with binding subject)
case (decorate-subject (parameterize params) subject):
  (parameterize params subject)
```

(`with` is `(with [name? expr] BODY...)`
([context.py:130](../../koskari/compile/specials/context.py:130)); that the
targets are specials rather than macros is irrelevant to the arm, which only
splices source.)

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
Written by hand today the same shape requires putting the definition inside a
`let` block and reaching for `define-global` to get the name back out, which
inverts the reading order: the reader scans into a block to find a top-level
definition.

Two properties of the arm, both load-bearing:

- **Only the body-less spelling is special.** `(let bindings)` matches exactly
  one subform after the head. A complete let with a body misses the arm and
  falls to the default, where it evaluates as an ordinary expression whose
  *result* is invoked on the subject. The special arm claims only the spelling
  that is meaningless as a standalone expression, so every decorator expression
  that already works keeps its meaning. The miss default is not merely
  compatibility; it is what makes shape arms safe to add at all.
- **The layout surface needs nothing new.** Decorator lines combine their forms
  into one, so the parens are optional:

  ```koskari
  @let [[counter 0]]
  define tick []: ...
  ```

  which reads as native binding-decorator syntax while being nothing but the
  existing reader lowering plus one arm.

Because `@(let ...)` listed first puts its expansion outermost, the binding is
visible to the remaining decorators as well as to the function body:

```koskari
@let [[cache #{}]]
@demand
define lookup [k]:
    ...
;; -> (define lookup (let [[cache #{}]] (demand (function lookup [k] ...))))
```

## Decorators as a composing block

The shape arms make the decorator prefix a second spelling of block
composition, without the indentation. Today an extent form costs a level:

```koskari
let [[c -1]]:
  define counter []:
    incr! c
    c
```

As a decorator line the same program flattens:

```koskari
@let [[c -1]]
define counter []:
  incr! c
  c
```

And the subject is not required to be a definition. When `decorate`'s
name-hoisting arms miss, the subject passes through the fold as a plain form,
so any layout form composes:

```koskari
@let [[c -1]]
begin:
  ...

;; equivalent to
let [[c -1]]:
  begin:
    ...
```

Stacked extent forms are where the flattening pays most. Each nested extent
today costs an indent level for the entire body beneath it; as decorator lines
they cost none:

```koskari
@let [[db (connect)]]
@with [lock]
@parameterize [[verbose True]]
define handle [msg]:
    ...
```

Ordering follows the existing decorator rule -- outermost first -- which reads
top-to-bottom exactly as the indented nesting would.

## Extension ordering

Shape arms must be tried before the invocation catch-all. This is guaranteed by
the mechanism, not by discipline: `Syntax.__add__`
([syntax.py:155](../../koskari/syntax.py:155)) **prepends** the new transforms,
so `extend-syntax compile/decorate-subject` arms always precede the base
definition's default. `extend-syntax` is the established registration surface;
[_variables.ki](../../koskari/bootstrap/_variables.ki) extends `compile/set!`
three separate times the same way.

The base shape arms (`let`, `with`, `parameterize`) sit above the catch-all in
the base definition; `extend-syntax` is how everything else registers, e.g. a
third-party extent form:

```koskari
extend-syntax compile/decorate-subject:
  syntax-rules [my-extent]:
    case (decorate-subject (my-extent group) subject):
      (my-extent group subject)
```

## Considered and not recommended

General macro expansion in compound-head position. `((let [[a 1]]) fn)` has a
pair, not a symbol, in head position, and one could imagine making such heads
expandable by ordinary lookup. The blast radius covers every call form in the
language, and this feature does not need it: the shape dispatch lives inside
`decorate-subject`'s own rule set, where it is scoped, extendable, and ordered.

## Risks

- **Self-reference across the new namespace.**
  [lambda_.py:102](../../koskari/compile/specials/lambda_.py:102) notes that
  `function` forms deliberately receive no self-reference, so that a decorated
  self-call resolves to the wrapped definition at module scope. The `let` does
  not bind `tick`, so lookup should chain past it to the module binding, but
  this is the most likely place for a surprise and wants a test.
- **Symbolic matching.** The shape-arm literals match the symbols `let` /
  `with` / `parameterize` at the head of the decorator expression, exactly as
  the existing `define` / `define-class` literals match in `decorate`. An
  aliased or module-qualified spelling misses the arm and falls to the default,
  where a body-less extent form evaluated as an expression is a compile error
  surfacing at an odd altitude. Acceptable; document it.
- **Subject-inspecting arms see the wrapper.** Expansion is outside-in, so a
  shape arm's `subject` is the still-unexpanded inner `(decorate ...)` form,
  not the final `function` form. Splice-only arms -- everything proposed here
  -- do not care. An arm that wanted to inspect the subject would need to be
  written against the wrapper, and that constraint should be stated wherever
  `decorate-subject` extension is documented.
- **`let` initializers evaluate in the enclosing scope.** The `flet` docstring
  ([_functions.ki:75](../../koskari/bootstrap/_functions.ki:75)) draws this
  contrast against `labels`. So `@let [[a 1] [b (+ a 1)]]` will not see `a`.
  Document it, or offer a sequential variant.
- **Every decorated definition in the tree changes expansion.** Ordinary
  runtime decorators go through the default arm and should be unaffected; the
  test suite is what establishes that.

## Open questions

- Sequential-binding variant (`let*` semantics) -- ship or defer?
- Class decoration: the `define-class` hoisting arms route through the same
  fold, so `@let [[x 1]]` over a `define-class` wraps the `class` form.
  Intended, or restricted to functions?
- Which other body-taking forms earn a base arm? `try` has clause structure
  rather than a simple body and probably stays out; `flet` / `labels` fit the
  template.
- Does anything outside `decorate` depend on the compose lambda's existence
  (introspection, `__wrapped__` chains, the Sphinx extras)?

## Where this meets formal default modes

A binding introduced by `@let [[cache #{}]]` is shared across every call to the
decorated function. A `copy:` or `new:` default
([formal-default-modes.md](formal-default-modes.md)) produces a fresh value per
call. Both are things a mutable default value gets used for today, so a lint
that flags a mutable default has a concrete spelling to suggest for either
intent.
