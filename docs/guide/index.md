# Guide

One page per mechanism: what it does, how it works, and why it is shaped
that way. The pages do not depend on being read in order; start with the
one whose topic has come up in your code.

## Reading and evaluation

| Page | Covers |
|------|--------|
| [Model of evaluation](model-of-evaluation.md) | Forms; the read, expand, compile, and evaluate stages; which stage an error names; code as data |
| [Koskari and s-expressions](layout-and-s-expressions.md) | Each layout rule beside the s-expression it reads to, and where parentheses stay |
| [Brackets and the call convention](brackets-and-call-convention.md) | `( )` is a call and `[ ]` is data; the bracket-pair marker and its pragma |
| [Name resolution and prefixes](name-resolution-and-prefixes.md) | The runtime, compile-time, and layout slots; how `(foo ...)` resolves; `compile/` and `layout/` |

## Names and values

| Page | Covers |
|------|--------|
| [Bindings and refs](bindings-and-refs.md) | `define`, `let`, `set!`, shadowing; namespaces, bind versus store, ref, deref, resolve, unpack |
| [References and storage indirection](references-and-storage-indirection.md) | Refs as location handles, `&`, writing through a ref, `bind!`, `take!`, dynamic refs |
| [Data structures](data-structures.md) | Python containers, pairs, quote and quasiquote, symbols and keywords |
| [Types and annotations](types-and-annotations.md) | `::` annotations, where they land, checking values at runtime |

## Functions and control

| Page | Covers |
|------|--------|
| [Functions](functions.md) | Formals, defaults, rest collectors, `return` and multiple values, closures, recursion and named `let`, thunks |
| [Decorators](decorators.md) | The `@` surface end to end: reader, `decorate`, graft and call-time families, extending it |
| [Control flow](control-flow.md) | `if`, `cond`, `when`, `unless`, `while`, `for-each`, `break` with a value, the `-each` family |
| [Match patterns](match-patterns.md) | The `match` pattern language: sequences, classes, mappings, typed captures, guards |
| [Idiomatic Koskari](idiomatic-koskari.md) | The direct form over the assembled one: `item-slice`, dotted `set!`, `define-values`, `None?`, `unless` |

## Programs

| Page | Covers |
|------|--------|
| [Modules and packages](modules-and-packages.md) | The shape of a file, imports, `when-main`, packages on disk, the `.py`/`.ki` same-name trap, `sys.path` |
| [I/O and effects](io-and-effects.md) | `print`, `with`, exceptions and `try` as an expression, stores, async effects |
| [Demand, async, and defer](demand-async-and-defer.md) | Ambient await, the demand path, `defer`, and which construct to reach for |
| [Hosting Koskari from Python](hosting-from-python.md) | The import hook, running a file into a namespace, awaiting Koskari functions from Python |
| [Packaging an installed command](packaging.md) | `koskari_scripts` entry points and the setuptools plugin |
| [Runtime tuning](runtime-tuning.md) | The per-site attribute cache and read-only code storage, and their pragmas |

## Syntax extension

| Page | Covers |
|------|--------|
| [Macros and hygiene](macros-and-hygiene.md) | What a macro is for, `define-syntax`, templates versus procedures, hygiene, expansion order |
| [Fences](fences.md) | Foreign literals as compile-time transliteration; when a format earns one |
| [Pragmas and feature settings](pragma.md) | Setting and reading a pragma; every `reader/*`, `compiler/*`, and `pragma/*` setting |

```{toctree}
:maxdepth: 1
:hidden:

model-of-evaluation
layout-and-s-expressions
brackets-and-call-convention
name-resolution-and-prefixes
bindings-and-refs
references-and-storage-indirection
data-structures
types-and-annotations
functions
decorators
control-flow
match-patterns
idiomatic-koskari
modules-and-packages
io-and-effects
demand-async-and-defer
hosting-from-python
packaging
runtime-tuning
macros-and-hygiene
fences
pragma
```
