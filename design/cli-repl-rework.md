# CLI + REPL Rework — Incremental Port to Koskari

This document records the plan for retiring the broken, unused
`koskari/cli/repl.ki` and incrementally moving the REPL/CLI machinery out of
`koskari/cli/__init__.py` (Python) into Koskari source (`.ki`), while
preserving the traceback-hiding guarantees that make REPL errors readable.

**Audience:** implementers and future prompts. Assumes familiarity with
`koskari.cli`, `koskari.exceptions`, the native evaluator (`koskari/lib/eval.c`),
and the dynchain machinery (`koskari/lib/dynchain.c`).

**Related code:**

- REPL/CLI driver: `koskari/cli/__init__.py`.
- Broken/unused prior attempt: `koskari/cli/repl.ki` (to be emptied).
- Traceback + boundary formatting: `koskari/exceptions.py`.
- Native dynchain capture: `koskari/lib/eval.c`
  (`decorate_exception_with_dynchain`), `koskari/lib/coro.c` (`KosCoroutine_New`).
- Tests: `tests/cli/test_cli.py`.

---

## 1. Motivation

Two goals, one dependent on the other:

1. **Readable REPL tracebacks.** An error in a user-entered form must show only
   what the user typed and the native call stack their form produced. REPL
   *infrastructure* — read, compile, the eval driver, the loop — is machinery
   the user did not write and cannot act on, and must not appear.

2. **Move the REPL into Koskari.** The Python REPL should shrink to the minimum
   host surface, with the loop, prompt handling, input helpers, banner, and
   option handling written in Koskari.

The traceback-hiding guarantee is the constraint that shapes how far the port
can go, so it is settled first.

---

## 2. Boundary hiding: principle and defect

### 2.1 Principle

The boundary marks "user code starts here." Everything above it is
infrastructure and is elided regardless of implementation language. The
guarantee applies to **two** distinct renderings of a failure:

- the **Python traceback** of the caught exception, and
- the **native dynchain** (`__koskari_dynchain__`), rendered as the
  "Koskari dynchain (native eval)" block.

Today only the Python traceback is filtered (`_filter_infrastructure_tb`
matches frame `__name__` against `REPL_INFRASTRUCTURE_MODULES`). The dynchain
is rendered in full. That is currently harmless because the REPL driver is
Python and the user form's dynchain contains essentially just the user's
`__main__` frame.

### 2.2 The defect (fixed)

`_filter_infrastructure_tb` returned `None` in two indistinguishable cases:

- the input traceback was `None`, and
- every frame was infrastructure and got removed.

The caller `format_code_boundary_exception` treated `None` as "keep the
original unfiltered traceback." So when a user form failed, the cause's
traceback was a *single* frame — the Python `await expr()` in
`_eval_expression_async` (module `koskari.cli`, i.e. infrastructure). Filtering
removed it, produced `None`, and the caller restored the unfiltered traceback,
leaking:

```
Traceback (most recent call last):
  File ".../koskari/cli/__init__.py", line 235, in _eval_expression_async
    return await expr()
           ^^^^^^^^^^^^
  Koskari dynchain (native eval):
    at __main__ (<stdin>:0:0) [pc=2]
ZeroDivisionError: division by zero
```

**Fix (implemented in `koskari/exceptions.py`):** when filtering removes every
frame *and* native dynchain metadata is present on the exception chain, drop
the traceback entirely (`tb = None`) instead of restoring the unfiltered one.
`format_exception_with_koskari_trace` already handles `tb is None` by rendering
the dynchain plus the exception line, which is exactly the desired output. When
there is no dynchain (e.g. a genuine internal bug in `koskari.cli` with no
native context), the original traceback is still preserved so the failure
location is not hidden. Regression tests:
`TestFormatCodeBoundaryException` and
`TestFilterInfrastructureTb.test_returns_none_when_all_frames_infrastructure`
in `tests/cli/test_cli.py`.

---

## 3. The seam: why the Python `await` stays

The single Python frame that awaits the compiled user form is not merely
"the thing left to port." It does load-bearing work that must be reinvented if
removed.

Two facts about the runtime:

- `decorate_exception_with_dynchain` (`koskari/lib/eval.c`) walks the failing
  **context's** dyn-node chain from its top through parents at escape time.
- `KosCoroutine_New` (`koskari/lib/coro.c`) allocates a **fresh**
  `KosContext_New(frame)` per coroutine, rooted at that coroutine's own frame.

Therefore, when a **Python** function does `await expr()` on a compiled form,
the form runs in a brand-new context rooted at the user form. The outer Koskari
REPL loop (once ported) runs in a *different* context, suspended at the point
where it called into the Python shim, so its frames are **not** ancestors on
the user form's chain.

Consequences:

1. **Python-frame boundary.** The shim's `__name__` is `koskari.cli`, already
   in `REPL_INFRASTRUCTURE_MODULES`, so `_filter_infrastructure_tb` cuts it.
2. **Dynchain root.** The user form's dynchain is rooted at the form; Koskari
   REPL infrastructure never enters it, because it is on the other side of a
   context boundary. No dynchain filtering is required.

If the eval step were Koskari-calling-Koskari, it would be an in-context
koscall: same context, REPL loop frames as ancestors, and those frames would
appear in the dynchain — reintroducing the leak in Koskari form. So the Python
`await` (and the `try` that wraps it) is retained deliberately as a
context boundary.

### 3.1 Minimal Python surface

The Python REPL shrinks to roughly one function:

```python
async def _await_user_form(expr):
    try:
        return await expr()
    except (UncaughtCompilerError, CompilerError):
        raise
    except Exception as e:
        raise REPLCodeBoundary() from e
```

This must be the **only** thing that awaits a compiled user form, and it must
be entered by a Python call from the Koskari loop (so `await` creates the fresh
coroutine context).

`REPLCodeBoundary` is now structurally optional: the cause already carries a
user-scoped dynchain and a single filterable Python frame. It is retained
because it provides the `--repl-full-trace` (`nofold`) toggle and a clean marker
for the Koskari `except`. Whether to keep it is an open decision (Section 6).

---

## 4. Port plan

Empty `koskari/cli/repl.ki` and port leaf-first. Keep Python as the source of
truth for each piece until its Koskari replacement has test coverage, then
delete the Python copy.

### 4.1 Contract the port must preserve

> Read and compile happen in Koskari, before the boundary. The boundary is a
> single Python async shim that is the only thing that awaits the compiled
> form. Catch and format happen back in Koskari. Nothing on the eval path
> between the Koskari loop and the user form is an in-context koscall.

As long as this holds, the "representative of what the user typed" property is
preserved for both the Python traceback and the dynchain.

### 4.2 Ordering

1. **Pure string helpers.** `_in_block_body`, `_continuation_indent`
   (`koskari/cli/__init__.py:242-288`). No async, no boundary, trivially
   unit-testable.
2. **Prompt backend.** `_make_prompter` (`:101-163`): prompt_toolkit →
   readline → `input()` fallback. Isolated construction.
3. **Banner / option handling.** `startup_lines`, argument parsing, and CLI
   target resolution. Ancillary; no eval-path involvement.
4. **Read step.** Reader / incomplete-input handling from
   `_eval_expression_async` (`:214-230`), *without* the eval. Returns an
   "incomplete" sentinel or a compiled fragment. Synchronous.
5. **Eval boundary.** Keep in Python per Section 3. Koskari calls
   `_await_user_form(expr)`.
6. **Loop + error dispatch.** Port the `while True` loop and its `except`
   ladder. Koskari catches `REPLCodeBoundary` (or the raw exception) and calls
   the Python formatters.

### 4.3 Read/compile errors are already separate

Reader and compiler errors (`ReaderError`, `CompilerError`,
`UncaughtCompilerError`) are surfaced via `.explain()`
(`koskari/cli/__init__.py:399-431`), not via traceback/dynchain filtering. They
do not depend on the dynchain-root boundary, so read and compile can move to
Koskari freely: a Koskari handler catches them and calls `.explain()`.

---

## 5. Async: `await` vs `demand`

For the eval step, prefer **`await`**, not `demand`.

- In Koskari, evaluation is ambiently async — an awaitable result is
  auto-awaited in place. Inside a Koskari REPL function you *call* the compiled
  form; you do not write an explicit await and you do not create a Python
  frame. (The Python `await` in the shim exists for the context-boundary
  reason in Section 3, not because Koskari needs it.)
- `demand` forces a synchronous result by driving the async machinery to
  completion. It suits a REPL running *outside* an event loop. The loop
  deliberately keeps an event loop alive. Using `demand` inside a running loop
  risks a nested drive fighting the outer loop and would block awaitables the
  user's own code creates.

Guidance: keep an outer async driver (`drive_async_loop`), let the evaluator
auto-await user forms. Reserve `demand` for a possible future synchronous
(non-async) REPL variant.

### 5.1 Prompt reads (implemented): cooperative `prompt_async`

The prompt read runs **cooperatively on the REPL's own event loop**, not on a
worker thread. `_make_repl_prompter` returns an *async* prompter; the
prompt_toolkit backend awaits `session.prompt_async(...)` (which runs the prompt
application on the current loop via `add_reader`), and `repl.ki` reads a line
with `(await (prompter prompt default: indent))` inside a Koskari
`try/except [EOFError] / [KeyboardInterrupt]`. The only thread offload left is
the blocking `input()` fallback, which has no async form.

This depended on two things landing first:
- `prompt_async` avoids the nested `asyncio.run` that `session.prompt()` runs
  (that collision was why an earlier draft pushed the prompt onto
  `run_in_executor`).
- Foreign-awaitable exceptions are now delivered at the Koskari `await` point, so
  EOF / interrupt from `prompt_async` reach the loop's `except` handlers with no
  Python sentinel shim (`fe8ab63a`, PR #86; regression test
  `tests/importer/test_await_foreign_exception.py`).

---

## 6. Open decisions

1. **Formatter location.** Keep the boundary/traceback formatters in
   `koskari/exceptions.py` (Python) and call them from the Koskari loop, or
   migrate them into Koskari? Calling Python formatters from Koskari is
   straightforward and is the lower-risk default.
2. **`REPLCodeBoundary` retention.** Keep it for the `--repl-full-trace` toggle
   and as an explicit marker, or drop it and have Koskari catch the raw
   exception directly (now viable given the fresh-context property)?
3. **Future dynchain filtering.** If the boundary ever needs to move
   *into* Koskari (no Python `await`), the dynchain renderer will need an
   explicit cut. Two candidate mechanisms, recorded for later:
   - **Positional:** capture dynchain depth at the eval call; trim frames at or
     above it. Simple, but fragile if user code re-enters REPL helpers.
   - **Provenance:** show frames whose source filename is `<stdin>` (user
     input); elide frames from `koskari/cli/repl.ki`. Mirrors the existing
     module-name approach and survives re-entry. Dynchain frame metadata
     already carries `filename`.
   This is out of scope while the Python `await` boundary is retained.

---

## 7. Testing

- Boundary fix regression: `tests/cli/test_cli.py`
  (`TestFormatCodeBoundaryException`,
  `TestFilterInfrastructureTb.test_returns_none_when_all_frames_infrastructure`).
- A full-stack reproducer under phase_cli / phase_reproducers that runs
  `(/ 1 0)` in the REPL and asserts the printed output contains the dynchain
  block but *not* `_eval_expression_async` or any `koskari/cli` path is the
  end-to-end guard. Worth writing before further porting so both the Python
  path and the eventual Koskari port validate against it.
- Each ported helper (Section 4.2) gets unit coverage before its Python
  counterpart is removed.

# The end.
