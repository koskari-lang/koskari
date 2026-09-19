# Guide

One page per mechanism: what it does, how it works, and why it is shaped
that way, with a worked example. Read a page when its topic comes up in
your own code; the pages do not depend on being read in order.

## Reading and evaluation

| Page | Covers |
|------|--------|
| [Model of evaluation](model-of-evaluation.md) | Read, evaluate, data; what a form is and what happens when it runs |
| [Forms and combining](forms-and-combining.md) | Values, names, calls, blocks, and files as sequences of forms |
| [Koskari and s-expressions](layout-and-s-expressions.md) | Each layout rule beside the s-expression it reads to; where parentheses stay |
| [Brackets and the call convention](brackets-and-call-convention.md) | `( )` is a call, `[ ]` is data; the bracket-pair marker and its pragma |
| [Name resolution and prefixes](name-resolution-and-prefixes.md) | The runtime, compile-time, and layout slots; how `(foo ...)` resolves; `compile/` and `layout/` |
| [From source to stack](from-source-to-stack.md) | One expression traced through read, compile, and evaluate |
| [Bytecode mental model](bytecode-mental-model.md) | Why nested calls run inside out |

## Names and values

| Page | Covers |
|------|--------|
| [Bindings and environments](bindings-and-environments.md) | `define`, `let`, `set!`, shadowing |
| [Bindings and refs](bindings-and-refs.md) | Namespaces, bind versus store, ref, deref, resolve, unpack |
| [References and storage indirection](references-and-storage-indirection.md) | Refs as location handles, `&`, writing through a ref, `bind!`, by-ref parameters |
| [Data structures](data-structures.md) | Python containers, pairs, quoting and quasiquote |
| [Types and contracts](types-or-contracts.md) | Annotations as metadata; checking values at runtime |

## Functions and control

| Page | Covers |
|------|--------|
| [Functions and recursion](functions-deep.md) | Formals, defaults, by-ref, multiple return values, recursion |
| [Decorators](decorators.md) | The `@` surface end to end: reader, `decorate`, graft and call-time families, extending it |
| [Control flow](control-flow.md) | `if`, `cond`, `while`, `for-each`, loop `else:`, `and` and `or` |
| [Match patterns](match-patterns.md) | The `match` pattern language: sequences, classes, mappings, guards |
| [Idiomatic Koskari](idiomatic-koskari.md) | The direct form over the assembled one: `item-slice`, dotted `set!`, `define-values`, `None?`, `unless` |

## Async

| Page | Covers |
|------|--------|
| [Demand, async, and defer](demand-async-and-defer.md) | Ambient await, the demand path, `defer`, `on-demand?` |
| [Choosing an async construct](choosing-an-async-construct.md) | `demand`, `defer`, `coroutine-fn`, `create-task`: which one and when |

## Programs

| Page | Covers |
|------|--------|
| [Modules and program structure](modules-and-program-structure.md) | Script shape, imports, `when-main` |
| [Modules and packages](modules-and-packages.md) | `.ki` on disk, `__init__.ki` versus namespace packages, the `.py`/`.ki` same-name conflict, `sys.path` |
| [I/O and effects](io-and-effects.md) | `print`, `with`, exceptions, async effects |
| [Testing and quality](testing-and-quality.md) | Small runnable examples; testing through Python |
| [Hosting Koskari from Python](hosting-from-python.md) | The import hook, running a file into a namespace, awaiting Koskari functions |
| [Packaging a Koskari application](packaging.md) | `koskari_scripts` entry points, the setuptools plugin, the bundled icon |

## Syntax extension

| Page | Covers |
|------|--------|
| [Macros and syntax extension](macros-or-syntax-extension.md) | What a macro does, templates versus procedures, looking at expansions |
| [Macro matching and hygiene](macro-matching-and-hygiene.md) | Pattern matching at expand time, datum forms and wrappers, macros winning over bindings |
| [Fences](fences.md) | Foreign literals as compile-time transliteration; when a format earns one |
| [Pragmas and feature settings](pragma.md) | Setting and reading a pragma; every `reader/*`, `compiler/*`, and `pragma/*` setting |

## Runtime tuning

| Page | Covers |
|------|--------|
| [Attribute and method caching](opcode-caching.md) | The per-site cache and its two pragmas |
| [Code sealing and arena packing](code-sealing.md) | Read-only bytecode, `protect-code` and `pack-module` |

```{toctree}
:maxdepth: 1
:hidden:

model-of-evaluation
forms-and-combining
layout-and-s-expressions
brackets-and-call-convention
name-resolution-and-prefixes
from-source-to-stack
bytecode-mental-model
bindings-and-environments
bindings-and-refs
references-and-storage-indirection
data-structures
types-or-contracts
functions-deep
decorators
control-flow
match-patterns
idiomatic-koskari
demand-async-and-defer
choosing-an-async-construct
modules-and-program-structure
modules-and-packages
io-and-effects
testing-and-quality
hosting-from-python
packaging
macros-or-syntax-extension
macro-matching-and-hygiene
fences
pragma
opcode-caching
code-sealing
```
