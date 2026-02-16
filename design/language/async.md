# Async and Coroutines

## From python-sibilant

### Coroutine Definition

**`declare-async`**: Flag function as coroutine
- Syntax: `(declare-async)`
- Must be called within function body
- Marks function as async coroutine
- Returns `None`

**`coroutine` macro**: Define coroutine function
- Syntax: `(coroutine NAME (FORMAL...) BODY...)`
- Expands to `(function NAME (FORMAL...) (declare-async) BODY...)`

**`coro` macro**: Anonymous coroutine
- Syntax: `(coro (FORMAL...) BODY...)`
- Expands to `(lambda (FORMAL...) (declare-async) BODY...)`

### Async Operations

**`await`**: Await async operation
- Syntax: `(await EXPR)` or `(await EXPR send: VALUE)`
- Gets awaitable from EXPR
- Yields control until awaitable completes
- Returns result of awaitable
- Optional `send:` keyword for coroutine.send()

**`yield-from`**: Delegate to iterable/async-iterable
- Syntax: `(yield-from EXPR)` or `(yield-from EXPR send: VALUE)`
- Delegates to an iterable or async-iterable (sync/async generators, iterators). **Coroutines are not valid targets**; use `await` for those. The runtime raises `TypeError` if EXPR evaluates to a coroutine.
- Optional `send:` keyword

### Async Context Managers

**`let&` macro**: Async let binding
- Syntax: `(let& ((BINDING EXPR) ...) BODY...)` or `(let& NAME ((BINDING EXPR) ...) BODY...)`
- Returns awaitable coroutine
- Executes bindings and body asynchronously

## From sibilant-ceval

### Async Iteration

**`await-for-each`**: Async iteration
- Syntax: `(await-for-each (BINDING ITEREXPR) BODY... [else: ELSEBODY...])`
- Iterates over async iterable
- Uses `__aiter__` and `__anext__` protocol
- Supports `else:` clause
- Handles `StopAsyncIteration`

### Async Context Managers

**`await-with`**: Async context manager
- Syntax: `(await-with (BINDING EXPR) BODY...)`
- Enters async context manager
- Uses `__aenter__` and `__aexit__` protocol

### Implementation Details

**Async Generator Support**:
- Functions with both `yield` and `await` become async generators
- Creates async generator instance when called
- Operated via `await-for-each`

**Defer Mechanism**:
- `yield-from`/`await` use defer slot in frame
- Outer invocation gets next value from iterable/awaitable
- Frame re-entered when exhausted/completed

### Synchronous entry points (run / iter)

For synchronous Python code (e.g. tests, scripts) that does not run an event loop, the runtime provides **KosCoroutine.run()** and **KosPyAsyncGen.iter()**. These use `asyncio.run()` under the hood so that callers can write `expr().run()` instead of `asyncio.run(expr())`, and `for x in agen.iter():` instead of driving the async gen with `async for` inside `asyncio.run()`. They must not be used when an event loop is already running (deadlock risk). See `design/interpreter/evaluator.md` § "Synchronous adapters (run / iter)".

## Migration Notes

### To Migrate

- Coroutine definition (`declare-async`, `coroutine`, `coro`)
- `await` operation
- `yield-from` for coroutines
- Async iteration (`await-for-each`)
- Async context managers (`await-with`)
- Async generator support

### To Reconsider

- `let&` macro (async let) - is this needed?
- Integration with Python asyncio
- Exception handling in async code
- Cancellation support

### To Design

- Async comprehensions
- Async with multiple awaitables (like `asyncio.gather`)
- Integration with `call/cc` (async continuations?)
- Structured concurrency primitives

## Related Documentation

- [../interpreter/execution-invariants.md](../interpreter/execution-invariants.md) —
  Execution model invariants including AWAIT semantics, flattened async, and
  Python interop
