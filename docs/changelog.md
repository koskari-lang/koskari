# Changelog

User-visible changes: language behavior, the command line, tooling, and
documentation. Each release entry says what changed for a reader first
and links to the page that covers it.

## 0.2.0

The binding forms are realigned. Source written for 0.1.0 needs
migrating; the old spellings raise errors rather than change
meaning.

- `var` binds a value and `def` a function, in the scope the form
  appears in; `var` over a bracket pattern unpacks. See
  [var](reference/specials/compile-var.md) and
  [def](reference/macros/compile-def.md).
- `class` defines and binds a class; `build-class` builds one without
  binding it. `define-class` and `define-dataclass` are removed: write
  `@dataclass` above `class`. See
  [class](reference/macros/compile-class.md).
- `global` binds at the module, and prefixes `def`, `class` and
  `syntax`. `local` is removed.
- `define` takes Scheme's shape, `(define [f x] body...)` for a
  function and `(define x value)` for a value; the old
  `(define f [x] body...)` no longer matches. See
  [define](reference/specials/compile-define.md) and
  [Bindings and refs](guide/bindings-and-refs.md).
- `syntax` defines a transformer, scoped to the rest of a body when
  written inside one.
- `define-demanding` and `define-coroutine` are removed: write
  `@demand` or `@coroutine-fn` above `def`.
- `(vars)` with no operand is the current namespace and replaces
  `(locals)`; `koskari-vars` is its runtime counterpart, and namespaces
  print their members. See [vars](reference/specials/compile-vars.md)
  and [koskari-vars](reference/runtime/koskari-vars.md).

## 0.1.0

The first public release.

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
