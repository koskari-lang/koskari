# Changelog

User-visible changes: language behavior, the command line, tooling, and
documentation. Each release entry says what changed for a reader first
and links to the page that covers it.

## 0.2.0

In development. Published as weekly `0.2.0.devYYYYMMDD` snapshots.

The binding forms are realigned. Source written for 0.1.0 needs
migrating. Most old spellings raise errors; two change meaning, because
both shapes are valid under the new forms: `(define [x] value)` is now a
function `x` of no formals returning the value, where 0.1.0 bound `x` to
the value, and `(class NAME SUPERS body ...)` as an expression now binds
**NAME** and evaluates to None, where 0.1.0 built the class as a value.

- `var` binds a value and `def` a function, in the scope the form
  appears in; `var` over a bracket pattern unpacks. See
  [var](reference/specials/compile-var.md) and
  [def](reference/macros/compile-def.md).
- `class` defines and binds a class; `build-class` builds one without
  binding it. `define-class` and `define-dataclass` are removed: write
  `@dataclass` above `class`. See
  [class](reference/macros/compile-class.md).
- `global` is a prefix over `var`, `def`, `class` and `syntax` that
  binds the form at the module; `global var x` is the idiomatic
  spelling of `define-global x`, which remains as the primitive. In
  block form the word after `global` decides how the block reads, so
  `global class NAME:` may omit the superclass list. `local` is
  removed. See [global](reference/macros/compile-global.md).
- `define` takes Scheme's shape, `(define [f x] body...)` for a
  function and `(define x value)` for a value; the old
  `(define f [x] body...)` no longer matches. A return type bundles
  with the name as a typed formal does, `(define [(f :: int) x]
  body...)`; the inline `[f :: int x]` of 0.1.0 is rejected. See
  [define](reference/specials/compile-define.md) and
  [Bindings and refs](guide/bindings-and-refs.md).
- `syntax` defines a transformer, scoped to the rest of a body when
  written inside one. `extend-syntax` extends the module's transformer
  from inside a body too, with the new arms present once the form has
  run. See [extend-syntax](reference/macros/compile-extend-syntax.md).
- A decorator line over a declaration, `var x` with no value, is a
  compile error; a typed value, `var x :: int 0`, is decorated with its
  annotation kept. See [decorate](reference/macros/compile-decorate.md).
- `define-demanding` and `define-coroutine` are removed: write
  `@demand` or `@coroutine-fn` above `def`.
- The `import-from` block layout is removed. A long member list wraps
  in the parenthesised form, with the continuation lines indented
  inside the parens; an `import-from M:` block now reads through the
  default block layout and does not import. See
  [import-from](reference/specials/import-from.md).
- `(vars)` with no operand is the current namespace and replaces
  `(locals)`; `koskari-vars` is its runtime counterpart, and namespaces
  print their members. See [vars](reference/specials/compile-vars.md)
  and [koskari-vars](reference/runtime/koskari-vars.md).

## 0.1.0

Published as weekly `0.1.0.devYYYYMMDD` snapshots; no final 0.1.0 was
released.

- S-expressions and an indentation layout that reads to the same forms:
  a line with more than one element is a call, and a line ending in a
  colon opens a block.
- A stackless bytecode evaluator with tail calls, generators, delimited
  and full continuations, dynamic wind, and ambient await.
- Special forms, bootstrap macros, `define-syntax` with template and
  procedural transformers, and hygiene.
- Python interoperability in both directions: `.ki` modules import
  through the standard import system, and every value is a Python
  object.
- Runtime `match` patterns, by-reference parameters, decorators with
  graft and call-time forms, fenced foreign literals, and module
  pragmas.
- The `koskari` command, `.kio` compilation, a language server, and
  editor support for Emacs, Helix, and TextMate.
- Documentation: a quickstart, the Little Koskari and From Python
  tutorials, the guide, and the reference.
