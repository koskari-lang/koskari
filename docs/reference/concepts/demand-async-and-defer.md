# Demand, async evaluation, and defer

Koskari bytecode runs on an **async-native** evaluator: suspension and resumption
are normal, and many Python calls that return coroutines or awaitables are
**auto-awaited** at call boundaries. How you **host** that evaluator (with or
without an asyncio event loop) is a separate choice.

## Ambient async execution

Python treats asynchronous execution as opt-in: functions are marked `async`,
and calling an async function returns a coroutine that must be awaited.

Koskari treats asynchronous execution as the default. Every function can
suspend and resume; the runtime tries to produce a **concrete value** at each
step and only suspends when it cannot continue. A stackless evaluator (frames on
the heap, not the C stack) makes that model practical for tail calls,
continuations, generators, and async work in one pipeline.

In practice:

- Every Koskari function is capable of suspension.
- Most code **reads** synchronously — explicit [`await`](../specials/compile-await.md)
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
through callers — sometimes called async contagion.

In Koskari:

```koskari
define fetch-user []:
  (db.get-user)

define user (fetch-user)
(print user)
```

If `db.get-user` finishes immediately, `user` is a normal value and `print`
runs. If it must wait on Python, the evaluator suspends the current context and
resumes when the result is ready — without requiring `async` on `fetch-user`
or an explicit await at the call site.

Conceptually, `(fetch-user)` at a use site behaves like Python's
`await fetch_user()` when suspension is needed, except the await is implicit
and managed by the runtime.

| Behaviour | Python | Koskari |
| --------- | ------ | ------- |
| Functions async by default | No | Yes (capable of suspend) |
| Explicit `async` declaration | Required for async defs | Not required |
| Explicit `await` at most call sites | Usually yes | Usually no |
| Call returns coroutine object by default | Yes (for async def) | No — value or suspend |
| Runtime tries immediate value resolution | No | Yes |
| Suspension handled by evaluator | No (caller awaits) | Yes |
| Intentionally hold work for later | Tasks, coroutines | [`defer`](#defer) |
| Python dunder / slot may suspend | No | No — see [demand](#demand-evaluation) |
| Demand (sync-forbidden) execution path | N/A | Yes |

## Auto-await at call boundaries

Coroutines, async generators, and async context managers (`with` / `dyn-with`)
integrate with the same evaluator. Ordinary calls into Python that return
awaitables are typically driven to completion at the boundary, so the stack
holds the result rather than a bare coroutine. See
[compile/await](../specials/compile-await.md) and the
[VM opcode reference](../vm/index.md) for opcode-level detail.

Two kinds of values are excluded and arrive as plain values: a
[defer](#defer) handle, and any asyncio **future** (anything passing
`asyncio.isfuture`, including `Task`). Futures are handles to work owned by
the event loop; hold them, cancel them, or resolve them with an explicit
`(await fut)` when the value is wanted.

## Suspension in sequential code

```koskari
define fetch-user [id]:
  (db.fetch-user id)

define user (fetch-user id)
(print user)
```

If `db.fetch-user` cannot complete immediately, the evaluator suspends, other
work may run on the host loop, the result arrives, execution resumes, and
`print` runs. The source stays sequential even though suspension occurred.

## defer

Sometimes a program must **not** resolve a value immediately. A **defer** is a
single-shot await handle (Python builtin `defer`, also exposed in bootstrap).
The wrapped call runs when the defer is **constructed**; the VM does **not**
auto-await the defer value at the next use site.

```koskari
define task (defer fetch-user)
;; ... pass task around ...
(await task)
```

That mirrors holding a coroutine in Python, and like a coroutine the
handle is consumed by its first await — awaiting it again raises
`RuntimeError`. When you need a handle that can be awaited more than
once, cancelled, or given callbacks, schedule it as a Task with
`create-task`. Defer is explicit — immediate resolution remains the
default for ordinary calls.

Use explicit [`(await expr)`](../specials/compile-await.md) when `expr` is a defer
or any other awaitable you must suspend on at that program point. See
[`koskari.lib`](../../api/koskari-lib.md) for the C/Python type surface.

Bare **`(await)`** with no argument performs a cooperative suspend on the
async-hosted path; on the demand path it triggers **`KoskariSyncViolation`**
(see [on-demand?](#authoring-for-both-paths) below).

## Demand evaluation

**Demand evaluation** runs Koskari to completion **without** hosting an asyncio
loop for that run. Each top-level form is driven synchronously from the host
(for example `demand_call` on a compiled expression).

Typical uses:

- Default **module import** path (`exec_module_demand` on the meta path finder)
- Loading bootstrap without an event loop (demand builtins path via
  `koskari.bootstrap`)
- [`load_module_demand`](../../api/koskari-module.md) — same read/compile loop as
  async load, but each expression is executed on the demand path

On the demand path, suspension that requires waiting on a Python awaitable
raises **`KoskariSyncViolation`**. Koskari code meant to run under demand must
not rely on `await` at points where the host cannot resume.

### Why demand exists

Many **Python integration points must not suspend**: numeric operators, attribute
access, sequence protocols, iterator hooks, and most CPython slot implementations
expect a synchronous result or an exception — not a suspended computation.

```python
x + y  # cannot yield to an event loop mid-operation
```

When Python invokes a Koskari dunder (for example `__add__`), the runtime routes
that work through **demand execution**: produce a value now, or raise; never
suspend. That preserves predictable behaviour for `obj + other` and similar
calls while still allowing suspension inside ordinary Koskari-to-Koskari
evaluation.

Demand establishes a boundary:

- Inside Koskari evaluation (async-hosted), suspension is allowed.
- At synchronous Python-facing boundaries, suspension is forbidden.

## Async-hosted evaluation

**Async-hosted evaluation** loads or runs code under an asyncio-driven host: each
compiled top-level expression is awaited. The CLI and
[`load_module_async`](../../api/koskari-module.md) use this path when an event loop
is available.

Same bytecode and namespaces as demand; different **host** and allowed
suspensions.

| Aspect | Demand path | Async-hosted path |
|--------|-------------|-------------------|
| Host loop | None required for the Koskari run | asyncio (or compatible) |
| Top-level module run | `load_module_demand` | `load_module_async` |
| `await` in Koskari | Forbidden where sync completion is required | Allowed when the awaitable can run |
| Python dunder / slot entry | Demand only | Demand only |

Importer details: [`koskari.importer`](../../api/koskari-importer.md),
[`koskari.module`](../../api/koskari-module.md).

## Authoring for both paths

- **`define-demanding`** — defines a function wrapped with `demand` so its body
  is compiled for demand-call contexts
  ([`compile/define-demanding`](../macros/compile-define-demanding.md))
- **`on-demand?`** — runtime predicate: tries bare `(await)` and returns true if
  that would violate sync completion (demand path)
  ([`on-demand?`](../runtime/on-demand.md))

Prefer demand-friendly code in modules that must import without a loop; use
async-hosted load when the embedding app already runs asyncio.

## Related reading

- [compile/await](../specials/compile-await.md) — explicit await and bare suspend
- [From Python: interop](../../start/from-python/interop.md) — embedding and load paths
- [koskari.asyncio](../../api/koskari-asyncio.md) — bridging Koskari and asyncio tasks
