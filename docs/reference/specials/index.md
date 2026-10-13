# Special forms

A special form is a head the compiler handles itself. When a form's head
names one, the compiler emits bytecode for it directly, following the
form's own rules for which arguments are evaluated and when. Every other
head in call position is a runtime call. Each page below gives the
form's symbol, syntax, behaviour, and examples; the surface names most
programs write, `if`, `when`, `set!`, `class`, are
[macros](../macros/index.md) that lower to these.

The compiler binds each special under its `compile/` name, and the
short name is how a program uses it. How `(foo ...)` decides between a
special, a macro, and a call is in
[Name resolution and prefixes](../../guide/name-resolution-and-prefixes.md).

## Definition and binding

| Form | Does |
|------|------|
| [`var`](compile-var.md) | Bind a name in the current scope, with an optional type, or several names by unpacking |
| [`global`](compile-global.md) | Bind a name at the module from any depth; `global def`, `global class`, `global syntax` |
| [`define`](compile-define.md) | The Scheme-layer spelling of `var`, or of `def` when formals follow |
| [`define-global`](compile-define-global.md) | The Scheme-layer spelling of `global` |
| [`define-values`](compile-define-values.md) | The Scheme-layer spelling of `var` over a pattern |
| [`function`](compile-function.md) | A named function value that is not bound |
| [`lambda`](compile-lambda.md) | An anonymous function value; formals, defaults, rest collectors, annotations, by-ref |
| [`let`](compile-let.md) | Lexical bindings for a body; named `let` for loops |
| [`letrec`](compile-letrec.md) | Lexical bindings that may refer to each other |
| [`setq`](compile-setq.md) | Store into an existing binding; the special under `set!` |
| [`setq-values`](compile-setq-values.md) | Store into several bindings by unpacking |
| [`bindq`](compile-bindq.md) | Bind a name to a ref |
| [`refq`](compile-refq.md) | The ref of a variable; the special under `&name` |
| [`deref`](compile-deref.md) | Read through a ref |
| [`takeq`](compile-takeq.md) | Read a binding and clear it |
| [`delq`](compile-delq.md) | Delete a binding |
| [`globals`](compile-globals.md), [`vars`](compile-vars.md) | The module namespace; the current scope's namespace, or an object's `__dict__` |
| [`parameter`](compile-parameter.md), [`parameterize`](compile-parameterize.md) | Read a dynamic parameter; rebind parameters for a body |

## Control flow

| Form | Does |
|------|------|
| [`begin`](compile-begin.md) | Run forms in order; the value is the last |
| [`cond`](compile-cond.md) | The first clause whose test is true; what layout `if` reads to |
| [`switch`](compile-switch.md) | Branch on equality against candidates |
| [`match`](compile-match.md) | Destructure against patterns; see [Match patterns](../../guide/match-patterns.md) |
| [`and`](compile-and.md), [`or`](compile-or.md) | Short-circuit conjunction and disjunction |
| [`while`](compile-while.md) | Loop while a test is true, with an `else:` clause |
| [`for-each`](compile-for-each.md) | Loop over an iterable, with destructuring and an `else:` clause |
| [`break`](compile-break.md), [`continue`](compile-continue.md) | Leave a loop, with an optional value; start the next iteration |
| [`return`](compile-return.md) | Leave a function; several values return a tuple, keywords a `values` |
| [`try`](compile-try.md) | Handle exceptions, with `else:` and `finally:` clauses |
| [`raise`](compile-raise.md) | Raise an exception, or re-raise the current one |
| [`check-exception`](compile-check-exception.md), [`load-exception`](compile-load-exception.md), [`clear-exception`](compile-clear-exception.md) | Internal forms that `try` expands into |

## Objects, attributes, and items

| Form | Does |
|------|------|
| [`attr`](compile-attr.md), [`set-attr`](compile-set-attr.md), [`del-attr`](compile-del-attr.md) | Read, store, delete an attribute; `obj.name` reads as `attr` |
| [`ref-attr`](compile-ref-attr.md), [`take-attr`](compile-take-attr.md), [`inplace-attr`](compile-inplace-attr.md) | A ref to an attribute; take and delete; update in place |
| [`item`](compile-item.md), [`set-item`](compile-set-item.md), [`del-item`](compile-del-item.md) | Read, store, delete a subscript; `obj[key]` reads as `item` |
| [`ref-item`](compile-ref-item.md), [`take-item`](compile-take-item.md), [`inplace-item`](compile-inplace-item.md) | A ref to a subscript; take and delete; update in place |
| [`inplace`](compile-inplace.md) | Update a variable in place; the special under `incr!` and `decr!` |
| [`call`](compile-call.md) | Call a value; forces a runtime call past a same-named macro |
| [`call-attr`](compile-call-attr.md) | Call a method by name; `(obj.method ...)` reads to this |
| [`build-class`](compile-build-class.md) | Build a class value without binding a name; what `class` lowers to |
| [`instance?`](compile-instance.md), [`subclass?`](compile-subclass.md) | Type tests; a tuple as the second operand tests several types |
| [`#str`](compile-str.md), [`format`](compile-format.md) | String conversion; `format` with an optional bare format spec |
| [`#list`, `#tuple`, `#list-append`](object-operators/index.md#collection-builders) | Build a list or tuple; append to a list |

## Operators

| Form | Does |
|------|------|
| [`==`](compile-eqeq.md), [`!=`](compile-eq.md), [`equal?`](compile-equal.md), [`not-equal?`](compile-not-equal.md) | Equality, chained over any number of operands |
| [`<`](compile-lt.md), [`<=`](compile-lteq.md), [`>`](compile-gt.md), [`>=`](compile-gteq.md) | Ordering, chained |
| [`le?`](compile-le.md), [`ge?`](compile-ge.md) | Aliases for `<=` and `>=` |
| [`**`](compile-starstar.md) | Exponentiation with an optional modulus |
| [`in`, `not-in`, `is`, `is-not`](object-operators/index.md#membership-and-identity) | Membership and identity, each with a `?`-suffixed alias |
| [`not`, `truth`, `nil?`, `repr`, `len`, `type`, `dir`, `vars`, `iter`, `next`, `divmod`](object-operators/index.md#unary-and-inspection-operators) | The unary and inspection operators |
| [Object operators](object-operators/index.md) | The arithmetic, bitwise, and unary operators, constant folding, and the comparison table |

## Pairs and quoting

| Form | Does |
|------|------|
| [`cons`](compile-cons.md), [`car`](compile-car.md), [`cdr`](compile-cdr.md) | Build a pair chain; its first element; its rest |
| [`bracket-cons`](compile-bracket-cons.md), [`bracket-proper`](compile-bracket-proper.md) | Build a bracket pair chain; a proper bracket list |
| [`quote`](compile-quote.md) | A form as data; `'form` |
| [`quote-syntax`](compile-quote-syntax.md) | A form as data, keeping hygiene marks; `#'form`; what a transformer's patterns and templates are quoted with |
| [`quasiquote`](compile-quasiquote.md), [`unquote`](compile-unquote.md), [`unquote-splicing`](compile-unquote-splicing.md) | A template; `` `form ``, `,form`, `,@form` |
| [`quasiquote-syntax`](compile-quasiquote-syntax.md) | A template keeping hygiene marks; `` #`form ``; what a procedural macro a template defines builds its output with |
| [Block quote](compile-quote-block.md), [block quasiquote](compile-quasiquote-block.md) | The reader shorthands `'head:` and `` `head: ``, which quote an indented block as its `#block:` cons; `#'head:` and `` #`head: `` do the same keeping hygiene marks |

## Async, generators, and continuations

| Form | Does |
|------|------|
| [`await`](compile-await.md) | Await a held awaitable; bare `(await)` is a cooperative suspend |
| [`yield`](compile-yield.md), [`yield-from`](compile-yield-from.md) | Generator and coroutine suspension; delegation |
| [`with`](with.md), [`dyn-with`](dyn-with.md) | Context managers, lexical and dynamic-extent |
| [`dyn-wind`](compile-dyn-wind.md) | Before and after thunks around a body, rerun across continuation jumps |
| [`reset`](compile-reset.md), [`shift`](compile-shift.md) | Delimited continuations |
| [`call-cc`](compile-call-cc.md), [`let-cc`](compile-let-cc.md), [`capture-cc`](compile-capture-cc.md) | Undelimited continuations, from the Scheme form down to the raw capture |

## Modules

| Form | Does |
|------|------|
| [`import`](import.md) | Import a module; `[MODULE as: NAME]` binds the leaf |
| [`import-from`](import-from.md) | Import names from a module, with aliases |
| [`import-all-from`](import-all-from.md) | Bind a module's public names, at module level only |
| [`use`](use.md) | Import a feature with all its facets, a macro together with its runtime sibling |

## Macro definition and the compiler

| Form | Does |
|------|------|
| [`syntax-rules`](compile-syntax-rules.md), [`syntax-match`](compile-syntax-match.md) | Template transformers, by rule list and by named spec |
| [`macro-rules`](compile-macro-rules.md), [`macro-match`](compile-macro-match.md) | Procedural transformers |
| [`special-rules`](compile-special-rules.md), [`special-match`](compile-special-match.md) | Transformers that receive the compiler and emit bytecode |
| [`bare-syntax`](compile-bare-syntax.md) | The arm used when a syntax name appears as a lone atom |
| [`gensym-thunk`](compile-gensym-partial.md) | The `{...}` thunk with inferred `%1`, `%2` formals |
| [`apply-fence`](compile-apply-fence.md) | Expand a fenced foreign literal through its handler |
| [`bytecode`](compile-bytecode.md) | Inject bytecode directly |

## Patterns and hygiene

Many specials declare their shape with [`koskari.matcher`](../api/koskari-matcher.md)
patterns, and a form that fits none of a special's patterns fails with
`SyntaxMatchError` at compile time. The transformer forms share one
matcher and one hygiene model; see
[Macros and hygiene](../../guide/macros-and-hygiene.md) and
[`koskari.hygiene`](../api/koskari-hygiene.md).

```{toctree}
:maxdepth: 1
:hidden:
:glob:

object-operators/index
*
```
