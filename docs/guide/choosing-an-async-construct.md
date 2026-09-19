# Choosing an async construct

Koskari resolves ordinary calls to values automatically (see [Demand, async,
and defer](demand-async-and-defer.md)), so most code needs none of these. Reach
for one only when you want to *override* that default — run without a loop, hold
a result instead of resolving it, or hand Koskari async work to a raw asyncio
API. The linked pages are the full rules.

## Decision table

| Your situation | Use | Kind |
|----------------|-----|------|
| The code must run to completion **now, with no event loop** — a module imported without a host, or a body reached through a Python dunder | **`demand`** / **`define-demanding`** | function wrapper / macro |
| You want to **hold** an async result and await it **later**, not resolve it at the next use site | **`defer`** | builtin type |
| You must hand a Koskari **function** to a **Python asyncio API that requires a coroutine function** (a task factory, framework hook) | **`coroutine-fn`** / **`define-coroutine`** | builtin / macro |
| You need to **schedule or gather** held async work on the asyncio loop | **`create-task`**, or `defer` + `asyncio.gather` | asyncio bridge |

The first two shape **how your own code runs**; the last two are **bridges to
raw asyncio**. If you are not crossing into an asyncio API by hand, you almost
certainly want `demand`/`defer`, not the coroutine bridges.

> **Auto-await caveat.** Koskari auto-awaits an awaitable at the call boundary
> (see [Demand, async, and defer](demand-async-and-defer.md)). So calling a
> coroutine function *from Koskari* resolves it to a value — to hand a real
> coroutine to asyncio, pass the **function** (`coroutine-fn`) or a **`defer`**,
> never a call result.

## `demand` / `define-demanding` — run without a loop

Runs on the demand path: produce a value now, or raise — never suspend. A body
that tries to await a Python awaitable here raises `KoskariSyncViolation`.

```koskari
define-demanding checksum [a b]:
  (+ a b)

(checksum 2 3)   ; -> 5, computed with no event loop hosting the run
```

`define-demanding NAME formals body` expands to
`(define NAME (demand (function NAME formals body ...)))`. See
[`compile/define-demanding`](../reference/macros/compile-define-demanding.md).

## `defer` — hold work instead of resolving it

The wrapped call runs when the defer is **constructed**; the VM does **not**
auto-await the defer at its next use. You resolve it explicitly.

```koskari
define task (defer fetch-user)   ; runs fetch-user now; result is held
;; ... pass task around; it is not auto-awaited ...
(await task)                     ; resolve when you are ready
```

Full treatment: [Demand, async, and defer](demand-async-and-defer.md) and
[`compile/await`](../reference/specials/compile-await.md).

## `coroutine-fn` / `define-coroutine` — a genuine coroutine function

Some asyncio entry points require a real *coroutine function* — one that
`asyncio.iscoroutinefunction` accepts. `coroutine-fn` wraps a function so it
qualifies; you hand the **function** to the API, and when *Python* calls it, it
gets a coroutine.

```koskari
(import asyncio)

define-coroutine work [x]:
  (await (sleep 0.1))
  x

(asyncio.iscoroutinefunction work)   ; -> True
```

Pass `work` itself to the asyncio API. Do **not** call `(work 1)` from Koskari
first — that auto-awaits to the value (see the caveat above).
`define-coroutine NAME args body` expands to
`(define NAME (coroutine-fn (function NAME args body ...)))`. See
[`compile/define-coroutine`](../reference/macros/compile-define-coroutine.md).

## Scheduling and gathering — `create-task`, `defer` + `gather`

To run held async work on the loop, schedule a `defer` (or coroutine) with
**`create-task`**. It returns the real `asyncio.Task` — a handle. Tasks
(like all asyncio futures) are excluded from implicit auto-await, so the
handle can be stored, cancelled, or given done-callbacks, and is resolved
by explicit await:

```koskari
define t (create-task (defer fetch-user))
;; ... other work runs meanwhile ...
;; (t.cancel) would stop it; (await t) resolves it
(await t)
```

Keep a reference to the task while it runs — the event loop holds only a
weak one. Passing a `defer` to `create-task` consumes it (a defer is
single-shot); the Task owns the work from then on.

For concurrency, hold each call as a `defer` and await them together — defers
are awaitable, so `asyncio.gather` accepts them directly:

```koskari
(import asyncio)
define a (defer (function fetch [] (fetch-user 1)))
define b (defer (function fetch [] (fetch-user 2)))
(await (asyncio.gather a b))          ; both run concurrently
```

A defer satisfies `asyncio.iscoroutine` (it carries the full coroutine
protocol), so the strict surfaces — `asyncio.create_task`, `asyncio.run`,
`run_coroutine_threadsafe` — accept it directly. The lower-level
`as-coroutine` helper (in `koskari.asyncio`, reached by import) remains
for the rare non-coroutine awaitable that needs to satisfy those checks.

## See also

- [Demand, async, and defer](demand-async-and-defer.md) — the authoritative
  model behind every construct here.
- [`compile/await`](../reference/specials/compile-await.md) — explicit await and bare
  suspend.
