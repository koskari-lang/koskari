# Defer, Task, Future -- usage matrix and role boundaries

**Status**: implemented, 2026-07-10. Records the division of labor
between `defer`, Koskari coroutines, and asyncio's `Task`/`Future`, with
an empirically verified asyncio compatibility table. All four decisions
below are implemented: futures are excluded from implicit auto-await,
`create-task` returns the real Task, defer is single-shot
(delegate-and-die), and KosCoroutine raises on reuse.

## Motivation

`defer` grew into three overlapping jobs: suppressing ambient auto-await
for one hop, adapting koskari calls into loop-schedulable awaitables, and
(accidentally) acting as the handle for scheduled work. The last job is
where it broke down: `create-task` used to return `defer(Task)`, which
hid the entire Task API. `examples/scheduler/kosrunner/kafka.ki` called
`(self._task.cancel)` on that handle, which raised `AttributeError` --
the stop path was designed against `Task` and got a `defer`.

The rule this document lands on: **defer is a one-hop, single-shot capture;
anything that needs a lifetime gets a real Task or Future.**

## The artifacts

| Artifact | What it is | Await semantics |
| --- | --- | --- |
| plain call `(f x)` | ambient model; runs inline, suspends only on a real await | value, already resolved |
| `demand` call | synchronous extent; suspension is an error | value, already resolved |
| `(defer f x)` | single-shot capture: `f` is invoked at construction through the plain Python call protocol; for a koskari callable that yields an **unstarted** coroutine held inside the handle | awaited exactly once; re-await raises like a finished coroutine |
| `Coroutine` (KosCoroutine) | what calling a koskari callable from the Python API returns | single-shot, drives the eval on send |
| `asyncio.Task` / `Future` | loop-owned handle: cancellable, done-callbacks, multi-await | many awaits, external completion |

## asyncio compatibility (verified on Python 3.12)

| asyncio surface | KosCoroutine | defer | notes |
| --- | --- | --- | --- |
| `asyncio.iscoroutine` | **True** | False | KosCoroutine passes `collections.abc.Coroutine` structurally (send/throw/close/`__await__` subclass hook; no explicit registration) |
| `inspect.isawaitable` | True | True | |
| `await x` in a coroutine | yes | yes | |
| `ensure_future(x)` | yes | yes | generic-awaitable wrap path |
| `asyncio.create_task(x)` | **yes, natively** | no (`TypeError`) | defers need `as_coroutine` first |
| `gather`, `wait_for`, `as_completed`, `shield` | yes | yes | ensure_future underneath |
| `asyncio.wait({...})` | via Task | no (`AttributeError: add_done_callback`) | wants Tasks/Futures only |
| `asyncio.run`, `run_coroutine_threadsafe` | yes (iscoroutine passes) | no | wrap defers with `as_coroutine` |
| `iscoroutinefunction(f)` | False for koskari callables | -- | use `coroutine-fn` for frameworks that check (FastAPI, aiohttp) |

The `as_coroutine` and `coroutine-fn` adapters in `koskari.asyncio` remain
the type-hiding shims for the strict-`iscoroutine` and
coroutine-function-checking surfaces. `create-task` does not need
`as_coroutine` for KosCoroutine inputs, only for defer and other generic
awaitables.

## Usage matrix

| Intent | Construct | Consume | Why not the others |
| --- | --- | --- | --- |
| result now, normal async flow | plain call | value in hand | -- |
| result now, suspension forbidden | `demand` | value in hand | plain call may suspend |
| capture work as a value, run later, exactly once | `(defer f x)` | one explicit `(await d)` | plain call runs immediately; Task starts running when scheduled |
| adapt a maybe-async callable to a uniform value | `(await (defer f x))` | immediate | -- |
| hand one unit of work to a Python API that awaits it once (`wait_for`, `gather`, `ensure_future`) | `(defer f x)` | the API awaits it | bare call result would be auto-awaited before the API sees it |
| hand work to a strict-coroutine API (`asyncio.run`, `create_task`, `run_coroutine_threadsafe`) | `(as-coroutine (defer f x))`, or pass the koskari callable to Python and let Python call it | the API drives it | defer alone fails `iscoroutine` |
| register a handler with a framework that checks `iscoroutinefunction` | `(coroutine-fn f)` | framework calls it | koskari callables fail the check |
| schedule concurrent work and keep a control handle (cancel, join, callbacks, re-await, `asyncio.wait`) | `create-task` returning a **real Task** | `.cancel`, `.add_done_callback`, `(await t)` any number of times | defer(Task) hides all of that -- the current kafka.ki bug |
| fire-and-forget background work | `create-task`, keep a strong reference | let it run; optionally done-callback for error logging | the loop holds only a weak ref to tasks |
| a value slot completed by someone else (promise) | `loop.create_future` | producer `set_result`; consumers await/multi-await | defer runs a function; a Future is completed externally |

## Decisions

1. **defer is single-shot and dies loudly.** First await consumes it;
   any later await raises `RuntimeError` in the style of "cannot reuse
   already awaited coroutine". The DONE-caching lifecycle and the
   `KosDefer_FeedResult` / `KosDefer_FeedException` API (never wired) are
   removed. States collapse to DELEGATING / DONE / SPENT / MOVED_FROM.

2. **`create-task` returns the Task itself.** The defer wrap existed only
   to protect the Task from ambient auto-await at the call boundary. The
   protection moves into the evaluator instead: see decision 3. The
   `as_coroutine` wrap stays only for non-coroutine awaitable inputs;
   KosCoroutine inputs pass to `asyncio.create_task` directly.

3. **asyncio futures are excluded from ambient auto-await.** The eval's
   auto-await sites (push_or_await family) currently skip only `defer`.
   They additionally skip anything passing asyncio's own duck check for
   futures (`_asyncio_future_blocking` attribute on the type, the same
   test `asyncio.isfuture` uses). Rationale: a Future/Task is by
   convention a *handle*, and a coroutine is *work in progress from the
   call you just made*. Explicit `(await t)` still resolves handles.

   Consequence to accept: APIs that return futures from ordinary calls
   (`loop.run_in_executor`, `loop.create_future`) now hand back the
   future itself and need an explicit await for the value. That is the
   Python programmer's intuition anyway.

4. **KosCoroutine finishes like a Python coroutine.** Re-sending a
   completed KosCoroutine currently returns `None` silently; it should
   raise `RuntimeError("cannot reuse already awaited coroutine")` for
   parity.

## Follow-ups

- Done: `create-task` returns the Task; scheduler `kafka.ki` cancel path
  works; `server.ki`/`fastapi.ki` keep strong task references.
- Done: `design/api/defer-coroutine.md` rewritten to delegate-and-die
  (Feed API removed).
- Open: reproducer for `Task.cancel()` on a task wrapping a KosCoroutine --
  `CancelledError` must propagate through koskari frames and run dynamic
  wind/unwind correctly (partially covered by
  `tests/importer/test_asyncio_builtins.py` cancel test, which cancels a
  started as_coroutine-wrapped defer).
- Open: corpus records for the matrix rows.

// The end.
