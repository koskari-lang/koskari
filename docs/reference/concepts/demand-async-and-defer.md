# Demand, async evaluation, and defer

Koskari bytecode runs on an **async-native** evaluator: many Python calls that
return coroutines or awaitables are **auto-awaited** at call boundaries. How you
**host** that evaluator (with or without an asyncio event loop) is a separate
choice. This page ties together **demand evaluation**, **async evaluation**,
**defer**, and related forms.

## Async-native evaluation

At runtime, Koskari code can suspend and resume (await Python awaitables,
`yield`, async iteration, and similar). Ordinary calls into Python often finish
with the awaited result on the stack, so you rarely write explicit
[`await`](../specials/compile-await.md) for those paths.

Coroutines, async generators, and `with` / `dyn-with` on async context managers
follow the same model. See [compile/await](../specials/compile-await.md) and the
[VM opcode reference](../vm/index.md) for opcode-level detail.

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

## Async evaluation (module load and REPL)

**Async evaluation** loads or runs code under an asyncio-driven host: each
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

Importer details: [`koskari.importer`](../../api/koskari-importer.md),
[`koskari.module`](../../api/koskari-module.md).

## defer

A **defer** is a single-shot await handle (Python builtin `defer`, also exposed
in bootstrap). The wrapped call runs when the defer is **constructed**; the VM
does **not** auto-await the defer value at the next use site.

That lets you pass the handle around and choose when to wait:

```koskari
;; defer was used so d is not auto-awaited at the call that produced it
(await d)
```

Use explicit [`(await expr)`](../specials/compile-await.md) when `expr` is a defer
or any other awaitable you must suspend on at that program point. See
[`koskari.lib`](../../api/koskari-lib.md) for the C/Python type surface.

Bare **`(await)`** with no argument performs a cooperative suspend on the
async path; on the demand path it triggers **`KoskariSyncViolation`** (see
**on-demand?** below).

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
