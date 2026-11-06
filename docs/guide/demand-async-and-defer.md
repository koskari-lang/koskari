# Demand, async, and defer

Koskari bytecode runs on an async-native evaluator: suspension and resumption
are normal, and many Python calls that return coroutines or awaitables are
auto-awaited at call boundaries. How you host that evaluator (with or
without an asyncio event loop) is a separate choice.

## Ambient async execution

Python treats asynchronous execution as opt-in: functions are marked `async`,
and calling an async function returns a coroutine that must be awaited.

Koskari treats asynchronous execution as the default. Every function can
suspend and resume; the runtime tries to produce a concrete value at each
step and only suspends when it cannot continue. A stackless evaluator (frames on
the heap, not the C stack) makes that model practical for tail calls,
continuations, generators, and async work in one pipeline.

Three consequences:

- Every Koskari function is capable of suspension.
- Most code reads synchronously -- explicit [`await`](../reference/specials/compile-await.md)
  is uncommon.
- Suspension propagates through the evaluator rather than through coroutine
  objects at every call site.

Shorthand:

> Python often defaults to returning awaitables; Koskari defaults to returning
> values when it can.

## Compared with Python

```python
async def fetch_user():
    return await db.get_user()

result = fetch_user()
print(result)  # <coroutine object ...>
```

The caller must `await` (or schedule) the coroutine. Async tends to spread
through callers -- sometimes called async contagion.

In Koskari:

```koskari
def fetch-user []:
  (db.get-user)

var user (fetch-user)
(print user)
```

If `db.get-user` finishes immediately, `user` is a normal value and `print`
runs. If it must wait on Python, the evaluator suspends the current context and
resumes when the result is ready -- without requiring `async` on `fetch-user`
or an explicit await at the call site.

Conceptually, `(fetch-user)` at a use site behaves like Python's
`await fetch_user()` when suspension is needed, except the await is implicit
and managed by the runtime.

| Behaviour | Python | Koskari |
| --------- | ------ | ------- |
| Functions async by default | No | Yes (capable of suspend) |
| Explicit `async` declaration | Required for async defs | Not required |
| Explicit `await` at most call sites | Usually yes | Usually no |
| Call returns coroutine object by default | Yes (for async def) | No -- value or suspend |
| Runtime tries immediate value resolution | No | Yes |
| Suspension handled by evaluator | No (caller awaits) | Yes |
| Intentionally hold work for later | Tasks, coroutines | [`defer`](#defer) |
| Python dunder / slot may suspend | No | No -- see [demand](#demand-evaluation) |
| Demand (sync-forbidden) execution path | N/A | Yes |

## Auto-await at call boundaries

Coroutines, async generators, and async context managers (`with` / `dyn-with`)
integrate with the same evaluator. Ordinary calls into Python that return
awaitables are typically driven to completion at the boundary, so the stack
holds the result rather than a bare coroutine. See
[compile/await](../reference/specials/compile-await.md) and the
[VM opcode reference](../reference/vm/index.md) for opcode-level detail.

Two kinds of values are excluded and arrive as plain values: a
[defer](#defer) handle, and any asyncio future (anything passing
`asyncio.isfuture`, including `Task`). Futures are handles to work owned by
the event loop; hold them, cancel them, or resolve them with an explicit
`(await fut)` when the value is wanted.

## Suspension in sequential code

```koskari
def fetch-user [id]:
  (db.fetch-user id)

var user (fetch-user id)
(print user)
```

If `db.fetch-user` cannot complete immediately, the evaluator suspends, other
work may run on the host loop, the result arrives, execution resumes, and
`print` runs. The source stays sequential even though suspension occurred.

## defer

Sometimes a program must not resolve a value immediately. A defer is a
single-shot await handle (Python builtin `defer`, also exposed in bootstrap).
The wrapped call runs when the defer is constructed; the VM does not
auto-await the defer value at the next use site.

```koskari
var task (defer fetch-user)
;; ... pass task around ...
(await task)
```

That mirrors holding a coroutine in Python, and like a coroutine the
handle is consumed by its first await -- awaiting it again raises
`RuntimeError`. When you need a handle that can be awaited more than
once, cancelled, or given callbacks, schedule it as a Task with
`create-task`. Defer is explicit -- immediate resolution remains the
default for ordinary calls.

Use explicit [`(await expr)`](../reference/specials/compile-await.md) when `expr` is a defer
or any other awaitable you must suspend on at that program point. See
[`koskari.lib`](../reference/api/koskari-lib.md) for the C/Python type surface.

Bare `(await)` with no argument performs a cooperative suspend on the
async-hosted path; on the demand path it triggers `KoskariSyncViolation`
(see [on-demand?](#authoring-for-both-paths) below).

## Demand evaluation

Demand evaluation runs Koskari to completion without hosting an asyncio
loop for that run. Each top-level form is driven synchronously from the host
(for example `demand_call` on a compiled expression).

Typical uses:

- Default module import path (`exec_module_demand` on the meta path finder)
- Loading bootstrap without an event loop (demand builtins path via
  `koskari.bootstrap`)
- [`load_module_demand`](../reference/api/koskari-module.md) -- same read/compile loop as
  async load, but each expression is executed on the demand path

On the demand path, suspension that requires waiting on a Python awaitable
raises `KoskariSyncViolation`. Koskari code meant to run under demand must
not rely on `await` at points where the host cannot resume.

### Why demand exists

Many Python integration points must not suspend: numeric operators, attribute
access, sequence protocols, iterator hooks, and most CPython slot implementations
expect a synchronous result or an exception -- not a suspended computation.

```python
x + y  # cannot yield to an event loop mid-operation
```

When Python invokes a Koskari dunder (for example `__add__`), the runtime routes
that work through demand execution: produce a value now, or raise; never
suspend. That preserves predictable behaviour for `obj + other` and similar
calls while still allowing suspension inside ordinary Koskari-to-Koskari
evaluation.

Demand establishes a boundary:

- Inside Koskari evaluation (async-hosted), suspension is allowed.
- At synchronous Python-facing boundaries, suspension is forbidden.

## Async-hosted evaluation

Async-hosted evaluation loads or runs code under an asyncio-driven host: each
compiled top-level expression is awaited. The CLI and
[`load_module_async`](../reference/api/koskari-module.md) use this path when an event loop
is available.

Same bytecode and namespaces as demand; different host and allowed
suspensions.

| Aspect | Demand path | Async-hosted path |
|--------|-------------|-------------------|
| Host loop | None required for the Koskari run | asyncio (or compatible) |
| Top-level module run | `load_module_demand` | `load_module_async` |
| `await` in Koskari | Forbidden where sync completion is required | Allowed when the awaitable can run |
| Python dunder / slot entry | Demand only | Demand only |

Importer details: [`koskari.importer`](../reference/api/koskari-importer.md),
[`koskari.module`](../reference/api/koskari-module.md).

## Authoring for both paths

- `@demand` above a `def` -- binds the function wrapped with `demand`, so a
  Python caller with no event loop gets its value
  ([`compile/decorate`](../reference/macros/compile-decorate.md))
- `on-demand?` -- runtime predicate: tries bare `(await)` and returns true if
  that would violate sync completion (demand path)
  ([`on-demand?`](../reference/runtime/on-demand.md))

Keep modules that must import without a loop on the demand path; use
async-hosted load when the embedding app already runs asyncio.

## Choosing a construct

Koskari resolves ordinary calls to values automatically, so most code
needs none of these. Reach for one only when you want to override that
default -- run without a loop, hold a result instead of resolving it, or
hand Koskari async work to a raw asyncio API.

### Decision table

| Your situation | Use | Kind |
|----------------|-----|------|
| The code must run to completion **now, with no event loop** -- a module imported without a host, or a body reached through a Python dunder | **`demand`**, or `@demand` above `def` | function wrapper |
| You want to **hold** an async result and await it **later**, not resolve it at the next use site | **`defer`** | builtin type |
| You must hand a Koskari **function** to a **Python asyncio API that requires a coroutine function** (a task factory, framework hook) | **`coroutine-fn`**, or `@coroutine-fn` above `def` | builtin |
| You need to **schedule or gather** held async work on the asyncio loop | **`create-task`**, or `defer` + `asyncio.gather` | asyncio bridge |

The first two shape how your own code runs; the last two are **bridges to
raw asyncio**. If you are not crossing into an asyncio API by hand, you almost
certainly want `demand`/`defer`, not the coroutine bridges.

> Auto-await caveat. Koskari
> [auto-awaits](#auto-await-at-call-boundaries) an awaitable at the call
> boundary, so calling a coroutine function from Koskari resolves it to a
> value. To hand a real coroutine to asyncio, pass the function
> (`coroutine-fn`) or a `defer`, never a call result.

### `demand` -- run without a loop

Runs on the demand path: produce a value now, or raise -- never suspend. A body
that tries to await a Python awaitable here raises `KoskariSyncViolation`.

```koskari
@demand
def checksum [a b]:
  (+ a b)

(checksum 2 3)   ; -> 5, computed with no event loop hosting the run
```

The decorator lines read as `(decorate [demand] (def checksum [a b] ...))`,
which binds `checksum` to `(demand (function checksum [a b] ...))`. See
[`compile/decorate`](../reference/macros/compile-decorate.md).

### `defer` -- hold work instead of resolving it

A defer holds an async result instead of resolving it at the next use
site, and an explicit `(await ...)` consumes it. [defer](#defer) above
has the rules.

### `coroutine-fn` -- a genuine coroutine function

Some asyncio entry points require a real coroutine function -- one that
`asyncio.iscoroutinefunction` accepts. `coroutine-fn` wraps a function so it
qualifies; you hand the function to the API, and when Python calls it, it
gets a coroutine.

```koskari
(import asyncio)
(import-from asyncio sleep)

@coroutine-fn
def work [x]:
  (await (sleep 0.1))
  x

(asyncio.iscoroutinefunction work)   ; -> True
```

Pass `work` itself to the asyncio API. Do not call `(work 1)` from Koskari
first -- that auto-awaits to the value (see the caveat above).
`@coroutine-fn` above `def work` binds `work` to
`(coroutine-fn (function work [x] ...))`. See
[`compile/decorate`](../reference/macros/compile-decorate.md).

Without the wrapper, `inspect.iscoroutinefunction`,
`asyncio.iscoroutinefunction` and `inspect.isasyncgenfunction` answer False
for a Koskari function, even though a call from Python returns a coroutine.
For a function that does not yield, `inspect.markcoroutinefunction` on
Python 3.12 and later also makes the first two answer True.

### Scheduling and gathering -- `create-task`, `defer` + `gather`

To run held async work on the loop, schedule a `defer` (or coroutine) with
`create-task`. It returns the real `asyncio.Task` -- a handle. Tasks
(like all asyncio futures) are excluded from implicit auto-await, so the
handle can be stored, cancelled, or given done-callbacks, and is resolved
by explicit await:

```koskari
var t (create-task (defer fetch-user))
;; ... other work runs meanwhile ...
;; (t.cancel) would stop it; (await t) resolves it
(await t)
```

Keep a reference to the task while it runs -- the event loop holds only a
weak one. Passing a `defer` to `create-task` consumes it (a defer is
single-shot); the Task owns the work from then on.

For concurrency, hold each call as a `defer` and await them together -- defers
are awaitable, so `asyncio.gather` accepts them directly:

```koskari
(import asyncio)
var a (defer (function fetch [] (fetch-user 1)))
var b (defer (function fetch [] (fetch-user 2)))
(await (asyncio.gather a b))          ; both run concurrently
```

A defer satisfies `asyncio.iscoroutine` (it carries the full coroutine
protocol), so the strict surfaces -- `asyncio.create_task`, `asyncio.run`,
`run_coroutine_threadsafe` -- accept it directly. The lower-level
`as_coroutine` helper (in `koskari.asyncio`, reached by import) remains
for the rare non-coroutine awaitable that needs to satisfy those checks.

## See also

- [compile/await](../reference/specials/compile-await.md): explicit
  await and bare suspend
- [Hosting Koskari from Python](hosting-from-python.md): the load paths
- [koskari.asyncio](../reference/api/koskari-asyncio.md): bridging Koskari
  and asyncio tasks
