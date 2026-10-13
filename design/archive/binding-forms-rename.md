# Binding forms and builder forms

## Status

Implemented 2026-10-12, as revised that day over the 2026-09-17
settlement. The work bumped the package from 0.1.0 to 0.2.0-dev: the
`class` rename and the `define` reshape break existing source.

## Summary

Today the surface mixes the two roles:

| form | role | binds |
|---|---|---|
| `define NAME expr` | value binding | yes |
| `define NAME [args]: body` | function definition, by header shape | yes |
| `define-global NAME ...` | both of the above at module scope | yes |
| `define-values PATTERN expr` | unpacking binding | yes |
| `define-class NAME [supers]: body` | class definition and `NAME?` predicate | yes |
| `local NAME ...` | wrapper over `define` | yes |
| `global NAME ...` | wrapper over `define-global` | yes |
| `lambda [NAME] [args] body` | function builder, self-binding | no |
| `function [NAME] [args] body` | function builder, closing over its name | no |
| `class NAME supers body` | class builder | no |

`class` carries a name, its layout reads as a definition, and it binds
nothing: a bare `class Box []:` block at module level leaves `Box`
unbound. `define` is one word for a value binding and a function
definition, told apart by whether formals and a body follow the name.

The final surface has two layers over the same machinery. Either may
be used in any module, and they mix freely.

| Koskari layer | Scheme layer | builders (no binding) |
|---|---|---|
| `var NAME expr`, `var NAME :: T expr`, `var NAME` | `define NAME expr` | `lambda`, `function` |
| `var [a b *: c] expr` | `define-values [a b] expr` | |
| `def NAME [args]: body`, `def NAME :: T [args]: body` | `define [NAME args...] body` | |
| `class NAME [supers]: body` | | `build-class NAME supers body` |
| `global ...` (prefix, below) | `define-global` (as `define`, at the module) | |
| `syntax NAME: ...` | `define-syntax NAME ...` | |

`var` binds in whatever namespace is live, module level included.
`def` and `class` bind the way `var` does. Neither creates anything
beyond the one name: `class` makes no `NAME?` predicate. A predicate
may return later as a syntax decorator.

    import-from dataclasses dataclass

    var DEFAULT_PORT 8080

    @dataclass
    class Endpoint []:
      var host :: str "localhost"
      var port :: int DEFAULT_PORT

      def url [self]:
        f"http://{self.host}:{self.port}"

    def parse-endpoint [text]:
      var [host port] (text.split ":")
      Endpoint host (int port)

The same module in the Scheme layer:

    (define default-port 8080)

    (define [parse-endpoint text]
      (define-values [host port] (text.split ":"))
      (Endpoint host (int port)))

Retired: `local` (to `var`), `define-class` (to `class`),
`define-dataclass` (to `@dataclass` over `class`), `define-demanding`
(to `@demand` over `def`) and `define-coroutine` (to a decorator over
`def`). None has a Scheme counterpart, so the Scheme layer loses
nothing.

## Why these words

A survey of the lisps with a following:

| lisp | value binding | function definition | class | builder |
|---|---|---|---|---|
| Hy | `setv` | `defn` | `defclass` | `fn` |
| Basilisp, Clojure | `def` | `defn` | `deftype`, `defrecord` | `fn` |
| Hissp | `define` | `define` + `lambda` | `deftype` macro | `lambda` |
| Janet | `def`, `var` | `defn` | none | `fn` |
| Fennel | `local`, `var`, `global` | `fn` | none | `fn`, `lambda` |
| Scheme, Racket | `define` | `define` | `class` builds a value in Racket | `lambda` |
| Common Lisp, Elisp | `defvar`, `setq` | `defun` | `defclass` | `lambda` |

`define` is Scheme's and Hissp's; it stays, reshaped to Scheme's
form, as the layer for readers who want it. `class` as a definition
has Python and every `defclass` lisp behind it; Racket, where it
builds a value, is the corner the language is in today.

`def` over `defn`: Koskari is tied to Python, and `def` beside `class`
gives two Python keywords with one shape -- a name, a bracket list, a
colon body -- under which `@decorator` reads as it does in Python.
`def` is a value binding in the Clojure family; a reader with that
habit who writes `def x 1` gets a match error, since `def` takes only
a name, formals and a body. `def` beside a `defn` would confuse both
camps.

`var` over `local`: `var` is a mutable local in Janet and Fennel and
the thing `def` creates in Clojure, so it reads as "a binding" in
every lisp that has it, and it pairs with `vars`. In Janet and Fennel
`var` marks mutability against an immutable `def` or `local`; here it
marks a value binding against a function binding, and no immutable
form stands beside it. `local` has Fennel alone behind it, and four
`.ki` lines use it.

`global` reads the same in Python and in Fennel. Hy declares with
`(global x)` and then assigns with `setv`; no surveyed lisp retargets
a binding with a decorator.

## `var` over a pattern

`(var [a b c] x)` declares `a`, `b` and `c`, then unpacks `x` into
them. It is a macro over the declare-only `var` and `setq-values`:

    (var [a [b c] *: d] x)
    ->
    (begin (var a) (var b) (var c) (var d) (setq-values [a [b c] *: d] x))

The macro walks the pattern with `macro-match`, recursing into nested
pairs and skipping `*:` and `**:`, as `unpack_lhs_bind_symbols` does.
A declaration covers its whole scope rather than the lines after it,
so declaring before the value is evaluated changes nothing; and inside
a function body the expansion compiles to the same instructions as
`define-values` today (`BUILD_LIST_CONST`, `UNPACK_FAST`). The
expansion binds the same way at module top level and in a class body.

`define-values` is the Scheme spelling. Its special unpacks through the
same helper and compiles to the same instructions.

`set!` keeps `(set! (values ...) v)`. A bracket arm on `set!` would
have to treat any pair whose head is not `item`, `attr` or `deref` as
a pattern, so the first name of a pattern could never be one of those
literals.

`(var [a b c])` with no value declares all three, as `(var a)` does.

## `global` as a prefix

`global` augments the binder that follows it, moving its binding to
the module:

    global N                        ; N bound at the module, no value
    global N 100                    ; and assigned 100

    global def foo []:              ; foo, a function, at the module
      ...

    global class Bar []:            ; Bar, a class, at the module
      ...

    global syntax do-foo:           ; compile/do-foo at the module
      syntax-rules []:
        ...

    global layout do-foo:           ; layout/do-foo at the module (future)
      ...

Each binder is an explicit `global` arm with the binder word as a
literal:

    (global N)                      -> (compile/global N)
    (global N v)                    -> (compile/global N v)
    (global [a b] x)                -> (begin (compile/global a) (compile/global b)
                                              (setq-values [a b] x))
    (global def f formals body...)  -> (compile/global f (function f formals body...))
    (global class C supers body...) -> (compile/global C (build-class C supers body...))
    (global syntax name rules)      -> today's define-syntax lowering
    (global layout name ...)        -> future

A general `global` that expands the inner form until it reaches `var`
and swaps in `compile/global` was rejected: a binder that expands into
helper `var`s would make the helpers global too. User binders join
with `extend-syntax global`, as user forms join `decorate`.

`def`, `class`, `syntax` and `layout` become literals after `global`.
Syntax outranks bindings, so no variable could carry those names
anyway. Inside a function body, `global def foo` makes `foo` global
for that scope, as Python's `global foo` followed by `def foo` does.

`@cache` above `global def foo []:` reads as
`(decorate [cache] (global def foo ...))`; `decorate` gains an arm
that lowers it to `(global foo (decorate [cache] (function foo ...)))`,
and the same for `global class`.

`define-global` stays as the Scheme layer's form: `define` with the
binding forced to the module, `(define-global [f x] body...)` and
`(define-global x v)`.

## `syntax` and `layout` scope

`define-syntax` binds globally today: every arm lowers to
`(define-global compile/NAME ...)` (`koskari/bootstrap/_define_syntax.ki`).
The one body-scoped syntax is `let-syntax`, which installs a compiler
overlay for the extent of its body rather than a namespace binding.

The compiler resolves a syntax while compiling, and a local `var` has
its value only when the code runs, so `syntax foo` inside a function
body cannot lower to `var`: it lowers to an overlay covering the rest
of the body, as `let-syntax` does. At module top level local and
global are the same namespace, and `syntax foo` and `global syntax
foo` coincide.

| local | global |
|---|---|
| `var` -> `compile/var` | `global` -> `compile/global` |
| `def` -> `var` + `function` | `global def` -> `compile/global` + `function` |
| `class` -> `var` + `build-class` | `global class` -> `compile/global` + `build-class` |
| `syntax` -> overlay in a body, `compile/global` at top level | `global syntax` -> `compile/global compile/NAME` |
| `layout` -> top level only | `global layout` -> `layout/NAME` |

The module loader reads, compiles and runs one top-level form before
reading the next (`koskari/module.py`), and the reader resolves
`layout/NAME` against the module namespace. A layout bound at top
level applies to the top-level forms after it. A `layout` inside a
body binds after its whole top-level form has been read, so it can
never apply; `layout` there is an error, and at top level `layout`
and `global layout` coincide.

## `define` reshaped

    (define [NAME formal ...] body ...)   ; function
    (define NAME value)                   ; value
    (define NAME)                         ; declaration

A pair in the name position means a function; a symbol means a value.
The shorthand `(define NAME [formals] body...)` and its typed variants
leave `define`. Only `define` and `define-global` give a pair in the
name position this meaning; `var` and `global` read it as an unpacking
pattern, so `(define [a b] x)` is a function returning `x`. A
wrong-usage corpus record covers that.

An unmigrated `(define f [x] body...)` has too many arguments for the
value arm and fails to match. `(define f [x])`, with no body, is a
value binding before and after. No form silently changes meaning.

## Specials underneath

The declare-and-store work lives in `compile/var` and
`compile/global` (`koskari/compile/specials/variables.py`): the
value, declaration and `::` / `type:` arms, with no function
shorthand. `def` and `class` lower to `var`.

`compile/define` and `compile/define-global` stay specials, with the
Scheme patterns: a pair in the name position compiles
`(function NAME FORMALS BODY...)` and stores it as `var` or `global`
would, and a symbol takes the `var` or `global` arms. As specials
rather than bootstrap macros they compile without the bootstrap, in
the specials test harness and in `_define_syntax.ki` before any macro
exists. `compile/define-values` keeps its implementation; it is the
Scheme spelling of `var` over a pattern and compiles to the same
instructions.

Generated forms that name `define` today
(`koskari/compile/specials/exceptions.py`, the module docstring's
`define __doc__` in `koskari/module.py`, and the macro templates in
the bootstrap) move to `var` before the reshape, so the reshape
changes no generated form.

## `vars` and `koskari-vars`

`vars` is already a special, `compile/vars`, unary, emitting
OBJECT_VARS. It gains a zero-arity pattern that emits LOCALS, so
`(vars)` is the live namespace and `(vars obj)` the object's
attribute dict, decided at compile time. `locals` folds into it; three
lines in `koskari/bootstrap/basics.ki` use it.

In value position `vars` resolves to Python's builtin (bound in
`koskari/bootstrap/__init__.py`), whose zero-argument call reads the
topmost CPython frame. `koskari-vars` is the runtime replacement, a
KosCallable singleton (`koskari/lib/domain.c`). With an object it
returns the object's `__dict__`. Called from Koskari with no argument,
its install runs while the caller's frame is still current, and
returns that evaluation's namespace through
`KosEvalState_RevealNamespace`, as the LOCALS handler does. A plain
Python callable in tail position would run after the caller's frame
was dropped and see the caller's caller. Called from Python, it reads
the evaluation `kos_tls_active_eval_for_ctx` finds for
`kos_ctx_current`.

## Namespace repr

`(globals)` and `(vars)` return namespace objects. `ns_repr` in
`koskari/lib/namespace.c` serves the function, collector and module
namespace types with one line, `<Namespace f1 in __main__: a=1
scale=2>`: the type, the name, the parent by name only, and
`name=value` for each of the scope's own names. An unbound name shows
as `<unbound>` rather than raising, a Ref slot shows its value rather
than the cell, and a cycle back to the namespace shows as
`<Namespace ...>`.

## Where the names live

- Specials: `compile/define` and `compile/define-global` share one
  pattern set covering the value, declaration and function shapes,
  and `compile/define-values` and `compile/setq-values` share the
  unpack patterns (`koskari/compile/specials/variables.py`);
  `compile/lambda`, `compile/function` (`lambda_.py`); `compile/class`
  (`class_.py`); `compile/vars` (`object.py`); `compile/locals`,
  `compile/globals` (`variables.py`).
- Layouts: `layout/class`, `layout/define-class` and
  `layout/define-dataclass` (`koskari/reader/layouts/classes.py`).
  `define` has no layout of its own; it reads through the default
  block layout.
- Macros: `local` and `global` in `_variables.ki`; `define-class`,
  `define-dataclass` and the instance check in `_class.ki`;
  `define-coroutine` and `define-demanding` in `_functions.ki`;
  `define-syntax` and `extend-syntax` in `_define_syntax.ki`;
  `decorate` in `_decorator.ki`, whose literal set names
  `define-class`, `define`, `define-global`, `define-demanding`,
  `define-coroutine` and `define-dataclass`.
- Uses, as counted on 2026-10-12, in `koskari/`, `tools/`, `tests/`,
  `docs/` outside the corpus, and corpus records:

  | form | koskari | tools | tests | docs | corpus |
  |---|---|---|---|---|---|
  | `define-class` | 11 | 4 | 94 | 64 | 164 |
  | `define-global` | 40 | 1 | 53 | 29 | 34 |
  | `define-values` | 16 | 17 | 12 | 24 | 40 |
  | `define-dataclass` | 10 | 2 | 12 | 28 | 0 |
  | `define-demanding` | 6 | 1 | 4 | 25 | 0 |
  | `define-coroutine` | 4 | 2 | 2 | 13 | 0 |

  `define` itself appears about 2,190 times across the `.ki` sources
  of `koskari/`, `tools/` and `examples/`. The `NAME?` predicates
  `define-class` generates are used about ten times (`Point?`,
  `Child?`, `Counter?`).

## Migration

The steps before the `class` rename and the `define` reshape only add
forms. Those two break source, each with a match error rather than a
silent change of meaning.

1. **New forms.** `compile/var` and `compile/global`; the `var` macro
   with its pattern arm; `def`; `build-class` registered on the
   `compile/class` handler; `global` as the prefix form. `decorate`
   gains `var`, `def`, `build-class`, `global def` and `global class`
   arms. `local` retires.
2. **Migrate to `var` and `def`.** A rewrite tool in `tools/`, written
   in Koskari, classifies each `define` by the arm it matches and
   edits the source text at that position, keeping comments and
   formatting: `define x [a b]` with nothing after it is a value, and
   `define f [x]:` with a body is a function. Generated `define` forms
   move by hand.
3. **`class`.** Every use of the builder `class` moves to
   `build-class`; then `class` and `layout/class` take over
   `define-class`, without the predicate. The predicate's users move
   to `instance?`. `define-class`, `layout/define-class`,
   `define-dataclass` and `layout/define-dataclass` retire.
4. **`vars`.** The zero-arity pattern, `koskari-vars`, and the repr.
5. **Decorators for the remaining definers.** `define-demanding` and
   `define-coroutine` retire in favour of `@demand` and a coroutine
   decorator over `def`. `syntax` lands, with the overlay arm for
   function bodies.
6. **`define` reshaped.** The `compile/define` and
   `compile/define-global` specials take the Scheme patterns, a pair
   in the name position for a function and the `var` or `global` arms
   for a symbol. `define-values` is unchanged.
7. **Version.** 0.2.0-dev.

## Obligations

- The corpus: every record for `define-class`, `define-global`,
  `define-values`, the function shorthand of `define` and the `class`
  builder is re-verified under the new spelling, and new records cover
  `def`, `var` (with patterns), `global` in each prefix shape,
  `class`, `build-class`, `syntax` and `(vars)`, valid and wrong-usage
  both. Wrong-usage records cover `def x 1` and `(define [a b] x)`.
- The docs: the specials pages under `docs/reference/specials/`, the
  layout pages under `docs/reference/layouts/`, the beginner track,
  the glossary, and the agent pages the corpus server returns, which
  teach `define` today; the `compile/class` docstring, which says the
  special binds the name and never did.
- Editor tooling: the lexer's keyword sets and the pygments and LSP
  extras name the forms.
- [dataclass-builder.md](dataclass-builder.md) targets
  `define-dataclass`; its target becomes `@dataclass` over `class`.
- Tests for the repr: unbound slot, Ref slot, dict-bearing namespace,
  parent by name, and a closure cycle that must not recurse.
