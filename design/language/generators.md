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
- **PEP 479 / PEP 525**: a `StopIteration` or `StopAsyncIteration` escaping
  the body becomes `RuntimeError("generator raised ...")` with the original
  as `__cause__`, on every consumer (Python `next`, `async for`, Koskari
  `for-each`); a coroutine body's `StopIteration` becomes
  `RuntimeError("coroutine raised StopIteration")`. Settled 2026-10-06.

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
- When consumed from within Koskari (e.g. `await-for-each`), the gen’s frame
  is attached to the current eval context and stepped in the same
  `koskari_eval` loop (no recursive C call); see `design/EVALUATOR.md`
  § "In-loop flattening".

## Migration Notes

### Migrated (VM)

- **YIELD** and **YIELD_FROM** opcodes. YIELD_FROM delegates to sync/async iterators and generators; handler in `koskari/lib/opcode/YIELD_FROM.c`; the delegation is the delegating frame's resume, the delegate its state. See [../OPCODES.md](../OPCODES.md), [../EVALUATOR.md](../EVALUATOR.md) §5, and [../archive/yield-from-execution-model.md](../archive/yield-from-execution-model.md).

### To Migrate

- Compiler: emit YIELD/YIELD_FROM from `yield` and `yield-from` special forms
- Generator function creation (automatic via `yield` in compiled code)
- Generator protocol support (language surface)
- Exception propagation in generators
- Generator cleanup (language surface)

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
