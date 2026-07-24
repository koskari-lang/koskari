# AsyncGenerator anext: await suspension underruns the operand stack

**Status**: defect, diagnosed to the failing drive path; root cause in
the placeholder/suspension accounting not yet pinned. No fix attempted.

---

## Observed

A single `await agen.__anext__()` from Python fails with
`RuntimeError: stack pop underrun` (guard at `stack.c:465/475`) whenever
the generator step being driven contains an `await`:

```
define g-await-first []:
    (await (sleep 0.001))
    (yield "one")
```

```python
g = g_await_first()
await g.__anext__()        # RuntimeError: stack pop underrun
```

The step boundary is what matters, not the first drive:

```
define g-yield-then-await []:
    (yield "one")
    (await (sleep 0.001))
    (yield "two")
```

anext #1 (plain yield step) succeeds; anext #2 (the step containing the
await) underruns.

This was first observed as a "demand -> async alternation" failure (a
DemandGenerator step followed by `__anext__` on the same agen). That
characterization was wrong: the demand step merely advanced the
generator so that the *next* anext step contained the await. The
minimal reproducer needs no demand generator, no alternation, and no
resume -- one anext over one awaiting step.

## What works (the containment that hid it)

- **Flattened iteration** (koskari `for-each` / `list-each` /
  `yield-from` over the agen): awaits inside the generator suspend the
  *consumer's* context via ambient async and resume correctly. All
  in-tree `.ki` consumption takes this path.
- **anext over non-awaiting steps**: plain yields drive correctly from
  Python, including multi-step iteration.
- **Demand drives**: awaits are denied in-eval (KoskariSyncViolation),
  so the failing leg is never exercised.
- **Coroutines** (defer): awaits suspend and resume correctly; the
  failing machinery is specific to the asyncgen asend drive.

Python-side `async for` over a koskari generator that awaits is the
first consumer to walk the broken leg, which is why the suite never
saw it.

## Drive-path anatomy (where to look)

The asend step loop is `asyncgenasend_iternext` (iter.c):

1. `install_frozen_gen_for_resume(agen->ctx, agen)` -- installs only
   when the gen is *frozen* (`GEN.active` holds the chain). Two cases
   skip the install entirely: the bootstrap first drive (the chain has
   lived on `agen->ctx` since construction) and resume-from-AWAIT (see
   below). When the install runs, `kos_dyn_gen_install` **pushes a
   `Py_None` placeholder** onto the operand stack; the YIELD revive
   path pops it (`eval.c:332` documents the contract).
2. `koskari_eval(ctx, send_val, throw_typ, throw_exc)`.
3. Result translation: `KOS_RES_YIELD` wraps the value in
   StopIteration (completing the `__anext__` awaitable);
   `KOS_RES_AWAIT` returns the raw awaitable to the driving loop, which
   awaits it and re-enters via `asend.send(result)`.

The suspension asymmetry is the suspect region:

- **YIELD** freezes the generator out (`kos_dyn_gen_freeze_and_release`
  -- chain moves into `GEN.active`, ctx dyn_top cleared). The next
  drive re-installs (placeholder pushed) and revives-from-YIELD
  (placeholder popped, send value pushed). Balanced.
- **AWAIT** suspends at *context level*: the chain stays on the ctx,
  the gen is not frozen, `py_await_iter` machinery holds the in-flight
  await. The re-entry (`asend.send(result)`) skips the install
  (nothing frozen) and revives-from-AWAIT (`revive_await` injects the
  result). No placeholder is pushed or popped on this leg.

A step that mixes the two -- revive-from-YIELD (pop placeholder), run,
suspend on AWAIT, re-enter (no install), revive-from-AWAIT, then reach
the next YIELD -- is exactly the failing shape, and the underrun
surfaces at or after that second YIELD's exit. The accounting between
the install placeholder protocol and the two revive flavors does not
balance across a mixed-suspension step. The bootstrap variant
(await-first) fails the same way with no placeholder ever pushed,
which suggests the imbalance is on the pop side of the yield exit or
the await revive, not the install push.

## Fix direction

Instrument one failing step with a debug build (`VERBOSE_DEBUG=1`; the
`[gen]`/`[try_local]` SAY channels already narrate install, freeze, and
revive) and diff the stack depth at each boundary against the working
plain-yield step. Candidate repairs, pending the trace:

- make the yield-exit/freeze path's stack expectations explicit per
  suspension history rather than assuming the placeholder is present;
- or push the placeholder unconditionally at drive entry (not only at
  install) so both revive flavors see one contract.

Whatever the repair, the reproducer matrix is already written above:
await-first single anext, yield-then-await second anext, plain-yield
control, and the flattened/demand paths as no-regression pins. The
mischaracterized note in tests/reproducers/test_demand_gen_await.py
should be corrected when this lands.

## References

- iter.c: `asyncgenasend_iternext`, `install_frozen_gen_for_resume`
- dyn_gen.c: `kos_dyn_gen_install` (placeholder push),
  `kos_dyn_gen_freeze_and_release`
- eval.c: `revive_await`, revive-from-YIELD (placeholder pop, ~332)
- stack.c:465/475: the underrun guards
