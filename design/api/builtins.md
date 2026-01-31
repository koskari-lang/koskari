# Built-in Functions

This document describes the built-in functions available in koskari.lib.

## Pair Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `cons` | `cons(*args, recursive=False)` | Create cons pair from arguments |
| `car` | `car(pair)` | Get first element of pair |
| `cdr` | `cdr(pair)` | Get rest of pair |
| `setcar` | `setcar(pair, value)` | Set first element of pair |
| `setcdr` | `setcdr(pair, value)` | Set rest of pair |
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

## String Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `build-str` | `(build-str *args)` | Concatenate string representations |

## Collection Builders

| Function | Signature | Description |
|----------|-----------|-------------|
| `build_tuple` | `build_tuple(*args)` | Build tuple from arguments |
| `build_list` | `build_list(*args)` | Build list from arguments |
| `build_set` | `build_set(*args)` | Build set from arguments |
| `build_dict` | `build_dict(*pairs, **kwargs)` | Build dict from key-value pairs |

## Utility Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `apply` | `apply(func, args=(), kwargs=None)` | Call function with args/kwargs; auto-unpacks pairs |
| `reapply` | `reapply(func, data, count)` | Apply `data = func(data)` count times |
| `identity` | `identity(value)` | Return argument unchanged |
| `constantly` | `constantly(value)` | Return callable that always returns value |
| `_pass` | `_pass(*args, **kwargs)` | No-op; accepts anything, returns None |

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
- Collection builders (`build_tuple`, `build_list`, `build_set`, `build_dict`)

**From sibilant-ceval**:
- Reducing operators (`+`, `-`, `*`, `/`, `//`, `@`, `&`, `|`, `^`)
- Membership operators (`in`, `not-in`)
- String builder (`build-str`)
- Namespace types (`Namespace`, `NamespaceSpec`, `ModuleNamespace`, `CollectorNamespace`, `Module`)
- Reference types (`Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`)
- Formals types (`Formals`, `FormalsSpec`)

### Not Migrating

The following are intentionally not being migrated:

- TCO infrastructure (`tailcall`, `trampoline`, `tailcall_full`, `tcr_frame_vars`) - 
  will be handled directly in the native evaluator
- PyCell operations (`getderef`, `setderef`, `clearderef`) - 
  replaced by Ref types

### To Consider

- `repeatedly(work, *args, **kwargs)` - infinite generator, may stay in Python
- `lazygensym` - lazy symbol resolution
- Sequence utilities (`last`, `take`, `first`..`tenth`)
