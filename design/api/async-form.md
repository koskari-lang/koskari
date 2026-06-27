# Concurrent async form (`async`)

**Implementation status:** Proposed — evaluate and possibly implement.

## Purpose

Koskari is **async-native**: ordinary calls auto-await at boundaries and most
code reads sequentially. That makes it easy to write sequential async workflows
and easy to **accidentally serialize** work that should run concurrently on the
current event loop.

The **`async`** macro (compiler name **`compile/async`**) is the standard
spelling for **fan-out / join** on the asyncio loop already owned by
**`drive_async_loop`**. It composes existing primitives — **`defer`**,
**`create-task`**, **`gather`** — rather than introducing new VM semantics.

This is **concurrent async scheduling**, not multicore parallelism. Jobs overlap
only when they **suspend** (I/O, timers, Python awaitables, explicit suspend).
CPU-bound Koskari with no suspension points still runs sequentially.

Ref: [defer-coroutine.md](defer-coroutine.md);
[../reference/concepts/demand-async-and-defer.md](../../docs/reference/concepts/demand-async-and-defer.md)
(user-facing ambient-async overview — do not link from `docs/` back here).

---

## Motivation

### The sequential trap

```koskari
(for-each [url urls]
  (fetch-one client url))
```

Each **`(fetch-one …)`** auto-awaits to completion before the next iteration
starts. For I/O-bound work this is correct sequentially but wastes concurrency.

Wrapping each call in **`(defer fn …)`** + **`create-task`** + **`gather`**
fixes launch semantics but is too easy to forget and too noisy to repeat.

### Relationship to existing primitives

| Primitive | Role |
| --------- | ---- |
| **`defer`** | Invoke at construction; hold awaitable without auto-await at the use site |
| **`create-task`** | Schedule on the current asyncio loop (`koskari.asyncio`) |
| **`gather`** | Join many tasks (proposed `@builtin` beside **`create-task`**) |
| **`async`** | User-facing macro; hides defer/task/gather wiring |

**`create-task`** is already registered when bootstrap loads **`koskari.asyncio`**
(via **`make_bootstrap`**). **`async`** belongs in bootstrap **`.ki`** (e.g.
**`_async.ki`**) at the same tier as **`compile/each`** and
**`compile/define-coroutine`**.

---

## Surface syntax (multi-form macro)

**`async`** follows the **`set!`** pattern: one macro name, several shapes
detected at expand time via **`syntax-rules`** literals and/or **`macro-match`**.

### Fixed job list

```koskari
(async
  (fetch-one client u1)
  (fetch-one client u2))
```

Expands to a **`gather`** of **`(create-task (defer …))`** forms — one per job.
No explicit **`await`** at the use site; **`gather`** is auto-awaited like other
Python asyncio calls.

### Bounded in-flight (`limit:`)

```koskari
(async limit: 100
  (fetch-one client u1)
  (fetch-one client u2)
  ...)
```

Each job is wrapped so a shared **`asyncio.Semaphore`** caps **in-flight**
work, not total queued jobs. Optional alias **`async/limit`** may desugar to
**`(async limit: N …)`**.

Runtime helper (name tentative **`with-limit`**):

```koskari
define with-limit [sem job ...]:
  (call-attr sem acquire)
  (try:
    (apply job)
   finally:
    (call-attr sem release))
```

Macro expansion wraps each job body as **`(with-limit sem ,@job)`** inside
**`(create-task (defer …))`**.

### Streaming jobs (`each`)

```koskari
(async (each [url urls]
  (fetch-one client url)))

(async limit: 100
  (each [url urls]
    (fetch-one client url)))
```

**Must not** splice O(n) **`create-task`** forms at macro-expand time for large
**`urls`**. Expands to a **single runtime call** (tentative **`async-each`**) that:

1. Iterates **`seq`** (generator / iterator from **`each`** or any iterable).
2. For each element, binds **`binding`**, spawns
   **`(create-task (defer …body…))`** (with optional semaphore).
3. Respects **`limit:`** as max in-flight: pull from the iterator as slots free.
4. Joins when all spawned work completes; returns results (shape TBD — list,
   or **`gather`** over accumulated tasks).

The macro only parses the inner **`(each [binding seq] body)`** shape (or an
equivalent **`[binding seq] body`** sugar) and emits **`async-each`**.

---

## Expansion summary

| User form | Expand strategy | Runtime |
| --------- | --------------- | ------- |
| **`(async job …)`** | Splice **`gather`** + **`create-task`** + **`defer`** | **`gather`** builtin |
| **`(async limit: N job …)`** | Same + shared **`Semaphore`** + **`with-limit`** per job | above + **`with-limit`** |
| **`(async (each …))`** | **`(async-each #f binding seq body)`** or limit variant | **`async-each`** |
| **`(async limit: N (each …))`** | **`(async-each N binding seq body)`** | **`async-each`** |

Future keywords (e.g. **`return_exceptions:`**) can follow the same literal
dispatch as **`limit:`**.

---

## Semantics and contracts

### Cooperative concurrency only

* One **`drive_async_loop`** thread; one asyncio loop.
* Overlap requires **suspension inside each job** before long CPU work
  completes.
* **`gather`** argument evaluation is left-to-right; a job that never suspends
  during **`defer`** construction may delay scheduling of later jobs in the
  fixed-list form. Streaming **`async-each`** avoids materializing all tasks
  before any I/O starts.

### Async-hosted only

**`create-task`** and **`gather`** require a running asyncio loop. **`async`**
is meaningful on the async-hosted path (CLI, **`load_module_async`**, apps
under **`drive_async_loop`**). On the **demand** path, **`async`** should fail
clearly if invoked (same class of error as misusing **`create-task`** on
demand).

### Explicit **`await`**

Usually unnecessary inside job bodies or at the **`async`** form itself.
Use **`(await …)`** only for **`defer`** handles and other intentional
non-auto-await sites (see user **`docs/reference/concepts/demand-async-and-defer.md`**).

---

## Implementation plan (proposed order)

1. **`gather`** — **`@builtin`** in **`koskari/asyncio.py`** (wrap
   **`asyncio.gather`**).
2. **`with-limit`** — bootstrap **`.ki`** helper or small Python helper in
   **`koskari/asyncio.py`**.
3. **`async-each`** — runtime in bootstrap **`.ki`** or **`koskari/asyncio`**
   module; semaphore + iterator loop + join.
4. **`compile/async`** — **`macro-match`** in **`koskari/bootstrap/_async.ki`**;
   add **`_async.ki`** to **`_BUILTINS_SOURCES`** after **`_functions.ki`**.
5. **Tests** — phase **`phase_basics`** or **`phase_reproducers`**:
   * two **`sleep`** jobs finish in ~one interval (fixed **`async`**);
   * **`async limit: 1`** serializes overlapping sleeps;
   * **`async (each …)`** over 100+ items without expand-time blow-up;
   * demand-path call raises (if testable).
6. **User docs** (when stable) — new page under **`docs/reference/`** (not
   **`design/`**); link from **`docs/api/koskari-asyncio.md`**.

---

## Non-goals (v1)

* Multicore / OS-thread parallelism inside **`async`** (separate future forms:
  executor, process pool, sharded **`drive_async_loop`**).
* Replacing **`create-task`** for fire-and-forget single tasks.
* Demand-path concurrent execution.
* **`asyncio.TaskGroup`** wrapper (can follow **`gather`** parity later).

---

## Open questions

* **Result shape** for **`async-each`**: list aligned with input order vs
  streaming callback vs lazy result iterator.
* **`return_exceptions:`** on fixed vs streaming forms — same keyword on both?
* **`async-each`** implementation: manual pending set + semaphore vs
  **`asyncio.TaskGroup`** (3.11+) vs batched **`gather`** chunks.
* Whether **`Semaphore`** is also exposed as **`@builtin`** for ad hoc use
  outside **`async`**.

---

## Related reading

* [defer-coroutine.md](defer-coroutine.md) — **`defer`** and auto-await exclusion
* [archive/koskari-scripts-packaging.md](../archive/koskari-scripts-packaging.md) —
  **`drive_async_loop`** host
* [../language/async.md](../language/async.md) — coroutine surface syntax rollup
* **`koskari/asyncio.py`** — **`create-task`**, **`sleep`**, **`coroutine-fn`**
