# AsyncGenerator asend / athrow / aclose

**Status**: proposed. Completes the Python-facing generator protocol on
`koskari.lib.AsyncGenerator`, closing the gap left open by the
dealloc-fence work (see the note in `asyncgen_dealloc`, iter.c).

**Depends on**:
[asyncgen-anext-await-underrun.md](../archive/asyncgen-anext-await-underrun.md)
(fixed) -- the point of an *async* close is that cleanup may suspend,
and a suspending drive step is exactly what the underrun defect broke.
With the asend stepper now driving in-flight awaits, `aclose` can build
on that contract.

---

## Scope

This is an outward-facing surface only. Koskari itself needs nothing
here: `iter` and `next` already cover both the synchronous and the
awaitable generator, and `asyncgen_tp_iter` hands a `DemandGenerator`
to the sync path. The work is what Python sees when it receives one of
our objects.

`collections.abc` membership is structural, so each method directly
buys library compatibility rather than just protocol tidiness
(`_collections_abc.py`):

- `AsyncGenerator` hook checks `__aiter__`, `__anext__`, `asend`,
  `athrow`, `aclose`.
- `Generator` hook checks `__iter__`, `__next__`, `send`, `throw`,
  `close`.

**Not in scope**: the introspection attributes (`ag_frame`, `ag_code`,
`ag_await`, `ag_running`, `__name__`, `__qualname__`). Every asyncio
access to those is `hasattr`-guarded (`asyncio/base_tasks.py`,
`asyncio/coroutines.py`), so their absence costs repr and
`Task.get_stack()` detail and nothing else. Deliberately left out.

## Current state

`KosPyAsyncGenType` (iter.c) declares no `tp_methods` at all -- only
`am_aiter` and `am_anext`. All three of `asend`, `athrow`, and `aclose`
are missing, so the type satisfies `abc.AsyncIterator` and stops there.
(`send` / `throw` / `close` exist on the *awaitable*,
`koskari.lib.async_generator_asend`, which is a different object.)

Two consequences beyond the ABC:

- `contextlib.aclosing` and `loop.shutdown_asyncgens()` raise
  AttributeError.
- `YIELD_FROM.c` classifies a foreign delegate as an async generator
  rather than a bare async iterator by `hasattr(obj, "asend")`, so a
  Koskari async generator that reaches yield-from through a Python
  round-trip classifies one tier down.

The original observation that "aclose kills the driver" was an
AttributeError walking into the (since fixed) exceptional unwind
teardown corruption. The method itself never existed.

## Staging

The methods are not equal work, and they split cleanly:

1. **`asend` and `athrow`** -- plumbing over the stepper that already
   exists. No new drive path, no new result mapping.
2. **`aclose`** -- the same stepper with a close-flavored result
   mapping.
3. **`DemandGenerator.close`** -- independent; a throw-then-classify
   wrapper in the `coroutine_close` shape.
4. **the `asyncgenasend_close` stub** -- independent defect fix on the
   awaitable, not on the generator.

## Stage 1: asend and athrow

`KosPyAsyncGenASend` already carries `is_throw`, `throw_typ`,
`throw_exc`, and its step loop forwards them into
`koskari_eval(ctx, send_val, throw_typ, throw_exc)`. The evaluator's
throw ingress delivers into the suspended context, where the dynchain's
TRY/FINALLY machinery gives catchability for free. So both methods are
constructors:

- **`asend(value)`** = `asyncgen_anext` with a value: the `closed`
  check, then `KosPyAsyncGenASend_New(agen, value)`.
- **`athrow(typ[, val[, tb]])`** = normalize the argument (the same
  `PyErr_NormalizeException` shape `asyncgenasend_throw` already uses),
  then construct a stepper with `is_throw` set and the throw fields
  primed. `KosPyAsyncGenASend_New` grows throw parameters, or gains a
  sibling constructor.

A freshly constructed stepper has `awaiting` clear, so the
resume-from-await branch is skipped and the first drive goes straight
to install plus `koskari_eval` with the throw in hand. No change to the
step loop.

### Semantics (PEP 525 / CPython parity)

`asend` matches `__anext__` in every respect but the sent value.

`athrow` drives the exception into the generator at its suspension
point:

- body catches and yields -> the yielded value is the result of
  awaiting the athrow.
- body catches and returns / falls off the end -> StopAsyncIteration.
- body does not catch -> the exception propagates to the awaiter.
- generator already closed / exhausted -> quiet completion: the
  awaitable raises bare StopIteration, so awaiting it yields None
  (CPython 3.12 behavior, taken from the parity oracle).
- generator currently running -> RuntimeError, via the existing
  `in_progress` guard.

## Stage 2: aclose

`aclose()` is `athrow(GeneratorExit)` with a close-specific result
mapping: a `closing` flag on the stepper (or a small subtype) that
branches the terminal switch in `asyncgenasend_iternext`.

The mapping is stated in awaitable terms, because "awaiting X yields
None" means raising `StopIteration(None)`, not `StopAsyncIteration`:

| eval result | normal stepper | closing stepper |
|---|---|---|
| `KOS_RES_YIELD` | StopIteration(value) | RuntimeError, "async generator ignored GeneratorExit" |
| `KOS_RES_RETURN` | StopAsyncIteration(value) | StopIteration(None) |
| `KOS_RES_RAISE`, GeneratorExit | propagates | StopIteration(None) |
| `KOS_RES_RAISE`, other | propagates | propagates |

Every terminal outcome marks `agen->closed`; `RETURN` / `RAISE` /
`FATAL` already do so today. `aclose` on an already-closed generator
returns an awaitable completing immediately with None (idempotent),
resolved at the method before any stepper is built.

Suspension during cleanup (`finally: (await ...)`) is honored: the
awaitable suspends like any anext step and the loop drives it. That is
the capability the demand-path teardown cannot offer, and the reason
`aclose` exists as a distinct path rather than an alias for teardown.

The GeneratorExit drive logic shared with `coroutine_close` (coro.c)
and `asyncgen_teardown_chain_on_ctx` (iter.c) should be factored where
practical, but the async stepper drives through the normal
suspend/resume protocol rather than the teardown's run-to-completion
loop -- they are siblings, not the same function.

### Interaction with the drive-state demand bit

aclose/athrow steppers are async boundary drives: the install assigns
invoker 0, so the demand bit is off and awaits inside `finally` blocks
suspend properly. No new flag work is needed; the drive-state model
covers this by construction.

### State machine

`closed` and `in_progress` on the agen are already the authority:

- `in_progress` guards re-entrancy for asend/athrow/aclose uniformly.
- terminal athrow/aclose outcomes set `closed`.
- `aclose` on `closed` -> immediate None; `athrow` on `closed` ->
  quiet None (bare StopIteration from the awaitable).
- dealloc behavior is unchanged: `KosPyAsyncGen_Teardown` remains the
  demand-path close for the drop-without-close case, exception-fenced
  per the dealloc-fence work.

## Stage 3: DemandGenerator.close

`DemandGenerator` exposes `__iter__`, `__next__`, `send`, and `throw`.
The single missing `close` is what keeps it out of `abc.Generator`, so
`contextlib.closing` and duck-typed generator handling reject it today.

`close` must drive GeneratorExit through the body so `finally` blocks
run -- it is not a teardown call. `demand_gen_step` already invokes
`demand_gen_close_ctx` (the `KosPyAsyncGen_Teardown` wrapper) on every
terminal path, so the method is throw-then-classify in the
`coroutine_close` shape (coro.c): throw GeneratorExit via the existing
`demand_gen_throw`; a value coming back means the body yielded ->
RuntimeError "generator ignored GeneratorExit"; StopIteration or
GeneratorExit -> cleared, return None; anything else propagates.
Already-closed is a no-op None. Not factored with `coroutine_close`:
different translation units, and sharing would cost a header export to
save a dozen lines -- comment the kinship instead. No dependency on
stages 1 or 2.

## Stage 4: the asyncgenasend_close stub

`asyncgenasend_close` (iter.c) sets `agen->closed = 1` and returns
None. That closes the *generator* as a side effect of dropping one
step's awaitable: `asyncio.wait_for(anext(agen), t)` timing out
permanently kills a generator that should remain usable, while the
generator's `finally` blocks never run for it either.

The fix operates on the stepper, not the generator: mark the stepper
spent, and release the in-flight ownership it may hold (an asend
abandoned mid-await owns `agen->in_progress` -- the same release
`asyncgenasend_dealloc` already performs). `agen->closed` is not
touched. Driving a spent stepper raises RuntimeError ("cannot reuse
already awaited __anext__()/asend()"), CPython's message.

One deliberate CPython deviation, decided by the parity oracle rather
than assumed: CPython does not release the running state when a
mid-flight asend is closed, so the generator wedges -- a later aclose
raises "asynchronous generator is already running" and the `finally`
blocks never get a sanctioned chance to run. We release `in_progress`
(consistent with our dealloc contract) so explicit cleanup keeps
working after an abandoned step.

## Phase 2 (separate, optional): finalizer hooks

PEP 525's `sys.set_asyncgen_hooks` firstiter/finalizer integration is
what lets asyncio schedule `aclose` for dropped generators
(`loop.shutdown_asyncgens`). Worth speccing only after aclose exists:
call the firstiter hook on first drive, stash the finalizer, and have
dealloc invoke it (falling back to today's demand-path teardown when no
hook is set). Not required for the explicit-close surface.

## Testing matrix

- asend: value delivered to the suspended yield; asend on a
  never-started generator (None-equivalent first step); asend on closed
  -> StopAsyncIteration.
- athrow caught -> yielded value returned; uncaught -> propagates;
  caught-then-return -> StopAsyncIteration.
- aclose: plain suspended gen -> None, closed; `finally` with an await
  inside -> cleanup runs, suspension honored (requires the underrun
  fix); body ignoring GeneratorExit (catch + yield) -> RuntimeError;
  idempotent second aclose -> None; aclose on never-started and on
  exhausted gens.
- re-entrancy: aclose/athrow while in_progress -> RuntimeError.
- cancelled step: close a pending `__anext__` awaitable and assert the
  generator is not marked closed -- a later aclose still runs its
  `finally` (the `asyncgenasend_close` regression).
- extent restore: parameterize inside the gen; aclose runs the unwind;
  a parameter read after close shows the extent restored (the test the
  original aclose investigation set out to write).
- interplay: aclose after partial flattened consumption; aclose after a
  demand step.
- conformance: `isinstance(agen, collections.abc.AsyncGenerator)` and
  `isinstance(dgen, collections.abc.Generator)`. (Yield-from
  classification is unaffected: `KosPyAsyncGen_Check` matches before
  the `hasattr(obj, "asend")` probe ever runs.)
- `contextlib.aclosing` and `contextlib.closing` smoke;
  `loop.shutdown_asyncgens` once phase 2 hooks exist.

## References

- iter.c: `KosPyAsyncGenType` (no `tp_methods`), `asyncgen_anext`,
  `KosPyAsyncGenASend` (`is_throw` plumbing), `asyncgenasend_iternext`
  (terminal switch), `asyncgenasend_close` (the stub),
  `KosPyAsyncGen_Teardown`, `asyncgen_dealloc` (fence + comment)
- demand.c: `demand_gen_methods`, `demand_gen_close_ctx`
- coro.c: `coroutine_close` (the sync close shape to mirror)
- YIELD_FROM.c: delegate classification by `hasattr(obj, "asend")`
- PEP 525 -- Asynchronous Generators
- [asyncgen-anext-await-underrun.md](../archive/asyncgen-anext-await-underrun.md)
