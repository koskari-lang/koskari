# Foreign-awaitable exceptions dropped on await-resume

**Status:** open defect, reproduced and root-caused; fix proposed, not applied.

When a Koskari coroutine `await`s a *foreign* (Python/asyncio) awaitable that
**suspends to the real event loop and then raises**, the exception is not
re-injected at the `await` point. It escapes the Koskari frame entirely,
bypassing every enclosing `try/except` (including a bare `except [Exception]`).
The coroutine is marked done and the raw `PyErr` propagates out to whatever is
driving the loop.

The return-value path works. A foreign awaitable that suspends and then
*returns* delivers its value into the Koskari frame correctly. Only the
raise-after-suspension path is broken.

**Audience:** evaluator maintainers. Assumes familiarity with `koskari/lib/eval.c`
(the stackless evaluator, `KOS_SUSP_AWAIT`, `process_raise`, `revive_await`) and
`koskari/lib/coro.c` (the `KosCoroutine` object and its `send`/`throw` methods
that asyncio drives).

**Related:**
- Evaluator model: [evaluator.md](evaluator.md); [boundary-control-reemit.md](boundary-control-reemit.md).
- Surfaced by: `design/cli-repl-rework.md` (the REPL prompt read had to route
  EOF/interrupt through Python sentinels because of this bug).
- Raise-path unification that fixed the *sibling* path but not this one: PR #79
  (`f2ccbb58` unify RAISE ingress on `process_raise`; `e2d8b81d` migrate
  `revive_await` to `KosOpResult`; `8f37beb5` delete
  `kos_eval_emit_raise_from_pyerr`).

---

## 1. Symptom

From Koskari, under the production driver `drive_async_loop` (a real
`asyncio.new_event_loop()`; see `koskari/lib/async_loop.c`):

```koskari
define try_await [make_coro]:
  try:
    define r (await (make_coro))
    f"ok:{r}"
  except [EOFError]:
    "caught-eof"
  except [Exception as: e]:
    f"caught-other:{(attr (type e) __name__)}"
```

If `make_coro` returns a Python coroutine that raises `EOFError` after a loop
suspension, `try_await` does **not** return `"caught-eof"`. The `EOFError`
unwinds past both `except` clauses and out of the coroutine.

---

## 2. Minimal reproducer

Two files. The `.ki` module is compiled through the import hook; the test drives
it under `drive_async_loop`, exactly as `main()` drives the CLI.

`koskari/_exp_await.ki` (temporary; delete after use):

```koskari
"""Experiment: Koskari awaiting a foreign, loop-suspending Python coroutine."""

define try_await [make_coro]:
  try:
    define r (await (make_coro))
    f"ok:{r}"
  except [EOFError]:
    "caught-eof"
  except [Exception as: e]:
    f"caught-other:{(attr (type e) __name__)}"
```

(Add `koskari.cli = *.ki` is not needed; `koskari = *.ki` in `setup.cfg`
already packages a top-level `koskari/*.ki`. A tox env recreate is required so
the new `.ki` is copied into site-packages.)

`tests/.../test_exp.py`:

```python
import asyncio
from koskari.importer import install
from koskari.lib import drive_async_loop


def _exp():
    install()
    import koskari._exp_await as exp
    return exp


async def _ok():
    await asyncio.sleep(0)          # suspend to the loop, then return
    return 42


async def _boom():
    await asyncio.sleep(0)          # suspend to the loop, then raise
    raise EOFError


async def _boom_nosuspend():
    raise EOFError                  # raise WITHOUT suspending


def test_value():                  # PASS
    exp = _exp()
    async def d(): return await exp.try_await(lambda: _ok())
    assert drive_async_loop(d()) == 'ok:42'


def test_exc_after_suspend():      # FAIL: EOFError escapes
    exp = _exp()
    async def d(): return await exp.try_await(lambda: _boom())
    assert drive_async_loop(d()) == 'caught-eof'


def test_exc_no_suspend():         # PASS
    exp = _exp()
    async def d(): return await exp.try_await(lambda: _boom_nosuspend())
    assert drive_async_loop(d()) == 'caught-eof'
```

---

## 3. Diagnostic matrix

| Case | Awaitable behavior | Result |
|---|---|---|
| Native suspension | `(await (sleep 0))` then return | value delivered ✅ |
| Foreign value | `await asyncio.sleep(0); return 42` | value delivered ✅ |
| Foreign raise **after** suspend | `await asyncio.sleep(0); raise EOFError` | exception **escapes** `try/except` ❌ |
| Foreign raise **without** suspend | `raise EOFError` | caught ✅ |

The discriminator is a **single loop suspension** between the `await` and the
raise. Raise-before-first-suspension is caught; raise-after-resume is not.

This also retro-explains two earlier observations while wiring the Koskari REPL:
- `_await_user_form` raising `REPLCodeBoundary` *was* catchable, because it only
  `await`s a **native** Koskari awaitable (driven inside `koskari_eval`, no
  real-loop suspension) before raising.
- `_read_line` awaiting `loop.run_in_executor(...)` **lost** `EOFError`, because
  the executor future suspends to the loop; the resume carries the exception.
  That is why `_read_line` now catches EOF/interrupt in Python and returns the
  `_READ_EOF` / `_READ_INTERRUPT` sentinels instead of letting Koskari catch.

---

## 4. Root cause

There are **two** code paths that resume a Koskari coroutine which is parked on
a foreign Python awaitable (`KOS_CORO_WAITING_PY`, i.e. after an `EVAL_AWAIT`
suspension recorded `ctx->py_await_iter`):

1. **In-evaluator resume** — `revive_await` in `koskari/lib/eval.c`
   (called from the `KOS_SUSP_AWAIT` resume sites around eval.c:1987/2013/2027).
   It forwards `send`/`throw` to the inner await iterator and, **when the inner
   iterator raises a non-`StopIteration` exception, returns `EVAL_RAISE()`** so
   the raise re-enters `process_raise` and runs the frame's handler chain at the
   `await` point. Correct.

2. **Coroutine-object resume** — `coroutine_send` / `coroutine_throw` in
   `koskari/lib/coro.c`. This is what **asyncio drives** (`.send()` / `.throw()`
   via `tp_as_async`). Its `KOS_CORO_WAITING_PY` branch forwards to the inner
   iterator directly, *without* going back through `koskari_eval`, and on inner
   exception it bails out:

   ```c
   /* koskari/lib/coro.c, coroutine_send, WAITING_PY branch */
   if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
       /* success: fetch value, fall through to koskari_eval(send_val) */
       ...
   } else {
       /* Exception from Python awaitable */
       KosEvalState_PythonAwaitClearIter(&coro_es);
       coro->state = KOS_CORO_DONE;
       return NULL;                 /* <-- BUG: raw PyErr propagates out */
   }
   ```

   The success case falls through to `koskari_eval(coro->ctx, send_val, ...)`,
   which resumes the frame at the `await` point (value delivered, `try/except`
   intact). The exception case just clears the iterator, marks the coroutine
   `DONE`, and returns `NULL` with the inner `PyErr` still set. It never
   re-enters `koskari_eval` to raise the exception *at the await point*, so the
   frame's handler chain never runs. `coroutine_throw` has the identical defect
   in its own `WAITING_PY` branch.

Under `drive_async_loop`, asyncio resumes our coroutine via `.send()`, so path
(2) is the one exercised. Path (1) (`revive_await`) is correct but is only used
for evaluator-internal await drives, which is why the bug is invisible to any
test that does not route through the asyncio loop.

**The core issue:** the two resume paths duplicate "forward to inner await
iterator," and only path (1) converts an inner raise into a frame-level raise.
Path (2) drops it.

---

## 5. Impact

- **Any Koskari code that `await`s a Python coroutine/future that can raise**
  cannot catch that exception once a loop suspension has occurred. `try/except`
  is silently ineffective across that boundary.
- **REPL:** the whole reason the prompt read lives in a Python `_read_line`
  shim that converts EOF/interrupt/cancel to sentinels
  (`design/cli-repl-rework.md`). Fixing this bug removes the need for that shim
  and unblocks driving `prompt_toolkit`'s `prompt_async` directly from Koskari
  on the shared loop (cooperative, single-threaded, no worker thread).
- **Cancellation:** `asyncio.CancelledError` delivered to a suspended Koskari
  coroutine takes the same dropped path, so Koskari `try/finally` cleanup around
  an `await` may not run on cancellation. (Not yet independently reproduced;
  flagged because it rides the same branch.)

---

## 6. Regression analysis (honest assessment)

The user's hypothesis was that this is a regression from the recent
exception-path work. The evidence is mixed and worth recording precisely rather
than assuming.

**Against "freshly introduced by the June/July work":** `git blame` puts the
defective `else { ... return NULL; }` branch in `coroutine_send` at commit
`f1d6a827` (2026-04-27, *"KosEvalState suspension/Python bridge; eval_state.c;
coro and opcodes"*) — the commit that introduced the Python-await bridge. So the
branch has behaved this way since the bridge was born. On that evidence it is a
**long-standing latent defect**, not a diff introduced by the recent raise-path
commits.

**For "the recent work is entangled":** the June 23 raise-path migration fixed
the *sibling* path and not this one. `e2d8b81d` migrated `revive_await` to
`KosOpResult` (its inner-raise case returns `EVAL_RAISE()` — correct), and
`8f37beb5` deleted `kos_eval_emit_raise_from_pyerr` as having "no remaining
callers." That helper's name is exactly "turn a pending `PyErr` into a Koskari
raise event" — the operation `coroutine_send`/`coroutine_throw` should perform
here and does not. So the unification improved one of two twin paths and left
`coro.c` doing `return NULL`. Whether an earlier revision ever compensated for
the `coro.c` path elsewhere (making this latent rather than live) needs a
bisect to settle.

**How to settle it:** run the §2 reproducer under `git bisect` across the
suspension-bridge history (`f1d6a827 .. HEAD`). If `test_exc_after_suspend`
fails all the way back to `f1d6a827`, it is long-standing; if it flips at one of
the June/July raise-path commits, that commit is the regression point. Do not
assert "regression" in a fix commit message until this is run.

---

## 7. Proposed fix

Make the coroutine-object resume paths route inner-awaitable exceptions back
through the evaluator so they raise at the `await` point, identical to
`revive_await`.

In `coroutine_send` and `coroutine_throw` (`koskari/lib/coro.c`),
`KOS_CORO_WAITING_PY` branch, replace the `else { ClearIter; DONE; return NULL; }`
with: fetch/normalize the inner exception, clear the await iterator, then
re-enter the evaluator with the exception as a throw so it is delivered at the
suspension point —

```c
} else {
    /* Inner Python awaitable raised: re-raise at the await point so the
       Koskari frame's handler chain runs (mirror revive_await's EVAL_RAISE). */
    PyObject *typ = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&typ, &val, &tb);
    PyErr_NormalizeException(&typ, &val, &tb);
    KosEvalState_PythonAwaitClearIter(&coro_es);
    coro->state = KOS_CORO_RUNNING;
    coro->in_send = 1;
    KosOpResult res = koskari_eval(coro->ctx, NULL, typ, val ? val : typ);
    coro->in_send = 0;
    /* then the existing switch(res.kind) handling below */
    ...
}
```

Preferred: **extract the shared "forward to inner await iter" logic** used by
`revive_await` and by the `coro.c` `WAITING_PY` branches into one helper, so the
StopIteration-value and inner-raise cases cannot diverge again. The duplication
is the real hazard; a single choke point is the durable fix.

The bottom of `coroutine_throw` (coro.c ~334-343) already shows the correct
shape for feeding an exception into `koskari_eval` as a throw — the `WAITING_PY`
branch should reach the same evaluator re-entry instead of `return NULL`.

---

## 8. Test plan

Keep the §2 reproducer as a permanent regression test (RED until fixed; do not
xfail/skip). Add coverage for:

1. Foreign coroutine raises after one loop suspension → caught by Koskari
   `except [EOFError]`.
2. Foreign coroutine raises after suspension → caught by bare `except [Exception]`.
3. `try/finally` around an `await` of a foreign coroutine that raises → the
   `finally` body runs.
4. `asyncio.CancelledError` thrown into a Koskari coroutine parked on a foreign
   awaitable → Koskari `try/finally` cleanup runs.
5. Value path still works (guard against over-correction):
   foreign coroutine returns after suspension → value delivered.

Once green, revisit `design/cli-repl-rework.md`: `_read_line` can drop the
sentinel dance and `repl.ki` can `(await (session.prompt_async …))` inside a
normal `try/except`, on the shared loop, with no worker thread.

# The end.
