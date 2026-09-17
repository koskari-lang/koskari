# Binding forms and builder forms

## Status

Proposal -- not implemented. A naming split between the forms that
bind a name and the forms that build a value, a zero-arity `vars`, a
namespace repr, and a migration that keeps every gate green at each
step. Settled 2026-09-17 after the survey below.

## Summary

Today the surface mixes the two roles:

| form | role | binds |
|---|---|---|
| `define NAME expr` | value binding | yes |
| `define NAME [args]: body` | function definition, by layout shape | yes |
| `define-global NAME ...` | both of the above at module scope | yes |
| `define-class NAME [supers]: body` | class definition (macro over `class`) | yes |
| `lambda [NAME] [args] body` | function builder, self-binding | no |
| `function [NAME] [args] body` | function builder, closing over its name | no |
| `class NAME supers body` | class builder | no |

`class` is the odd one: it carries a name in the form, its layout reads
as a definition, and it binds nothing. A bare `class Box []:` block at
module level leaves `Box` unbound, and inside a function the read
resolves as a closure sym to an empty module cell. `define` carries the
other wrinkle: one word for a value binding and a function definition,
told apart by the shape of the layout header.

The set:

| form | role | today |
|---|---|---|
| `define NAME ...` | do-everything by form, for purists | unchanged |
| `def NAME [args]: body` | function definition | the function shape of `define` |
| `var NAME expr` | value binding in the scope it appears in, module level included | the value shape of `define` |
| `global NAME ...` | binding at the module, from anywhere | `define-global` |
| `class NAME [supers]: body` | class definition | `define-class` |
| `build-class NAME supers body` | class builder | `class` |
| `build-function [NAME] [args] body` | function builder, closing over its name | `function` |
| `lambda [NAME] [args] body` | function builder, self-binding | unchanged |

`var` and `global` carry no scope restriction: `var` binds in whatever
namespace is live, and `global` binds at the module from any depth.
`def` and `class` bind the way `var` does.

    def f1 [a]:
      class Box []:
        var scale a
      Box

    print (f1 1).scale

## The two function builders

`lambda` and `build-function` differ in how the function resolves its
own name. A named `lambda` resolves its name to itself: recursion
inside the body reaches the function object the form built, whatever
the name is later bound to. `build-function` closes over the name: it
expects to be bound, and a reference to the name inside the body
reaches whatever the name holds when the reference runs, which is the
decorated wrapper when a decorator was applied. It is the builder the
declarative forms and the decorator macro reach for, and it is not
meant for standalone use.

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

`define` is Scheme's and Hissp's and nobody else's; it stays as the
one-word form for readers who want it. `class` as a definition has
Python and every `defclass` lisp behind it; Racket, where it builds a
value, is the corner the language is in today. `global` reads the
same in Python and in Fennel.

`def` is the contested token: a function in Python, a value in the
Clojure family. It takes the Python meaning here, and with no `defn`
beside it and `var` carrying the value role, each word has one
meaning inside the language; a Clojure reader pauses once, and `var`
tells them the convention. `def` beside a `defn` would be the one
arrangement that confuses both camps.

`var` over `local`: `var` is a mutable local in Janet and Fennel and
the thing `def` creates in Clojure, so it reads as "a binding" in
every lisp that has it, and it pairs with `vars` the way `local`
would pair with `locals`. `local` has Fennel alone behind it, where
it exists because Lua scopes at the statement level.

## `vars`

`vars` is already a special, `compile/vars`, unary, emitting
OBJECT_VARS. It gains a zero-arity pattern that emits LOCALS, so
`(vars)` is the live namespace and `(vars obj)` the object's
attribute dict, decided at compile time. There is no runtime `vars`
that knows a Koskari frame, on purpose: `vars`, `locals` and
`globals` inspect the scope they appear in, a scope is a compile-time
fact, and none of the three is meaningful as a first-class value. In
value position `vars` resolves to Python's builtin, which is right
for objects; a zero-argument call of that builtin from Koskari sees
the driving Python frame, as `(apply locals #())` does today, and no
runtime copy could answer it better than the special already does.

## Namespace repr

`(globals)`, `(locals)` and `(vars)` return namespace objects whose
repr is the default one, which is why they are unhelpful at the CLI.
Every namespace type object carries `.tp_repr = ns_repr` commented
out beside the live `nspec_repr`. The repr shows the type, the name,
and `member: value` pairs read off the fast names and slots and the
dict when one exists: an unbound slot shows as unbound rather than
raising, a Ref slot shows its value rather than the cell, and the
parent shows by name only, since walking it recurses through the
module and through closure cycles.

## Where the names live

- Specials: `compile/define` and `compile/define-global` share one
  pattern set covering the value, declaration and function shapes
  (`koskari/compile/specials/variables.py`); `compile/lambda`,
  `compile/function` (`lambda_.py`); `compile/class` (`class_.py`);
  `compile/vars`, `compile/locals`, `compile/globals`.
- Layouts: `layout/define` picks value or function by header shape;
  `layout/class` lowers to the `class` builder; `layout/define-class`
  lowers to `define-class` (`koskari/reader/layouts/`).
- Macros: `define-class` in `_class.ki` expands to `define` over
  `class` plus the instance check; `decorate` in `_decorator.ki`
  matches `define-class` and `define` and rebuilds through `class` and
  `function`; `define-async` and `define-demand` in `_functions.ki`
  build through `function`.
- Uses, as counted on 2026-09-17: `define-class` in 18 bootstrap and
  tool lines, 55 test lines, 164 corpus records and 221 doc lines;
  `define-global` in 38, 25, 29 and 51; the `class` builder idiom
  `(define C (class C () ...))` throughout the class tests.

## Migration

Each step leaves the suite, the corpus and the docs build green.

1. **Builders under their new names.** Register `build-class` as the
   `compile/class` special and `build-function` as `compile/function`,
   keeping the old names as registrations of the same handlers. Point
   the `define-class` macro, the decorator macro, `define-async`,
   `define-demand` and `layout/class` at the new names. Nothing
   user-visible moves.
2. **`class` becomes the definition.** The `class` special and
   `layout/class` take over `define-class`: name, supers, body, the
   binding and the instance check. The three corpus records for the
   bare `class` block and the class tests that use the builder idiom
   move to `build-class`. `define-class` stays as an alias for now.
3. **`def`, `var`, `global`.** Register `def` for the function shape
   of `define` and its layout, `var` for the value and declaration
   shapes, `global` for `define-global`. Registrations only; the
   handlers exist. Layout handlers for `def` and `var` follow
   `layout/define`. The decorator macro gains `def` and `class` arms.
4. **`(vars)` and the repr.** The zero-arity pattern on `compile/vars`
   and `ns_repr` filled in and wired on the four namespace types.
5. **Retire the old spellings** on a schedule of your choosing:
   `define-class`, `define-global`, `function` and the builder `class`
   drop from the docs and corpus first, then from the registrations.
   `define` stays.

## Obligations

- The corpus: every record for `define-class`, `define-global` and the
  `class` builder is re-verified under the new spelling, and new
  records cover `def`, `var`, `global`, `class`, `build-class`,
  `build-function` and `(vars)`, valid and wrong-usage both.
- The docs: the specials pages under `docs/reference/specials/`, the
  layout pages under `docs/reference/layouts/`, the beginner track and
  the glossary; the `compile/class` docstring, which says the special
  binds the name and never did.
- Editor tooling: the lexer's keyword sets and the pygments and LSP
  extras name the forms.
- Tests for the repr: unbound slot, Ref slot, dict-bearing namespace,
  parent by name, and a closure cycle that must not recurse.

## Open questions

1. **Whether `function` survives beside `build-function`.** The
   no-self-bind builder is used by the async and demand macros and by
   the decorator; an alias keeps them, a rename touches four macros.
2. **The repr's body shape**: `<Namespace f1: a=1 scale=2>` on one
   line, or a dict-style body for wide namespaces. The CLI is the
   reader; a line per member past some count may read better.
