# Runtime library

Functions and modules available at run time: the builtins written in
Koskari or native to it, and the four library modules that ship with
the package -- `koskari.itertools`, `koskari.solve`, `koskari.asyncio`,
and `koskari.assets`. Names in the builtins need no import; the modules
are imported as any Python module is.

## Compilation and expansion

| Name | Does |
|------|------|
| [`compile`](compile.md) | Compile a source form to an invokable expression without running it |
| [`evaluate`](evaluate.md) | Compile a form if needed and run it in a namespace |
| [`get-compiler`](get-compiler.md) | The compiler that owns a namespace, for compiling into that module |
| [`macroexpand`](macroexpand.md) | Expand a form until its head is no longer a macro |
| [`macroexpand-1`](macroexpand-1.md) | One expansion step |
| [`apply-layouts`](block-quasiquote-and-block-layout-expansion.md) | Expand a `#block:` cons through the layout handlers at run time, for filled quasiquote blocks |
| [`help`](help.md) | Documentation for a binding, through the pager |

## Values and access

| Name | Does |
|------|------|
| [`map`, `filter`](map-and-filter.md) | The native iterators, which take Koskari callables directly and iterate from both sync and async consumers |
| [`getitem*`, `hasitem*`](getitemstar-and-hasitemstar.md) | Nested item access and membership along a key path |
| [`getattr*`, `hasattr*`](getattrstar-and-hasattrstar.md) | Nested attribute access and presence along a dotted path |
| [`build-slice`](build-slice.md) | Construct a slice object, the runtime behind `item-slice` |
| [`on-demand?`](on-demand.md) | Whether the current context is on the demand path, where a bare `await` is not allowed |

## Modules

| Module | Provides |
|--------|----------|
| [`koskari.itertools`](koskari-itertools.md) | The `itertools` combinators in a form that accepts Koskari functions and Koskari sources |
| [`koskari.solve`](koskari-solve.md) | `amb`, `require`, `solve`, and `solve-first`: nondeterministic search over delimited continuations |
| [`koskari.asyncio`](../api/koskari-asyncio.md) | `run-process` and `ProcessResult`, the subprocess pair behind the bash fence |
| [`koskari.assets`](koskari-assets.md) | The packaged icon, as SVG or PNG bytes or a filesystem path |

A chosen set of the Python builtins is bound as well, under their
Python names: `len`, `print`, `sorted`, `enumerate`, `hex`, `bytearray`,
`NotImplemented`, and every exception type, with the natives `map`,
`filter`, and `partial` replacing their Python namesakes, which remain
as `py-map`, `py-filter`, and `functools.partial`. The predicates use a
`?` suffix: `instance?`, `subclass?`, `callable?`, `None?`, `nil?`. The
full list with signatures is the
[`koskari.builtins`](../api/koskari-builtins.md) API page; a Python
builtin outside it, such as `pow` or `eval`, is reached through
`(import builtins)`.

```{toctree}
:maxdepth: 1
:hidden:
:glob:

*
```
