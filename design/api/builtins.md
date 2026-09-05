# Built-in Functions

This document describes the built-in functions available in koskari.lib.

## Pair Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `cons` | `cons(*args)` | Create cons pair from arguments (last arg is tail; no tail implies nil) |
| `car` | `car(pair)` | Get first element of pair |
| `cdr` | `cdr(pair)` | Get rest of pair |
| `setcar` | `setcar(pair, value)` | Set first element of pair |
| `setcdr` | `setcdr(pair, value)` | Set rest of pair |
| `make_recursive` | `make_recursive(pair)` | Set tail-most link's cdr to head, making the chain circular |
| `break_recursive` | `break_recursive(pair)` | Set first link whose cdr is head to nil, breaking the cycle |
| `build_unpack_pair` | `build_unpack_pair(*iterables)` | Build pair from unpacked iterables |

## Atom Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `gensym` | `gensym([prefix], [predicate])` | Generate unique symbol |

## Reducing Operators

These operators accept variable arguments and reduce them using the
corresponding operation. Unary forms for `+` and `-` use the positive/negative
unary operations.

| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition (unary: positive) | `(+ 1 2 3)` → `6` |
| `-` | Subtraction (unary: negate) | `(- 10 3 2)` → `5` |
| `*` | Multiplication | `(* 2 3 4)` → `24` |
| `/` | True division | `(/ 10 2)` → `5.0` |
| `//` | Floor division | `(// 10 3)` → `3` |
| `@` | Matrix multiplication | `(@ A B C)` |
| `&` | Bitwise AND | `(& 0xFF 0x0F)` → `0x0F` |
| `\|` | Bitwise OR | `(\| 0x01 0x02)` → `0x03` |
| `^` | Bitwise XOR | `(^ 0xFF 0x0F)` → `0xF0` |

## Membership Operators

| Operator | Signature | Description |
|----------|-----------|-------------|
| `in` | `(in val container)` | Check if val is in container |
| `not-in` | `(not-in val container)` | Check if val is not in container |

## Collection Builders

| Function | Signature | Description |
|----------|-----------|-------------|
| `build_tuple` | `build_tuple(*args)` | Build tuple from arguments |
| `build_list` | `build_list(*args)` | Build list from arguments |
| `build_set` | `build_set(*args)` | Build set from arguments |
| `build_dict` | `build_dict(*pairs, **kwargs)` | Build dict from key-value pairs |
| `build_str` | `build_str(*args)` | Join the str of each argument; the runtime `#str` and the OBJECT_STR core |

## Utility Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `apply` | `apply(func, args=(), kwargs=None)` | Call function with args/kwargs; auto-unpacks pairs |
| `reapply` | `reapply(func, data, count)` | Apply `data = func(data)` count times |
| `identity` | `identity(value)` | Return argument unchanged |
| `constantly` | `constantly(value)` | Return callable that always returns value |
| `_pass` | `_pass(*args, **kwargs)` | No-op; accepts anything, returns None |

## Async handle: defer (DeferCoroutine)

| Construct | Signature | Description |
|-----------|-----------|-------------|
| `defer` | `(defer f a b c ... key: val ...)` | Build a single-shot await-handle: stores `f`, positional `args`, and keyword `kwds`; does not call `f` until first explicit `await`. Accepts all argument types. Excluded from implicit auto-await so it can be passed around as a value. |

Implemented in `koskari/lib/defer.c`. See [defer-coroutine.md](defer-coroutine.md) for full design (lifecycle, defer-of-defer collapse, AWAIT integration, invariants).

## Types Available

The following types are exported from `koskari.lib`:

### Core Types

| Type | Description |
|------|-------------|
| `symbol` | Interned immutable identifier |
| `keyword` | Interned keyword with colon handling |
| `pair` | Mutable cons cell |
| `nil` | Empty list singleton |
| `values` | Multi-value return object |

### Namespace Types

| Type | Description |
|------|-------------|
| `Namespace` | Lexical scope container |
| `NamespaceSpec` | Specification for namespace creation |
| `ModuleNamespace` | Module-level namespace |
| `CollectorNamespace` | Namespace blocking closure capture |
| `Module` | Module wrapper with attribute access |

### Reference Types

| Type | Description |
|------|-------------|
| `Ref` | Base mutable reference cell |
| `AttributeRef` | Reference backed by object attribute |
| `ItemRef` | Reference backed by mapping/sequence item |
| `DynamicRef` | Reference backed by getter/setter/deleter |
| `ConstantRef` | Immutable reference |
| `MemoizedRef` | Lazy reference caching first result |

### Async handle type

| Type | Description |
|------|-------------|
| `DeferCoroutine` | Single-shot await-handle produced by `defer`; awaitable only via explicit `await`, not by implicit greedy await |

### Formals Types

| Type | Description |
|------|-------------|
| `Formals` | Formal parameter bindings |
| `FormalsSpec` | Template for creating Formals |

## Migration Status

### Completed

All the following have been migrated from python-sibilant and sibilant-ceval:

**From python-sibilant**:
- Pair operations (`cons`, `car`, `cdr`, `setcar`, `setcdr`, `build_unpack_pair`)
- Atom operations (`gensym`, `symbol`, `keyword`)
- Values type
- Utility functions (`reapply`, `_pass`, `apply`, `identity`, `constantly`)
- Collection builders (`build_tuple`, `build_list`, `build_set`, `build_dict`, `build_str`)

**From sibilant-ceval**:
- Reducing operators (`+`, `-`, `*`, `/`, `//`, `@`, `&`, `|`, `^`)
- Membership operators (`in`, `not-in`)
- Namespace types (`Namespace`, `NamespaceSpec`, `ModuleNamespace`, `CollectorNamespace`, `Module`)
- Reference types (`Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`)
- Formals types (`Formals`, `FormalsSpec`)

### Not Migrating

The following are intentionally not being migrated:

- TCO infrastructure (`tailcall`, `tailcall_full`, `tcr_frame_vars`) - 
  will be handled directly in the native evaluator
- PyCell operations (`getderef`, `setderef`, `clearderef`) - 
  replaced by Ref types

### To Consider

- `repeatedly(work, *args, **kwargs)` - infinite generator, may stay in Python
- `lazygensym` - lazy symbol resolution
- Sequence utilities (`last`, `take`, `first`..`tenth`)
