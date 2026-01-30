# Built-in Functions

## From python-sibilant

### Pair Operations

- `cons(car, cdr)`: Create cons cell
- `car(pair)`: Get first element
- `cdr(pair)`: Get rest
- `setcar(pair, value)`: Set first element
- `setcdr(pair, value)`: Set rest
- `build-proper(*values)`: Create proper list
- `build-unpack-pair(*tuples)`: Build pair from unpacked tuples
- `unpack(value)`: Unpack pair or iterable

### Collection Builders

- `build_tuple(*items)`: Build tuple
- `build_list(*items)`: Build list
- `build_set(*items)`: Build set
- `build_dict(*pairs)`: Build dict from key-value pairs

### Values and Unpacking

- `values(*args, **kwargs)`: Create values object for multi-value return
- `getderef(ref)`: Get value from reference
- `setderef(ref, value)`: Set value in reference
- `clearderef(ref)`: Clear reference

### Utilities

- `reapply(func, *args, n)`: Apply function n times
- `repeatedly(work, *args, **kwargs)`: Infinite generator
- `last(seq, empty=None)`: Get last item
- `take(n, seq)`: Take first n items
- `_pass()`: No-op function

## From sibilant-ceval

### Namespace Operations

- Namespace creation and manipulation (via opcodes)
- Reference cell operations (via opcodes)
- Module operations (via opcodes)

## Migration Status

### Completed

The following built-in functions have been migrated to koskari:

**Pair Operations**:
- `cons(*args, recursive=False)`: Variadic pair builder
- `car(pair)`: Get first element
- `cdr(pair)`: Get rest
- `setcar(pair, value)`: Set first element
- `setcdr(pair, value)`: Set rest
- `build_unpack_pair(*iterables)`: Build pair from unpacked iterables

**Atom Operations**:
- `gensym([prefix], [predicate])`: Generate unique symbol

**Values**:
- `values(*args, **kwargs)`: Create multi-value return object

### To Migrate

- Collection builders (`build_tuple`, `build_list`, `build_set`, `build_dict`)
- Reference operations (`getderef`, `setderef`, `clearderef`)
- Tail-call support infrastructure

### To Reconsider

- `repeatedly` utility
- TCO infrastructure (trampoline vs. direct)

### To Design

- Additional collection utilities
- Iterator utilities
- Functional programming utilities
