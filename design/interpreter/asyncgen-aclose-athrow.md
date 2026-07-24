# AsyncGenerator aclose / athrow

**Status**: proposed. Closes the async-generator protocol gap left open
by the dealloc-fence work (see the note in `asyncgen_dealloc`, iter.c):
`koskari.lib.AsyncGenerator` implements `__aiter__` / `__anext__` /
`asend` but has no `aclose` or `athrow`, so it does not fulfill PEP 525
and cannot cooperate with the standard cleanup idioms.

**Depends on**:
[asyncgen-anext-await-underrun.md](../archive/asyncgen-anext-await-underrun.md)
(fixed) -- the point of an *async* close is that cleanup may suspend,
and a suspending drive step is exactly what the underrun defect broke.
With the asend now driving in-flight awaits, `aclose` can build on that
contract.

---

## Motivation

- **Protocol completeness**: `async for ... finally aclose()`,
  `contextlib.aclosing`, and `loop.shutdown_asyncgens()` all assume
  `aclose`/`athrow` exist. Today they get AttributeError.
- **The suspending close**: dealloc teardown (`KosPyAsyncGen_Teardown`)
  is deliberately the *demand-path* close -- GeneratorExit driven
  synchronously to completion, suspend attempts taking the
  cannot-honor fallback. That is correct for dealloc, which can run
  mid-unwind. An explicit `aclose` driven by a loop is the sanctioned
  place where `finally: (await ...)` inside a generator can actually be
  honored.
- **History**: the original observation that "aclose kills the driver"
  was an AttributeError walking into the (since fixed) exceptional
  unwind teardown corruption. The method itself never existed.

## Semantics (PEP 525 / CPython parity)

### athrow(exc)

Returns an awaitable (an ASend-flavored stepper). Driving it throws
`exc` into the generator at its suspension point:

- body catches and yields -> the yielded value is the result of
  awaiting the athrow.
- body catches and returns / falls off the end -> StopAsyncIteration.
- body does not catch -> the exception propagates to the awaiter.
- generator already closed / never started and exhausted ->
  StopAsyncIteration (CPython: athrow on a closed agen raises
  StopAsyncIteration, except GeneratorExit which returns quietly).
- generator currently running -> RuntimeError (re-entrancy guard, same
  as `asend`).

### aclose()

Returns an awaitable. Semantically `athrow(GeneratorExit)` with the
close-specific result mapping:

- GeneratorExit propagates out (uncaught) or the body returns ->
  success; awaiting aclose yields None; generator marked closed.
- body catches GeneratorExit and *yields* -> RuntimeError
  ("async generator ignored GeneratorExit"); generator marked closed.
- body raises some other exception -> it propagates to the awaiter.
- already closed -> immediate None (idempotent).
- suspension during cleanup (`finally: (await ...)`) -> honored: the
  awaitable suspends like any anext step and the loop drives it. This
  is the capability the demand-path teardown cannot offer.

## Design

### Machinery: extend the ASend stepper

`KosPyAsyncGenASend` (iter.c) already carries `is_throw`, `throw_typ`,
`throw_exc` and its step loop already forwards throws into
`koskari_eval(ctx, send_val, throw_typ, throw_exc)` -- the evaluator's
throw ingress delivers into the suspended context, where the dynchain's
TRY/FINALLY machinery gives catchability for free. So:

- **athrow(exc)** = `KosPyAsyncGenASend_New(agen, NULL)` with the throw
  fields primed. Mostly plumbing: a `m_athrow` method normalizing its
  argument and constructing the stepper.
- **aclose()** = a close-flavored stepper: same drive, GeneratorExit as
  the thrown exception, plus the close result mapping (a `closing` flag
  on the stepper or a small subtype): YIELD -> RuntimeError instead of
  a value; RETURN / GeneratorExit-raise -> None; mark `agen->closed`
  on every terminal outcome.

The GeneratorExit drive logic shared with `coroutine_close` (coro.c)
and `asyncgen_teardown_chain_on_ctx` (iter.c) should be factored where
practical, but the async steppers drive through the normal
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
  StopAsyncIteration (GeneratorExit -> None).
- dealloc behavior is unchanged: `KosPyAsyncGen_Teardown` remains the
  demand-path close for the drop-without-close case, exception-fenced
  per the dealloc-fence work.

### DemandGenerator surface

`DemandGenerator` already has `throw` (demand_gen_throw) and its close
path (`demand_gen_close_ctx`) rides `KosPyAsyncGen_Teardown`. A sync
`close()` method for generator-protocol parity is a one-liner over the
same teardown; include it for symmetry, or defer -- no dependency
either way.

### Phase 2 (separate, optional): finalizer hooks

PEP 525's `sys.set_asyncgen_hooks` firstiter/finalizer integration is
what lets asyncio schedule `aclose` for dropped generators
(`loop.shutdown_asyncgens`). Worth speccing only after aclose exists:
call the firstiter hook on first drive, stash the finalizer, and have
dealloc invoke it (falling back to today's demand-path teardown when no
hook is set). Not required for the explicit-close surface.

## Testing matrix

- athrow caught -> yielded value returned; uncaught -> propagates;
  caught-then-return -> StopAsyncIteration.
- aclose: plain suspended gen -> None, closed; `finally` with an await
  inside -> cleanup runs, suspension honored (requires the underrun
  fix); body ignoring GeneratorExit (catch + yield) -> RuntimeError;
  idempotent second aclose -> None; aclose on never-started and on
  exhausted gens.
- re-entrancy: aclose/athrow while in_progress -> RuntimeError.
- extent restore: parameterize inside the gen; aclose runs the unwind;
  a parameter read after close shows the extent restored (the test the
  original aclose investigation set out to write).
- interplay: aclose after partial flattened consumption; aclose after a
  demand step.
- `contextlib.aclosing` and `loop.shutdown_asyncgens` smoke once
  phase 2 hooks exist.

## References

- iter.c: `KosPyAsyncGenASend` (`is_throw` plumbing), `asyncgen_anext`,
  `KosPyAsyncGen_Teardown`, `asyncgen_dealloc` (fence + comment)
- coro.c: `coroutine_close` (the sync close shape to mirror)
- PEP 525 -- Asynchronous Generators
- [asyncgen-anext-await-underrun.md](../archive/asyncgen-anext-await-underrun.md)
