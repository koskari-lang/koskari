# Runtime library

The runtime builtins -- the values bound in every module, which need no
import -- and the four library modules that ship with the package,
imported as any Python module is. Every builtin name, layout, compile,
and runtime, is listed on [Builtin names](../builtins.md).

## Builtins

| Page | Covers |
|------|--------|
| [Types and type predicates](types-and-predicates.md) | Every bound type and the `NAME?` predicate bound with it |
| [Operators](operators.md) | The arithmetic, bitwise, comparison, membership, and identity operators as functions |
| [Functions](functions.md) | Every other runtime builtin: predicates, pairs, collection builders, calling, async, iteration, conversion, attributes, refs, compilation |
| [Exceptions](../error-codes.md) | The Koskari exception classes and their hierarchy; every Python exception type is bound too |

A Python builtin that is not bound, such as `pow` or `eval`, is reached
through `(import builtins)`.

## Detailed pages

| Name | Does |
|------|------|
| [`compile`](compile.md) | Compile a source form to an invokable expression without running it |
| [`evaluate`](evaluate.md) | Compile a form if needed and run it in a namespace |
| [`get-compiler`](get-compiler.md) | The compiler that owns a namespace, for compiling into that module |
| [`macroexpand`](macroexpand.md) | Expand a form until its head is no longer a macro |
| [`macroexpand-1`](macroexpand-1.md) | One expansion step |
| [`apply-layout`, `apply-layouts`](block-quasiquote-and-block-layout-expansion.md) | Expand a `#block:` cons through the layout handlers at run time, for filled quasiquote blocks |
| [`help`](help.md) | Documentation for a binding, through the pager |
| [`map`, `filter`](map-and-filter.md) | The lazy iterators, which take Koskari callables directly and iterate from both sync and async consumers |
| [`getitem*`, `hasitem*`](getitemstar-and-hasitemstar.md) | Nested item access and membership along a key path |
| [`getattr*`, `hasattr*`](getattrstar-and-hasattrstar.md) | Nested attribute access and presence along a dotted path |
| [`build-slice`](build-slice.md) | Construct a slice object, the runtime behind `item-slice` |
| [`koskari-vars`](koskari-vars.md) | The calling Koskari frame's namespace, or an object's `__dict__` |
| [`on-demand?`](on-demand.md) | Whether the current context is on the demand path, where a bare `await` is not allowed |

## Modules

| Module | Provides |
|--------|----------|
| [`koskari.itertools`](koskari-itertools.md) | The `itertools` combinators in a form that accepts Koskari functions and Koskari sources |
| [`koskari.solve`](koskari-solve.md) | `amb`, `require`, `solve`, and `solve-first`: nondeterministic search over delimited continuations |
| [`koskari.asyncio`](../api/koskari-asyncio.md) | `run-process` and `ProcessResult`, the subprocess pair behind the bash fence |
| [`koskari.assets`](koskari-assets.md) | The packaged icon, as SVG or PNG bytes or a filesystem path |

The full list with signatures is the
[`koskari.builtins`](../api/koskari-builtins.md) API page.

```{toctree}
:maxdepth: 1
:hidden:
:glob:

*
```
