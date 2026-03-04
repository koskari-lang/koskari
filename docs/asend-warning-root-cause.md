# Root Cause Analysis: RuntimeWarning "coroutine method 'asend' was never awaited"

## Summary

The warning occurs because Koskari drives Python async generators' `__anext__` return value (an `async_generator_asend` object) using the low-level generator protocol (`.send(None)` on the `__await__` iterator) instead of proper async awaiting. Python 3.13's finalizer treats this as "never awaited" and emits the warning when the asend object is garbage collected.

## Flow for Python Async Iterators

1. **NEXT opcode** on a Python async gen calls `get_await_next(iter)`.
2. `get_await_next` calls `iter.__anext__()`, which for Python async gens returns `iter.asend(None)` — an `async_generator_asend` object.
3. `next_from_python_async_iter` passes that awaitable to `eager_step_awaitable`.
4. **eager_step_awaitable** (`eval.c` lines 932–1008):
   - Gets `await_iter = awaitable.__await__()`
   - Calls `await_iter.send(None)` to drive it
   - Either completes (StopIteration) or suspends (yielded value)

5. For a simple `async def gen(): yield 10`:
   - One `send(None)` drives the asend to completion and returns 10.
   - The asend is then DECREF'd and finalized.
   - Python 3.13's `_PyGen_Finalize` sees that the asend was driven via sync `.send()` from C (no async frame on the stack) and warns that it was "never awaited".

## Why Python 3.13 Warns

The "coroutine was never awaited" / "coroutine method 'asend' was never awaited" warning is raised by `_PyGen_Finalize` in `Objects/genobject.c` when a coroutine-like object is garbage collected without having been driven from an async frame. Driving via `.send()` from C code does not count as "properly awaited" — Python expects the object to be awaited from async Python code (or equivalent async machinery).

## Why Test-Side `aclose()` Does Not Fix It

The warning is emitted when the asend object is finalized — during iteration, not during cleanup. Each `__anext__()` creates a new asend; each is driven with `.send()` and then released. The warning occurs at that release. Calling `aclose()` on the gen after iteration does not prevent finalization of the asends that were already driven and discarded.

## True Fix

To eliminate the warning, the runtime must avoid driving Python's `async_generator_asend` with `.send()` from C. Instead, it should:

1. **Don’t eager-step Python async gen `__anext__` in C**  
   For the awaitable returned by `get_await_next` when the iterator is a Python async gen, do *not* call `eager_step_awaitable`. Return it as the payload of `KOS_EV_AWAIT` so the driver can await it.

2. **Have the driver await the payload**  
   When the coroutine suspends with `KOS_EV_AWAIT`, the driver must `await payload` and then send the result back. This implies the driver must run in an async context (e.g. an `async def run()` or similar), not a purely sync loop.

3. **Detect Python async gens**  
   Use a type or capability check so this path applies only to Python async generators (e.g. objects whose `__anext__` return value is an `async_generator_asend`), and leave the existing eager-step path for other async iterators (e.g. Koskari async gens) that do not trigger the warning.

## Impact

- **Current:** `eager_step_awaitable` uses `await_iter.send(None)` for all awaitables, including Python’s `async_generator_asend`, which triggers the warning on 3.13.
- **Proposed:** For Python async gen `__anext__`, bypass `eager_step_awaitable`, yield the awaitable as `KOS_EV_AWAIT` payload, and have an async driver await it and send the result back.

## Files Involved

| File | Role |
|------|------|
| `koskari/lib/eval.c` | `eager_step_awaitable`, `next_from_python_async_iter`, `get_await_next` |
| `koskari/lib/coro.c` | `coroutine_send` handling of `KOS_EV_AWAIT` and `py_await_iter` |
| `tests/lib/opcode/__init__.py` | `run_to_completion` — currently ignores payload and always sends `None`; would need an async variant for this fix |

## Driver Behavior Note

Even with the runtime fix above, the tests use `run_to_completion`, which does:

```python
while True:
    coro.send(None)
```

It ignores the return value and always sends `None`. For `KOS_EV_AWAIT`, the correct behavior is:

- When `coro.send(None)` returns a value, the driver must `await` that value and then `coro.send(result)`.
- That requires an async driver, e.g. `async def run_to_completion_async(coro)` or similar, used in tests that mix with Python async generators.

Tests that use Python async iterators should switch to such an async driver so the payload is properly awaited instead of discarded.
