# Demand wrapper and call API

This document specifies the **Demand** wrapper type (Python-facing adapter for
Koskari callables), the split between **call** and **apply** entry points
(renaming `demand_sync`), and bootstrap bindings for `.ki` code.

**Implementation status**: Implemented in `koskari/lib/demand.c`. `koskari.lib`
exposes the wrapper type as **`demand`** (constructor), plus `demand_call` and
`demand_apply`; Ki bootstrap binds the name **`demand`** to that type.
`demand_sync` remains available as a deprecated alias for `demand_call`.

---

## 1. Goals

1. **Python interop**: Hand Koskari callables to Python APIs (`map`, `starmap`,
   sorting keys, third-party libraries) that expect a normal `tp_call` object
   with useful metadata (`__doc__`, `__name__`, annotations, optional
   `__wrapped__`).

2. **Single demand execution path**: Wrapper `__call__` and the explicit
   primitives share the same underlying implementation as today’s
   `demand_sync_ex` / host bridge (no suspension; yielding functions yield
   `DemandSyncGenerator`; `KoskariSyncViolation` on illegal suspend).

3. **Clear names**: Reserve **demand** for the wrapper *constructor* (or type),
   and expose **call** vs **apply** primitives explicitly in both Python and Ki.

4. **No partial-style pre-binding on the wrapper**: The `Demand` instance holds
   only the inner callable. Callers that need deferred arguments use thunks
   (`{ ... }` / `compile/gensym-thunk`) or explicit `demand-call` / `demand-apply`
   at the use site.

---

## 2. Motivation

Today, `demand_sync(callable, *args, **kwargs)` is the host primitive. Ki
bootstrap binds the name **`demand`** to that function, so `(demand expr)` means
“invoke now.”

For **adapter** use cases, `basics.ki` defines **`demanding`** as
`(wraps fn) (partial demand fn)`, i.e. `functools.partial` around the sync
primitive. That works but:

- `partial` plus a C function is awkward for tooling that builds signatures.
- The word **demand** in user mental model should mean “this object always runs
  on the demand path when Python calls it,” not necessarily “invoke this
  expression right now.”

A small **`Demand`** type (implemented in C alongside `demand.c`, or a thin Python
type delegating to C) can:

- Expose `__call__(self, /, *args, **kwargs)` → `demand_call(self.__wrapped__, args, kwargs)` (or equivalent internal call).
- Hold **`__wrapped__`** as a native member (read-only) pointing at the Koskari (or plain Python) callable.
- Expose **`__annotations__`**, **`__doc__`**, **`__name__`**, **`__qualname__`**, **`__module__`** via getters that read through to `__wrapped__` (no copy at construction).
- Optionally implement **`__signature__`** or rely on `inspect.signature` via `__wrapped__` for the inner callable (see [inspect-interop-kos-callables.md](inspect-interop-kos-callables.md)).

**demanding** then becomes: wrap `fn` in `Demand(fn)` with
no `partial`.

---

## 3. Python API

### 3.1 `demand_call`

- **Definition**: Same behavior as today’s `demand_sync`.
- **Name**: `demand_call` in `koskari.lib` (and public re-export policy per
  [package-layout.md](package-layout.md)).
- **Legacy**: Keep `demand_sync` as a **deprecated alias** for `demand_call` for
  one or more release cycles (C implementation may keep internal
  `demand_sync_ex` name unchanged).

Signature (conceptual):

```text
demand_call(callable, /, *args, **kwargs) -> result | DemandSyncGenerator | raises
```

### 3.2 `demand_apply`

- **Definition**: `demand_apply(callable, args, kwargs=None)` where `args` is a
  sequence (e.g. tuple) and `kwargs` is a mapping or `None`. Semantically
  equivalent to `demand_call(callable, *args, **kwargs)` without requiring the
  caller to splat. Aligns with the existing Ki **`apply`** primitive pattern.

Implementation should share the same core as `demand_call` (single internal
entry after normalizing `args`/`kwargs` to the structures `demand_sync_ex`
already expects).

### 3.3 `Demand` type

- **Constructor**: `Demand(callable)` — stores callable, no bound arguments.
- **`__call__`**: forwards to `demand_apply(__wrapped__, args, kwargs)`.
- **Introspection**: forward metadata from `__wrapped__` as described in §2.
- **Type name**: e.g. `koskari.lib.Demand` (exact `tp_name` TBD).

Optional: class method or module-level **`demand`** factory alias to `Demand`
for symmetry with Ki.

---

## 4. Ki bootstrap bindings

In `koskari.importer.bootstrap.make_bootstrap()` (and any mirror lists in docs):

| Ki name        | Python value    | Role |
| -------------- | --------------- | ---- |
| `demand-call`  | `demand_call`   | Immediate varargs/kwargs invoke (replaces today’s use of `demand` for this). |
| `demand-apply` | `demand_apply`  | Apply form: `demand_apply(callable, args, kwargs)` when args and kwargs are already in a tuple and dict (same synchronous demand path as `demand-call`). |

| Ki name   | Python value | Role |
| --------- | ------------ | ---- |
| `demand`  | `Demand` (type) or factory | Build adapter objects for Python consumers; **not** the old “call now” primitive. |

**Migration**: Every existing `(demand x)` in `.ki` sources becomes
`(demand-call x)` (and similarly for calls with arguments). Grep for `\bdemand\b`
in `*.ki` when implementing.

---

## 5. Related surfaces to update

- **`koskari/importer/basics.ki`**: `demanding` — stop using `partial`; use
  `Demand` + metadata.
- **`koskari/importer/module.py`**, **`importlib.py`**, docs that say
  `demand_sync(expr)` → `demand_call(expr)` in prose; loader code imports
  updated symbol.
- **`koskari/types.py`**: `as_coroutine` lives here for bootstrap/asyncio
  adaptation (the old `koskari._sync` module was removed).
- **C layer** (`demand.c`): optionally rename public Python methoddef only;
  internal `demand_sync_ex` can stay to minimize churn.
- **Tests**: `tests/lib/test_demand_sync.py` → extend or rename to cover
  `demand_call`, `demand_apply`, and `Demand`; keep tests for deprecated
  `demand_sync` alias until removed.
- **Design cross-links**: [dunder-adapter.md](dunder-adapter.md) should refer to
  `demand_call` as the user-facing name while noting the historical
  `demand_sync` name.

---

## 6. Non-goals (this change)

- Koskari-native `map` / `starmap` (future; wrapper is the stdlib bridge for
  now).
- `Demand` holding partial-applied arguments (use thunks or call
  `demand-call` / `demand-apply` directly).
- Async `demand_next` / full slot matrix (remains per dunder-adapter.md).

---

## 7. Acceptance criteria

- `Demand(fn)(*args, **kwargs)` matches `demand_call(fn, *args, **kwargs)` for
  representative Koskari functions, expressions, and plain Python callables.
- `demand_apply(fn, args, kw)` matches splatted `demand_call`.
- Ki bootstrap exposes `demand`, `demand-call`, `demand-apply` as in §4; module
  loader and tests updated.
- Documentation and design indexes reference this file.

# The end.
