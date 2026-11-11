# Builtin names

Every name a module has bound by default, by section. Each entry links
to the page that documents it. A name can answer in more than one
section -- `try` is a layout handler and a special form, `+` a special
form and an operator function -- and
[Names with more than one meaning](#names-with-more-than-one-meaning)
lists each of those with all its pages.

- [Layout handlers](#layout-handlers): block heads that read an indented block into a form
- [Special forms](#special-forms): forms the compiler handles directly
- [Macros](#macros): forms that expand to special forms
- [Fences](#fences): triple-backtick foreign literals
- [Runtime](#runtime): types and type predicates, operators, functions, and exceptions

## Layout handlers


| Head | Peers | Reads to |
|------|-------|----------|
| [any other head](layouts/layout-block-default.md) | | `(HEAD ARGS... BODY...)`, one argument per body line, keyword lines as keyword arguments |
| [`block-begin`](layouts/layout-block-begin.md) | | `(HEAD ARGS... (begin BODY...))`; bind `layout/<head>` to it for a head that wants one body form |
| [`inline-block`](layouts/layout-inline-block.md) | | `(HEAD ARGS... ELEMENT...)`, every element of every body line spliced and nothing folded; bind `layout/<head>` to it for a head whose block only breaks a long line |
| [`if`](layouts/layout-if.md) | `elif`, `else` | `(cond [TEST BODY] ... [else: BODY])` |
| [`while`](layouts/layout-while.md) | `else` | `(while TEST BODY... (else: BODY...))` |
| [`for-each`](layouts/layout-for-each.md) | `else` | `(for-each [VAR SEQ] BODY... (else: BODY...))` |
| [`try`](layouts/layout-try.md) | `except`, `else`, `finally` | `(try (begin BODY...) ([Type] BODY...) ... (else: ...) (finally: ...))`, each except clause keeping the bracketed form as written |
| [`dyn-wind`](layouts/layout-dyn-wind.md) | `before`, `after` | `(dyn-wind BEFORE (begin BODY...) AFTER)`, an absent peer emitting `()` |
| [`switch`](layouts/layout-switch.md) | | `(switch SUBJECT [VALUE BODY...] ... [else: BODY...])` |
| [`match`](layouts/layout-match.md) | | `(match SUBJECT [[PATTERN...] BODY...] ... [else: BODY...])` |
| [`class`](layouts/layout-class.md) | | `(class NAME SUPERS (begin BODY...))`, a class bound to NAME |
| [`build-class`](layouts/layout-build-class.md) | | `(build-class NAME SUPERS (begin BODY...))`, a class expression that binds nothing |
| [`global`](layouts/layout-global.md) | | the block of the word after `global`, read through that word's layout, with `global` put back in front |
| [`syntax-rules`](layouts/layout-syntax-rules.md) | | a `syntax-rules` transformer, one arm per rule block |
| [`syntax-match`](layouts/layout-syntax-match.md) | | a `syntax-match` transformer |
| [`macro-rules`](layouts/layout-macro-rules.md) | | a `macro-rules` transformer |
| [`macro-match`](layouts/layout-macro-match.md) | | a `macro-match` transformer |
| [`special-rules`](layouts/layout-special-rules.md) | | a `special-rules` transformer |
| [`special-match`](layouts/layout-special-match.md) | | a `special-match` transformer |
| [`bare-syntax`](layouts/layout-bare-syntax.md) | | `(bare-syntax [NAME] SLOT TEMPLATE)`, the arm for the name used as a lone atom |

## Special forms

### Definition and binding

| Form | Does |
|------|------|
| [`var`](specials/compile-var.md) | Bind a name in the current scope, with an optional type, or several names by unpacking |
| [`define`](specials/compile-define.md) | The Scheme-layer spelling of `var`, or of `def` when the name position is a pair `[NAME FORMALS ...]` |
| [`define-global`](specials/compile-define-global.md) | Bind a name at the module from any depth; what `global var` and `global def` lower to |
| [`define-values`](specials/compile-define-values.md) | Bind the names of a pattern in the current scope; what `var` over a pattern lowers to |
| [`define-global-values`](specials/compile-define-global-values.md) | Bind the names of a pattern at the module; what `global var` over a pattern lowers to |
| [`function`](specials/compile-function.md) | A named function value that is not bound |
| [`lambda`](specials/compile-lambda.md) | An anonymous function value; formals, defaults, rest collectors, annotations, by-ref |
| [`let`](specials/compile-let.md) | Lexical bindings for a body; named `let` for loops |
| [`letrec`](specials/compile-letrec.md) | Lexical bindings that may refer to each other |
| [`setq`](specials/compile-setq.md) | Store into an existing binding; the special under `set!` |
| [`setq-values`](specials/compile-setq-values.md) | Store into several bindings by unpacking |
| [`bindq`](specials/compile-bindq.md) | Bind a name to a ref |
| [`refq`](specials/compile-refq.md) | The ref of a variable; the special under `&name` |
| [`deref`](specials/compile-deref.md) | Read through a ref |
| [`takeq`](specials/compile-takeq.md) | Read a binding and clear it |
| [`delq`](specials/compile-delq.md) | Delete a binding |
| [`globals`](specials/compile-globals.md), [`vars`](specials/compile-vars.md) | The module namespace; the current scope's namespace, or an object's `__dict__` |
| [`parameter`](specials/compile-parameter.md), [`parameterize`](specials/compile-parameterize.md) | Read a dynamic parameter; rebind parameters for a body |

### Control flow

| Form | Does |
|------|------|
| [`begin`](specials/compile-begin.md) | Run forms in order; the value is the last |
| [`cond`](specials/compile-cond.md) | The first clause whose test is true; what layout `if` reads to |
| [`switch`](specials/compile-switch.md) | Branch on identity or equality against candidates |
| [`match`](specials/compile-match.md) | Destructure against patterns; see [Match patterns](../guide/match-patterns.md) |
| [`and`](specials/compile-and.md), [`or`](specials/compile-or.md) | Short-circuit conjunction and disjunction |
| [`while`](specials/compile-while.md) | Loop while a test is true, with an `else:` clause |
| [`for-each`](specials/compile-for-each.md) | Loop over an iterable, with destructuring and an `else:` clause |
| [`break`](specials/compile-break.md), [`continue`](specials/compile-continue.md) | Leave a loop, with an optional value; start the next iteration |
| [`return`](specials/compile-return.md) | Leave a function; several values return a tuple, keywords a `values` |
| [`try`](specials/compile-try.md) | Handle exceptions, with `else:` and `finally:` clauses |
| [`raise`](specials/compile-raise.md) | Raise an exception, or re-raise the current one |
| [`check-exception`](specials/compile-check-exception.md), [`load-exception`](specials/compile-load-exception.md), [`clear-exception`](specials/compile-clear-exception.md) | Internal forms that `try` expands into |

### Objects, attributes, and items

| Form | Does |
|------|------|
| [`attr`](specials/compile-attr.md), [`set-attr`](specials/compile-set-attr.md), [`del-attr`](specials/compile-del-attr.md) | Read, store, delete an attribute; `obj.name` reads as `attr` |
| [`ref-attr`](specials/compile-ref-attr.md), [`take-attr`](specials/compile-take-attr.md), [`inplace-attr`](specials/compile-inplace-attr.md) | A ref to an attribute; take and delete; update in place |
| [`item`](specials/compile-item.md), [`set-item`](specials/compile-set-item.md), [`del-item`](specials/compile-del-item.md) | Read, store, delete a subscript; `obj[key]` reads as `item` |
| [`ref-item`](specials/compile-ref-item.md), [`take-item`](specials/compile-take-item.md), [`inplace-item`](specials/compile-inplace-item.md) | A ref to a subscript; take and delete; update in place |
| [`inplace`](specials/compile-inplace.md) | Update a variable in place; the special under `incr!` and `decr!` |
| [`call`](specials/compile-call.md) | Call a value; forces a runtime call past a same-named macro |
| [`call-attr`](specials/compile-call-attr.md) | Call a method by name; `(obj.method ...)` reads to this |
| [`build-class`](specials/compile-build-class.md) | Build a class value without binding a name; what `class` lowers to |
| [`instance?`](specials/compile-instance.md), [`subclass?`](specials/compile-subclass.md) | Type tests; a tuple as the second operand tests several types |
| [`#str`](specials/compile-str.md), [`format`](specials/compile-format.md) | String conversion; `format` with an optional bare format spec |
| [`#list`, `#tuple`, `#list-append`](specials/object-operators/index.md#collection-builders) | Build a list or tuple; append to a list |

### Operators

| Form | Does |
|------|------|
| [`==`](specials/compile-eqeq.md), [`!=`](specials/compile-eq.md), [`equal?`](specials/compile-equal.md), [`not-equal?`](specials/compile-not-equal.md) | Equality, chained over any number of operands |
| [`<`](specials/compile-lt.md), [`<=`](specials/compile-lteq.md), [`>`](specials/compile-gt.md), [`>=`](specials/compile-gteq.md) | Ordering, chained |
| [`le?`](specials/compile-le.md), [`ge?`](specials/compile-ge.md) | Aliases for `<=` and `>=` |
| [`**`](specials/compile-starstar.md) | Exponentiation with an optional modulus |
| [`in`, `not-in`, `is`, `is-not`](specials/object-operators/index.md#membership-and-identity) | Membership and identity, each with a `?`-suffixed alias |
| [`not`, `truth`, `nil?`, `repr`, `len`, `type`, `dir`, `vars`, `iter`, `next`, `divmod`](specials/object-operators/index.md#unary-and-inspection-operators) | The unary and inspection operators |
| [Object operators](specials/object-operators/index.md) | The arithmetic, bitwise, and unary operators, constant folding, and the comparison table |

### Pairs and quoting

| Form | Does |
|------|------|
| [`cons`](specials/compile-cons.md), [`car`](specials/compile-car.md), [`cdr`](specials/compile-cdr.md) | Build a pair chain; its first element; its rest |
| [`bracket-cons`](specials/compile-bracket-cons.md), [`bracket-proper`](specials/compile-bracket-proper.md) | Build a bracket pair chain; a proper bracket list |
| [`quote`](specials/compile-quote.md) | A form as data; `'form` |
| [`quote-syntax`](specials/compile-quote-syntax.md) | A form as data, keeping hygiene marks; `#'form`; what a transformer's patterns and templates are quoted with |
| [`quasiquote`](specials/compile-quasiquote.md), [`unquote`](specials/compile-unquote.md), [`unquote-splicing`](specials/compile-unquote-splicing.md) | A template; `` `form ``, `,form`, `,@form` |
| [`quasiquote-syntax`](specials/compile-quasiquote-syntax.md) | A template keeping hygiene marks; `` #`form ``; what a procedural macro a template defines builds its output with |
| [Block quote](specials/compile-quote-block.md), [block quasiquote](specials/compile-quasiquote-block.md) | The reader shorthands `'head:` and `` `head: ``, which quote an indented block as its `#block:` cons; `#'head:` and `` #`head: `` do the same keeping hygiene marks |

### Async, generators, and continuations

| Form | Does |
|------|------|
| [`await`](specials/compile-await.md) | Await a held awaitable; bare `(await)` is a cooperative suspend |
| [`yield`](specials/compile-yield.md), [`yield-from`](specials/compile-yield-from.md) | Generator and coroutine suspension; delegation |
| [`with`](specials/with.md), [`dyn-with`](specials/dyn-with.md) | Context managers, lexical and dynamic-extent |
| [`dyn-wind`](specials/compile-dyn-wind.md) | Before and after thunks around a body, rerun across continuation jumps |
| [`reset`](specials/compile-reset.md), [`shift`](specials/compile-shift.md) | Delimited continuations |
| [`reset-at`](specials/compile-reset-at.md), [`shift-at`](specials/compile-shift-at.md) | Delimited continuations with a delimiter chosen by tag |
| [`call/cc`](specials/compile-call-cc.md), [`let/cc`](specials/compile-let-cc.md), [`capture/cc`](specials/compile-capture-cc.md) | Undelimited continuations, from the Scheme form down to the raw capture |

### Modules

| Form | Does |
|------|------|
| [`import`](specials/import.md) | Import a module; `[MODULE as: NAME]` binds the leaf |
| [`import-from`](specials/import-from.md) | Import names from a module, with aliases |
| [`import-all-from`](specials/import-all-from.md) | Bind a module's public names, at module level only |
| [`use`](specials/use.md) | Import a feature with all its facets, a macro together with its runtime sibling |

### Macro definition and the compiler

| Form | Does |
|------|------|
| [`syntax-rules`](specials/compile-syntax-rules.md), [`syntax-match`](specials/compile-syntax-match.md) | Template transformers, by rule list and by named spec |
| [`macro-rules`](specials/compile-macro-rules.md), [`macro-match`](specials/compile-macro-match.md) | Procedural transformers |
| [`special-rules`](specials/compile-special-rules.md), [`special-match`](specials/compile-special-match.md) | Transformers that receive the compiler and emit bytecode |
| [`bare-syntax`](specials/compile-bare-syntax.md) | The arm used when a syntax name appears as a lone atom |
| [`gensym-thunk`](specials/compile-gensym-partial.md) | The `{...}` thunk with inferred `%1`, `%2` formals |
| [`apply-fence`](specials/compile-apply-fence.md) | Expand a fenced foreign literal through its handler |
| [`bytecode`](specials/compile-bytecode.md) | Inject bytecode directly |

### Operator names

| Form | Does |
|------|------|
| [`+`, `-`, `*`, `/`, `//`, `%`, `**`, `@`, `&`, `\|`, `^`, `~`, `<<`, `>>`, `divmod`](specials/object-operators/index.md#arithmetic-and-bitwise-operators) | Arithmetic and bitwise operators |
| [`==`, `!=`, `<`, `<=`, `>`, `>=`, `equal?`, `not-equal?`, `lt?`, `le?`, `gt?`, `ge?`](specials/object-operators/index.md#comparison-operators) | Comparison operators and their aliases |
| [`in`, `not-in`, `is`, `is-not`, `in?`, `not-in?`, `is?`, `is-not?`](specials/object-operators/index.md#membership-and-identity) | Membership and identity operators and their aliases |

## Macros

### Binding and assignment

| Macro | Does |
|-------|------|
| [`set!`](macros/compile-set.md) | Store into a name, attribute, subscript, or ref, choosing the special by target shape |
| [`incr!`, `decr!`](macros/compile-incr-and-compile-decr.md) | Add to or subtract from a binding in place |
| [`del!`](macros/compile-del.md) | Delete a name, attribute, or subscript |
| [`take!`](macros/compile-take.md) | Read a name, attribute, or subscript and clear it |
| [`bind!`](macros/compile-bind.md) | Bind a name to a ref, typed by the target shape |
| [`ref`](macros/compile-ref.md) | A ref to a name, attribute, subscript, or parameter |
| [`unassigned?`, `default!`](macros/compile-default-and-compile-unassigned.md) | Whether a location holds a value; store one unless it already does |
| [`let*`](macros/compile-letstar.md) | Sequential `let`, each binding seeing the ones before |
| [`let-values`](macros/compile-let-values.md) | `let` with unpacking |
| [`labels`](macros/compile-labels.md), [`flet`](macros/compile-flet.md) | Local functions for a body, mutually recursive and not |
| [`define-parameter`](macros/compile-define-parameter-and-compile-parameterq.md) | Declare a dynamic parameter |
| [`define-environ`](macros/compile-define-environ.md) | Bind a name to an environment variable |
| [`declare-exports`](macros/compile-declare-exports.md) | Name the module's public features |
| [`entry-point`](macros/compile-entry-point.md) | Mark the function a program runs once its module has loaded |

### Conditionals

| Macro | Does |
|-------|------|
| [`if`](macros/compile-if.md) | Two-armed conditional over `cond` |
| [`when`](macros/compile-when.md), [`unless`](macros/compile-unless.md) | One-armed conditionals |
| [`?`](macros/compile-q.md) | Conditional with a boolean default |
| [`bool`](macros/compile-bool.md) | Truthiness as a boolean |
| [`None?`](macros/compile-none-and-compile-nil.md) | Identity test against the `None` singleton |
| [`has?`](macros/compile-has.md) | Membership |
| [`assert`](macros/compile-assert.md) | Raise `AssertionError` unless a test holds; removable by pragma |
| [`pass`](macros/compile-pass.md) | A no-op expression, and a bare control word |

### Definitions

| Macro | Does |
|-------|------|
| [`def`](macros/compile-def.md) | Define a function and bind it in the current scope |
| [`global`](macros/compile-global.md) | Prefix over `var`, `def`, `class` and `syntax` that binds the form at the module |
| [`class`](macros/compile-class.md) | Define a class and bind it in the current scope |
| [`super`](macros/compile-super.md) | The superclass proxy inside a method |
| [`decorate`](macros/compile-decorate.md), [`decorate-subject`](macros/compile-decorate-subject.md) | What `@` lines lower to, and the per-decorator dispatch |
| [`when-main`](macros/compile-when-main.md) | Python's `__name__` guard; prefer `entry-point` for a program's work |

### Iteration and collections

| Macro | Does |
|-------|------|
| [`each`](macros/compile-each.md) | A generator over a `[var seq]` binding, with `when:` and `unless:` filters |
| [`list-each`](macros/compile-list-each.md) | A list from the same shape |
| [`dict-each`](macros/compile-dict-each.md) | A dict from `key:` and `value:` clauses |
| [`slice`, `item-slice`](macros/compile-slice-and-compile-item-slice.md) | A slice object; a slice of a sequence |
| [`item*`, `attr*`](macros/compile-itemstar-and-compile-attrstar.md) | Nested item and attribute chains |
| [`cadr` ... `caddddr`, `first` ... `fifth`](macros/compile-cadr-caddr-cadddr-caddddr.md) | Positions in a pair chain |
| [`function-compose`](macros/compile-function-compose.md) | Compose functions right to left |

### Macros about macros

| Macro | Does |
|-------|------|
| [`syntax`](macros/compile-syntax.md) | A transformer bound at the module from top level, or for the rest of a body inside one |
| [`define-syntax`](macros/compile-define-syntax.md) | The Scheme-layer spelling: a transformer bound at the module, with any of the rule and match kinds or a bare form; its template names are kept apart from the use site, see [Hygiene](../guide/hygiene.md) |
| [`extend-syntax`](macros/compile-extend-syntax.md) | Add arms to an existing transformer |
| [`let-syntax`](macros/compile-let-syntax.md) | A transformer scoped to a body |
| [`inline-macro`](macros/compile-inline-macro.md) | Run a body at expand time and splice its result |
| [`datum`](macros/compile-datum.md) | Capture a form with its source position for hygiene |
| [`datumq`](macros/compile-datumq.md) | The same without evaluating the argument; in a template, writes a form raw (`-'form`) |
| [`define-fence`](macros/compile-define-fence.md) | Define a fence handler for a triple-backtick literal |
| [`macroexpand`, `macroexpand-1`](macros/compile-macroexpand.md) | Expand a form fully, or by one step |
| [`macroexpandq`, `macroexpandq-1`](macros/compile-macroexpandq.md) | The same without quoting the form |
| [`compileq`](macros/compile-compileq.md), [`evaluate`](macros/compile-evaluate.md), [`get-compiler`](macros/compile-get-compiler.md) | Compile, evaluate, and reach the compiler from a program |
| [`pragma`](macros/compile-pragma.md) | Set or read a module's reader and compiler settings |
| [`help`](macros/compile-help.md) | Documentation for a name |

## Fences

| Name | Does |
|------|------|
| [`fence/bash`](fences/fence-bash.md) | A bash script as a literal, run through `run-process` |

## Runtime

### Types and type predicates

Each type is bound with its `NAME?` predicate: `int` and `int?`,
`list` and `list?`, and so on.

| Name | Does |
|------|------|
| [`bool`, `int`, `float`, `complex`, `decimal`, `fraction`](runtime/types-and-predicates.md#numbers) | Numbers |
| [`str`, `bytes`, `bytearray`, `memoryview`](runtime/types-and-predicates.md#text-and-bytes) | Text and bytes |
| [`list`, `tuple`, `dict`, `set`, `frozenset`, `range`, `slice`](runtime/types-and-predicates.md#collections) | Collections |
| [`object`, `type`, `property`, `classmethod`, `staticmethod`](runtime/types-and-predicates.md#objects-and-classes) | Objects and classes |
| [`pair`, `symbol`, `keyword`, `datum`](runtime/types-and-predicates.md#pairs-and-symbols) | Pairs and symbols |
| [`partial`, `demand`, `constantly`, `values`, `defer`](runtime/types-and-predicates.md#functions-and-results) | Functions and results |
| [`map`, `filter`, `zip`, `py-map`, `py-filter`, `py-zip`](runtime/types-and-predicates.md#iterators) | Iterators |
| [`Ref`, `ItemRef`, `AttributeRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`, `ParamRef`, `ref?`](runtime/types-and-predicates.md#refs) | Refs |
| [`ProcessResult`](runtime/types-and-predicates.md#processes) | The result of `run-process` |

### Operators

| Name | Does |
|------|------|
| [`+`, `-`, `*`, `/`, `//`, `**`, `@`, `&`, `\|`, `^`, `<<`, `>>`](runtime/operators.md#arithmetic-and-bitwise) | Arithmetic and bitwise |
| [`==`, `!=`, `<`, `<=`, `>`, `>=`, `equal?`, `not-equal?`, `lt?`, `le?`, `gt?`, `ge?`](runtime/operators.md#comparison) | Comparison, chained |
| [`in`, `not-in`, `in?`, `not-in?`, `is?`, `is-not?`](runtime/operators.md#membership-and-identity) | Membership and identity |

### Functions

| Name | Does |
|------|------|
| [`instance?`, `subclass?`, `callable?`, `None?`, `nil?`, `proper?`, `recursive?`, `all?`, `any?`, `hasattr?`, `on-demand?`](runtime/functions.md#predicates) | Predicates |
| [`cons`, `build-proper`, `car`, `cdr`, `cadr`, `caddr`, `cadddr`, `caddddr`, `cddr`, `cdddr`, `cddddr`, `first`, `second`, `third`, `fourth`, `fifth`, `set-car`, `set-cdr`, `unpack`, `bracket-cons`, `bracket-splice`](runtime/functions.md#pairs) | Pairs |
| [`#list`, `#tuple`, `#set`, `#dict`, `#str`, `#list-append`](runtime/functions.md#collection-builders) | Collection builders |
| [`apply`, `identity`, `constantly`, `partial`, `demand`, `demand-call`, `demand-apply`, `values`, `decorate-wraps`](runtime/functions.md#calling) | Calling |
| [`defer`, `create-task`, `coroutine-fn`, `run-process`](runtime/functions.md#async) | Async |
| [`iter`, `next`, `sorted`, `max`, `min`, `len`, `sum`, `enumerate`, `reversed`, `map`, `filter`, `zip`](runtime/functions.md#iteration) | Iteration |
| [`repr`, `py-repr`, `format`, `abs`, `round`, `bin`, `hex`, `oct`, `chr`, `ord`, `ascii`, `hash`, `id`](runtime/functions.md#conversion-and-display) | Conversion and display |
| [`print`, `input`, `open`](runtime/functions.md#input-and-output) | Input and output |
| [`getattr`, `setattr`, `vars`, `dir`, `itemgetter`, `attrgetter`, `super`, `getattr*`, `hasattr*`, `getitem*`, `hasitem*`, `build-slice`, `koskari-vars`](runtime/functions.md#attributes-and-items) | Attributes and items |
| [`deref`, `parameter`, `environ-ref`](runtime/functions.md#refs-parameters-and-the-environment) | Refs, parameters, and the environment |
| [`compile`, `evaluate`, `get-compiler`, `macroexpand`, `macroexpand-1`, `apply-layout`, `apply-layouts`, `help`, `gensym`, `active-pragma`, `help-pragma-key`, `build-syntax`, `build-macro`, `build-special`, `build-bare-syntax`](runtime/functions.md#compilation-and-expansion) | Compilation and expansion |
| [`None`, `True`, `False`, `Ellipsis`, `...`, `NotImplemented`, `nil`, `bracket-nil`](runtime/functions.md#constants) | Constants |

### Exceptions

| Name | Does |
|------|------|
| [`KoskariException`, `NotYetImplemented`, `KoskariSyntaxError`, `ReaderError`, `ReaderSyntaxError`, `ReaderEOF`, `FormatStringSyntaxError`, `LayoutError`, `CompilerError`, `SpecialError`, `SyntaxMatchError`, `FenceError`, `ParametersError`, `UncaughtCompilerError`, `KoskariContinuationError`, `KoskariPromptNotFound`, `KoskariPromptUnreachable`, `KoskariSyncViolation`](error-codes.md) | The Koskari exceptions; every Python exception type is bound as well |

## Names with more than one meaning

| Name | Meanings |
|------|----------|
| `bare-syntax` | [layout handler](layouts/layout-bare-syntax.md), [special form](specials/compile-bare-syntax.md) |
| `bool` | [macro](macros/compile-bool.md), [type](runtime/types-and-predicates.md#numbers) |
| `bracket-cons` | [function](runtime/functions.md#pairs), [special form](specials/compile-bracket-cons.md) |
| `build-class` | [layout handler](layouts/layout-build-class.md), [special form](specials/compile-build-class.md) |
| `caddddr` | [function](runtime/functions.md#pairs), [macro](macros/compile-cadr-caddr-cadddr-caddddr.md) |
| `cadr` | [function](runtime/functions.md#pairs), [macro](macros/compile-cadr-caddr-cadddr-caddddr.md) |
| `car` | [function](runtime/functions.md#pairs), [special form](specials/compile-car.md) |
| `cdr` | [function](runtime/functions.md#pairs), [special form](specials/compile-cdr.md) |
| `class` | [layout handler](layouts/layout-class.md), [macro](macros/compile-class.md) |
| `cons` | [function](runtime/functions.md#pairs), [special form](specials/compile-cons.md) |
| `constantly` | [function](runtime/functions.md#calling), [type](runtime/types-and-predicates.md#functions-and-results) |
| `datum` | [macro](macros/compile-datum.md), [type](runtime/types-and-predicates.md#pairs-and-symbols) |
| `defer` | [function](runtime/functions.md#async), [type](runtime/types-and-predicates.md#functions-and-results) |
| `demand` | [function](runtime/functions.md#calling), [type](runtime/types-and-predicates.md#functions-and-results) |
| `deref` | [function](runtime/functions.md#refs-parameters-and-the-environment), [special form](specials/compile-deref.md) |
| `dir` | [function](runtime/functions.md#attributes-and-items), [special form](specials/object-operators/index.md#unary-and-inspection-operators) |
| `dyn-wind` | [layout handler](layouts/layout-dyn-wind.md), [special form](specials/compile-dyn-wind.md) |
| `equal?` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-equal.md) |
| `evaluate` | [function](runtime/functions.md#compilation-and-expansion), [macro](macros/compile-evaluate.md) |
| `fifth` | [function](runtime/functions.md#pairs), [macro](macros/compile-cadr-caddr-cadddr-caddddr.md) |
| `filter` | [function](runtime/functions.md#iteration), [type](runtime/types-and-predicates.md#iterators) |
| `first` | [function](runtime/functions.md#pairs), [macro](macros/compile-cadr-caddr-cadddr-caddddr.md) |
| `for-each` | [layout handler](layouts/layout-for-each.md), [special form](specials/compile-for-each.md) |
| `format` | [function](runtime/functions.md#conversion-and-display), [special form](specials/compile-format.md) |
| `ge?` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-ge.md) |
| `get-compiler` | [function](runtime/functions.md#compilation-and-expansion), [macro](macros/compile-get-compiler.md) |
| `global` | [layout handler](layouts/layout-global.md), [macro](macros/compile-global.md) |
| `gt?` | [operator function](runtime/operators.md#comparison), [special form](specials/object-operators/index.md#comparison-operators) |
| `help` | [function](runtime/functions.md#compilation-and-expansion), [macro](macros/compile-help.md) |
| `if` | [layout handler](layouts/layout-if.md), [macro](macros/compile-if.md) |
| `in?` | [operator function](runtime/operators.md#membership-and-identity), [special form](specials/object-operators/index.md#membership-and-identity) |
| `in` | [operator function](runtime/operators.md#membership-and-identity), [special form](specials/object-operators/index.md#membership-and-identity) |
| `instance?` | [function](runtime/functions.md#predicates), [special form](specials/compile-instance.md) |
| `is-not?` | [operator function](runtime/operators.md#membership-and-identity), [special form](specials/object-operators/index.md#membership-and-identity) |
| `is?` | [operator function](runtime/operators.md#membership-and-identity), [special form](specials/object-operators/index.md#membership-and-identity) |
| `iter` | [function](runtime/functions.md#iteration), [special form](specials/object-operators/index.md#unary-and-inspection-operators) |
| `len` | [function](runtime/functions.md#iteration), [special form](specials/object-operators/index.md#unary-and-inspection-operators) |
| `le?` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-le.md) |
| `#list-append` | [function](runtime/functions.md#collection-builders), [special form](specials/object-operators/index.md#collection-builders) |
| `#list` | [function](runtime/functions.md#collection-builders), [special form](specials/object-operators/index.md#collection-builders) |
| `lt?` | [operator function](runtime/operators.md#comparison), [special form](specials/object-operators/index.md#comparison-operators) |
| `macroexpand-1` | [function](runtime/functions.md#compilation-and-expansion), [macro](macros/compile-macroexpand.md) |
| `macroexpand` | [function](runtime/functions.md#compilation-and-expansion), [macro](macros/compile-macroexpand.md) |
| `macro-match` | [layout handler](layouts/layout-macro-match.md), [special form](specials/compile-macro-match.md) |
| `macro-rules` | [layout handler](layouts/layout-macro-rules.md), [special form](specials/compile-macro-rules.md) |
| `map` | [function](runtime/functions.md#iteration), [type](runtime/types-and-predicates.md#iterators) |
| `match` | [layout handler](layouts/layout-match.md), [special form](specials/compile-match.md) |
| `next` | [function](runtime/functions.md#iteration), [special form](specials/object-operators/index.md#unary-and-inspection-operators) |
| `nil?` | [function](runtime/functions.md#predicates), [special form](specials/object-operators/index.md#unary-and-inspection-operators) |
| `None?` | [function](runtime/functions.md#predicates), [macro](macros/compile-none-and-compile-nil.md) |
| `not-equal?` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-not-equal.md) |
| `not-in?` | [operator function](runtime/operators.md#membership-and-identity), [special form](specials/object-operators/index.md#membership-and-identity) |
| `not-in` | [operator function](runtime/operators.md#membership-and-identity), [special form](specials/object-operators/index.md#membership-and-identity) |
| `**` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/compile-starstar.md) |
| `&` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `*` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `+` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `-` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `//` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `/` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `<<` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `>>` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `@` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `\|` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `^` | [operator function](runtime/operators.md#arithmetic-and-bitwise), [special form](specials/object-operators/index.md#arithmetic-and-bitwise-operators) |
| `==` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-eqeq.md) |
| `!=` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-eq.md) |
| `>=` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-gteq.md) |
| `>` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-gt.md) |
| `<=` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-lteq.md) |
| `<` | [operator function](runtime/operators.md#comparison), [special form](specials/compile-lt.md) |
| `parameter` | [function](runtime/functions.md#refs-parameters-and-the-environment), [special form](specials/compile-parameter.md) |
| `partial` | [function](runtime/functions.md#calling), [type](runtime/types-and-predicates.md#functions-and-results) |
| `repr` | [function](runtime/functions.md#conversion-and-display), [special form](specials/object-operators/index.md#unary-and-inspection-operators) |
| `slice` | [macro](macros/compile-slice-and-compile-item-slice.md), [type](runtime/types-and-predicates.md#collections) |
| `special-match` | [layout handler](layouts/layout-special-match.md), [special form](specials/compile-special-match.md) |
| `special-rules` | [layout handler](layouts/layout-special-rules.md), [special form](specials/compile-special-rules.md) |
| `#str` | [function](runtime/functions.md#collection-builders), [special form](specials/compile-str.md) |
| `subclass?` | [function](runtime/functions.md#predicates), [special form](specials/compile-subclass.md) |
| `super` | [function](runtime/functions.md#attributes-and-items), [macro](macros/compile-super.md) |
| `switch` | [layout handler](layouts/layout-switch.md), [special form](specials/compile-switch.md) |
| `syntax-match` | [layout handler](layouts/layout-syntax-match.md), [special form](specials/compile-syntax-match.md) |
| `syntax-rules` | [layout handler](layouts/layout-syntax-rules.md), [special form](specials/compile-syntax-rules.md) |
| `try` | [layout handler](layouts/layout-try.md), [special form](specials/compile-try.md) |
| `#tuple` | [function](runtime/functions.md#collection-builders), [special form](specials/object-operators/index.md#collection-builders) |
| `type` | [special form](specials/object-operators/index.md#unary-and-inspection-operators), [type](runtime/types-and-predicates.md#objects-and-classes) |
| `values` | [function](runtime/functions.md#calling), [type](runtime/types-and-predicates.md#functions-and-results) |
| `vars` | [function](runtime/functions.md#attributes-and-items), [special form](specials/compile-vars.md) |
| `while` | [layout handler](layouts/layout-while.md), [special form](specials/compile-while.md) |
| `zip` | [function](runtime/functions.md#iteration), [type](runtime/types-and-predicates.md#iterators) |
