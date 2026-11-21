# KosPartial: KosCallable-aware partial application

This document records **KosPartial**, a native KosCallable that reproduces the
behavior of `functools.partial`. When koskari bytecode calls a KosPartial whose
inner callable is a flattenable KosCallable, the CALL opcode dispatches through
the install-slot fast path — no `KosContext` allocation, no nested
`koskari_eval`.

**Implementation status:** Implemented and archived (2026-06-27). See
`koskari/lib/partial.c`, `koskari/lib/partial.h`; init wired in
`koskari/lib/lib.c`; tests in `tests/lib/test_partial.py` and
`tests/native/test_partial.c`.

**Audience:** maintainers reviewing or extending this change.

**Related:**
- [kosproperty.md](kosproperty.md) — same stdlib-behavior family (`property` +
  auto-`Demand` on accessors)
- [demand-evalex-phase2.md](demand-evalex-phase2.md) — `KosDemand` as
  KosCallable (the install-slot flattening pattern this reuses)
- [../api/demand-wrapper.md](../api/demand-wrapper.md) — Demand wrapper API

---

## 1. What was implemented

`KosPartial` is a **standalone static `KosCallableTypeObject`** with its own
instance storage (`func`, `args`, `keywords`, plus `__dict__` and weakref
support). It reproduces `functools.partial`'s behavior:

- construction `KosPartial(func, *args, **keywords)` (callable check; at least
  one argument);
- nested flattening — `KosPartial(KosPartial(f, a), b)` collapses to a single
  partial over `f`;
- `tp_call` merges bound + call-site args and invokes `func`;
- `repr`, `__reduce__` / `__setstate__` (pickling), instance attributes.

It is **not** in `functools.partial`'s inheritance tree, so
`isinstance(x, functools.partial)` is `False`. The match is by duck typing
only. See §3 for why.

Because it is a static `KosCallableTypeObject` whose `tp_base` is
`KosCallableType`, `KosCallable_Check` passes and koskari bytecode calls
dispatch straight through the install slot.

## 2. The KosCallable fast path

- `tp_kos_wants_frame_fn` (`kospartial_wants_frame`) delegates to the inner
  callable's wants-frame slot, so the partial wants a frame exactly when its
  `func` is a flattenable KosCallable.
- `tp_kos_install` (`kospartial_kos_install`):
  - **flattenable inner:** merge `args` + call args and `keywords` + call
    kwargs, then `KosCallable_CallArgs` through to `func`, **forwarding
    `tail_call` unchanged**. `do_frame_prep` already prepared the frame using
    this partial's `wants_frame` (which delegates to `func`), so `func`'s
    install slot fills exactly that one frame. One frame regardless of nesting.
  - **otherwise:** invoke via the Python call path (`kospartial_call` merges)
    and push the result, mirroring the plain-Python call fallback.

This mirrors `demand_kos_install`. Note: the `tail_call` is forwarded, **not**
forced to `1` — an earlier draft proposed unconditional `tail_call=1`, which
would risk a double `kos_dynframe_reset_for_tail` on a freshly pushed frame.

## 3. Why standalone (and not an isinstance match)

The original design called for a C-level **subtype** of `functools.partial`, so
that `isinstance` would hold. That route is blocked on the Pythons koskari
targets:

- **A static type cannot subclass a heap type.** `type_ready_post_checks`
  requires a statically-allocated type's entire MRO to be static. In **3.12+**,
  `functools.partial` is a **heap type** (`PyType_FromModuleAndSpec` in
  `_functoolsmodule.c`). It was a static `PyTypeObject` in ≤3.11 and was
  converted to a heap type in 3.12 as part of the `_functools` module-state
  isolation work. So a static `KosPartialCallableType` subtyping it fails at
  `PyType_Ready` on 3.12+ (it would have compiled on 3.9–3.11 — a latent
  cross-version break).

- **A heap KosPartial would break slot dispatch.** The KosCallable fast path
  reads slots via `(KosCallableTypeObject *) Py_TYPE(obj)` at a **fixed offset**
  (immediately after `PyTypeObject`). That layout only holds for *static* type
  objects; a heap type is a `PyHeapTypeObject` (with `as_async` / `as_number`
  exactly where `tp_kos_install` would be read). Supporting a heap KosCallable
  would require relocating slot storage (a static mixin base + MRO-walk
  resolver, a `tp_dict` capsule, or a KosCallable metaclass) — a hot-path
  change for all callables.

No koskari code does `isinstance(x, functools.partial)` (the stdlib name is only
imported as a utility in `koskari/types.py` and `koskari/reader/parser.py`).
Trading that compatibility away buys a fully static, fast, self-contained type
with no dispatch rework. That is the deliberate choice recorded here.

## 4. Future direction (not done)

A more general alternative was scoped but not built: a **`__call__`-dispatch
optimization** in the evaluator. The CALL path would recognize an object whose
`__call__` resolves to a KosCallable, inject `self`, and install that method
directly — inlining any callable koskari instance (functors, decorators, a
pure-koskari `partial` subclassing `functools.partial` for `isinstance`). It
needs a koskari-owned sentinel `tp_call` (not demand-wrapped, to preserve async)
plus self-injection in `KosEvalState_Call`. If `isinstance(functools.partial)`
compatibility is ever required, that is the path — it composes with, and does
not replace, the standalone `KosPartial` recorded here.

## 5. File index

| File | Change |
| ---- | ------ |
| `koskari/lib/partial.h` | `KosPartialCallableType`; `KosPartial_CheckExact`; init/clear decls |
| `koskari/lib/partial.c` | standalone type, merge helpers, install slot, behavior |
| `koskari/lib/lib.c` / `lib.h` | wire `koskari_partial_init` / `_clear` |
| `tests/lib/test_partial.py` | behavior, dispatch, pickle, GC, non-isinstance |
| `tests/native/test_partial.c` | C-level memory / refcount tests |

# The end.
