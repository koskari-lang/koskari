# Decorator compose and the `closure` decorator

## Status

**Proposal (2026-08-09).** Not implemented.

Two changes, the second enabled by the first: replace `compile/decorate`'s
terminal expansion with literal nesting, then add `closure`, a `let`-style
decorator that injects a lexical binding around a definition.

## Today's expansion

`compile/decorate` ([_functions.ki:126](../../koskari/bootstrap/_functions.ki:126))
ends at:

```koskari
case (decorate [decorators ...] fn):
    ((function-compose decorators ...) fn)
```

and `compile/function-compose`
([_functions.ki:42](../../koskari/bootstrap/_functions.ki:42)) documents its own
expansion:

```koskari
(function-compose fn1 (fn2 foo) fn3)

;; is equivalent to
(lambda [*: args **: kwds]
  (fn1 ((fn2 foo) (fn3 *: args **: kwds))))
```

So `@d1` `@d2` over `define foo [x] ...` lands at:

```koskari
(define foo ((lambda [*: args **: kwds] (d1 (d2 *: args **: kwds)))
             (function foo [x] ...)))
```

Three consequences:

1. **The function form enters through a varargs tunnel as a runtime argument.**
   It is compiled independently and handed over as a value, so it is not
   lexically inside any decorator expression. No decorator can enclose it.
2. **A whole Function object is built, called once, and discarded** at every
   decorated definition, plus a tuple/dict pack and unpack. Paid at module load,
   and `@demand` alone appears on a great many definitions in this tree.
3. **The compose lambda occupies a frame** during decoration that corresponds to
   no source the author wrote.

(1) is what this proposal is about. (2) and (3) are why the change is worth
making on its own.

## Proposed: `decorator-compose`

Literal nesting, one decorator per layer:

```koskari
case (decorate [d rest ...] fn):
    (d (decorate [rest ...] fn))
```

`@d1` `@d2` then gives `(d1 (d2 (function foo [x] ...)))`, matching Python's
`d1(d2(f))`. Outermost decorator listed first, unchanged from today.

`function-compose` stays as a standalone combinator with its documented
behavior; `decorate` stops routing through it.

## The `closure` decorator

```koskari
@(closure [counter 0])
define tick []:
    incr! counter 1
    counter
```

expands through `decorate`'s existing `define` arm
([_functions.ki:122](../../koskari/bootstrap/_functions.ki:122)) to

```koskari
(define tick (decorate [(closure [counter 0])] (function tick [] ...)))
```

and then to

```koskari
(define tick (let [[counter 0]] (function tick [] ...)))
```

`tick` binds at module scope, because `define` is outside the `let`. Written by
hand today the same shape requires putting the definition inside a `let` block
and reaching for `define-global` to get the name back out to module scope, which
inverts the reading order: the reader scans into a block to find a top-level
definition.

Stacking composes the obvious way. `@(closure ...)` listed first puts the `let`
outermost, so the binding is visible to the remaining decorators as well as to
the function body:

```koskari
@(closure [cache #{}])
@demand
define lookup [k]:
    ...
;; -> (define lookup (let [[cache #{}]] (demand (function lookup [k] ...))))
```

## Mechanism

`((closure [a 1]) (function foo [x] ...))` has a **pair** in head position
rather than a symbol, so ordinary macro dispatch does not fire on it.

**Recommended:** recognize `closure` as a literal in `compile/decorate`'s own
rule set, exactly as it already matches `define` and `define-class`
structurally in the decorated form
([_functions.ki:115](../../koskari/bootstrap/_functions.ki:115) declares
`syntax-rules [define-class define compile/define-class compile/define]`). One
additional arm:

```koskari
case (decorate [(closure bindings) rest ...] fn):
    (let bindings (decorate [rest ...] fn))
```

No new head-position machinery, and the arm sits beside the arms it resembles.
Extensibility comes from `extend-syntax compile/decorate`, already the
established pattern for `compile/set!`, which
[_variables.ki](../../koskari/bootstrap/_variables.ki) extends three separate
times.

**Considered and not recommended:** general macro expansion in compound-head
position. It would make `((closure [a 1]) fn)` expand by ordinary lookup, but
the blast radius covers every call form in the language, and this feature does
not need it.

## Risks

- **Self-reference across the new namespace.**
  [lambda_.py:102](../../koskari/compile/specials/lambda_.py:102) notes that
  `function` forms deliberately receive no self-reference, so that a decorated
  self-call resolves to the wrapped definition at module scope. The `let` does
  not bind `foo`, so lookup should chain past it to the module binding, but this
  is the most likely place for a surprise and wants a test.
- **Ordering is a rule, not an accident.** That `@(closure ...)` first means the
  `let` is outermost, and therefore visible to sibling decorators, should be
  specified rather than inferred from the expansion.
- **`let` initializers evaluate in the enclosing scope.** The `flet` docstring
  ([_functions.ki:75](../../koskari/bootstrap/_functions.ki:75)) draws this
  contrast against `labels`. So `@(closure [a 1] [b (+ a 1)])` will not see `a`.
  Document it, or offer a sequential variant.
- **Every decorated definition in the tree changes expansion.** Ordinary runtime
  decorators should be unaffected; the test suite is what establishes that.

## Open questions

- Sequential variant (`closure*` / `let*` semantics) -- ship or defer?
- Does `closure` accept several bindings in one decorator, several stacked
  `@(closure ...)` lines, or both?
- Class decoration: `decorate`'s `define-class` arms route through the same
  terminal case, so `@(closure ...)` over a `define-class` would wrap the class
  form. Intended, or restricted to functions?
- Does anything outside `decorate` depend on the compose lambda's existence
  (introspection, `__wrapped__` chains, the Sphinx extras)?

## Where this meets formal default modes

A binding introduced by `@(closure ...)` is shared across every call to the
decorated function. A `copy:` or `new:` default
([formal-default-modes.md](formal-default-modes.md)) produces a fresh value per
call. Both are things a mutable default value gets used for today, so a lint
that flags a mutable default has a concrete spelling to suggest for either
intent.
