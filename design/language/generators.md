# Generators

## From python-sibilant

### Generator Definition

Generators are created when a function contains `yield` or `yield-from`:
- `function` with `yield` → generator function
- `lambda` with `yield` → generator function
- `let` with `yield` → generator function

### Yield Operations

**`yield`**: Yield value from generator
- Syntax: `(yield [VALUE])`
- Pauses function execution
- Returns VALUE to caller
- Resumes when generator is advanced
- Defaults to `None` if no value

**`yield-from`**: Delegate to another generator
- Syntax: `(yield-from EXPR)` or `(yield-from EXPR send: VALUE)`
- Delegates to iterable/generator
- Yields each value from iterable
- When exhausted, continues execution
- Optional `send:` keyword for generator.send()

### Generator Features

- **Lazy evaluation**: Generators produce values on demand
- **State preservation**: Generator state preserved between yields
- **Exception handling**: Exceptions can be sent into generator via `.throw()`
- **Cleanup**: Generator cleanup via `.close()`

## From sibilant-ceval

### Implementation Details

**Defer Slot**:
- Frame has defer slot for `yield-from`
- Outer invocation gets next value directly from iterable
- Frame re-entered only when iterable exhausted
- More efficient than bouncing each value

**Async Generators**:
- Functions with both `yield` and `await` become async generators
- Operated via `await-for-each`
- Uses `__aiter__` and `__anext__` protocol

## Migration Notes

### To Migrate

- Generator function creation (automatic via `yield`)
- `yield` operation
- `yield-from` operation
- Generator protocol support
- Exception propagation in generators
- Generator cleanup

### To Reconsider

- Defer slot mechanism (sibilant-ceval optimization)
- Generator performance optimizations
- Integration with comprehensions

### To Design

- Generator expressions (if desired)
- Generator composition utilities
- Integration with `call/cc` (generator continuations?)

## Related Documentation

- [../interpreter/execution-invariants.md](../interpreter/execution-invariants.md) —
  Execution model invariants including generator/async generator separation
