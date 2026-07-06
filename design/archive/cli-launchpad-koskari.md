# CLI launchpad in Koskari

**Implemented (archived 2026-07-06).** The launchpad now lives in Koskari:
`koskari/cli/main.ki` owns `option_parser`, `resolve_cli_target`, and `cli`
(driving `argparse`), and `koskari/cli/__init__.py` is reduced to the thin
Python host whose `main()` boots `main.ki` through `run_target`. The body below
is the original design, kept as historical record.

This document plans moving the Koskari command-line launchpad -- option
parsing, target resolution, and run orchestration -- out of Python
(`koskari/cli/__init__.py`) into a Koskari module (`koskari/cli/main.ki`), and
booting it through the existing launcher the same way a packaged
`koskari_scripts` command runs. The REPL loop already lives in Koskari
(`koskari/cli/repl.ki`); this extends that so the whole CLI, not just the
interactive loop, is Koskari with a minimal Python host.

**Audience:** implementers and future prompts. Assumes familiarity with
`koskari/cli/__init__.py`, `koskari/cli/repl.ki`, `koskari/launcher.py`
(`hosted_run` / `run_target`), and the import hook (`koskari/importer.py`).

**Related:**
- REPL port and boundary contract: [cli-repl-rework.md](cli-repl-rework.md).
- Launcher host: `koskari/launcher.py` (`hosted_run`, `run_target`,
  `import_module_async`).
- Precompilation (separate, later build/distribution task -- not part of this
  effort): [interpreter/kio-setuptools-build.md](interpreter/kio-setuptools-build.md).

**Decisions (settled with the user):**
- Argument parsing moves fully into `main.ki`, driving Python's `argparse`.
- The `koskari` command boots `main.ki` through the launcher (`run_target`).

**Status:** complete (`make test` green). `koskari/cli/main.ki`
holds `option_parser`, `resolve_cli_target`, and `cli`; `koskari/cli/__init__.py`
is reduced to the thin host (`main` stub, `_await_user_form`,
`_make_repl_prompter`, `version_line`/`startup_lines`, boundary constants, and
thin `repl_async`/`run_file_async`/`run_command_async`). Two findings worth
recording:

- **Domain:** `cli` does **not** open its own `Domain`; it relies on the one
  `hosted_run` opens. No nesting needed -- clean.
- **`main` name collision:** `koskari.cli` exposes both the `main` *function* and
  the `main.ki` *submodule*. Reaching the submodule must go through
  `sys.modules` (`import_module_async`, `importlib.import_module`), not
  `import koskari.cli.main as m` / `getattr(koskari.cli, "main")`, which resolve
  to the function. `hosted_run` already uses `sys.modules`; tests use
  `importlib.import_module`. The function attribute is unaffected (the `.ki`
  loader does not overwrite the parent package attribute), so
  `console_scripts: koskari = koskari.cli:main` and `from koskari.cli import
  main` keep resolving to the function.

---

## 1. Goal

Write the CLI launchpad in Koskari and launch it like a `koskari_script`. The
Python residue shrinks to a thin host: the `console_scripts` stub, the async
eval-boundary shim, and (for now) the prompt_toolkit backend and the exception
formatters. Everything else -- banner, option parsing, target resolution,
`Domain` orchestration, file/command/REPL dispatch -- becomes Koskari.

The single goal is **self-hosting the CLI**. Correctness of the port is what
matters here.

**Non-goals (explicitly out of scope):**
- Startup latency / performance. Compiling `.ki` at launch is fine for now;
  precompiling all `.ki` to `.kio` is a separate build-and-distribution task,
  handled later, not part of this effort.
- Migrating the prompt_toolkit backend or the exception formatters (they stay
  Python for now).

---

## 2. Target architecture

Three layers:

1. **Python host** (`koskari/cli/__init__.py`, shrunk to the irreducible core):
   - `main(argv=None)` -- the `console_scripts` entry, reduced to a launcher
     stub (§4).
   - `_await_user_form(expr)` -- the eval-boundary shim. Must stay Python so the
     `await` roots a fresh coroutine context; see cli-repl-rework.md §3.
   - `_make_repl_prompter(histfile, ns)` -- prompt_toolkit / input() backend.
     Movable, but it is pt-object glue with no eval-path role; kept in Python.
   - `REPL_INFRASTRUCTURE_MODULES`, `REPLCodeBoundary` -- boundary constants,
     imported by the Koskari layers. Kept in the host (small, shared).
   - Exception formatters already live in `koskari.exceptions` (Python) and are
     called from Koskari; unchanged (cli-repl-rework.md open decision #1).

2. **`koskari/cli/main.ki`** -- the launchpad:
   - `version-line`, `startup-lines`.
   - option parsing: an `option-parser` that builds and drives
     `argparse.ArgumentParser`, plus the target-resolution helpers.
   - `cli` -- the async entry (the current `cli_async` body): install the import
     hook, tweak `sys.path`, run a command / file, or drop into the REPL. Does
     **not** open its own `Domain` (the launcher already did; §3).
   - thin `run-file` / `run-command` over `koskari.module` loaders.

3. **`koskari/cli/repl.ki`** -- the interactive loop (already done).

`main.ki` and `repl.ki` both `import-from koskari.cli` the host pieces they need
(`_await_user_form`, `_make_repl_prompter`, formatters, boundary constants),
exactly as `repl.ki` does today. No import cycle at load time: the host module
finishes importing before either `.ki` submodule is loaded.

---

## 3. Boot path (through the launcher)

`hosted_run` ([koskari/launcher.py](koskari/launcher.py)) already does what the
CLI needs: set `sys.argv`, enter `with Domain():`, `import_module_async` the
target module, fetch a callable, invoke it, await if awaitable, return the exit
code. `run_target(target, argv, script_name)` wraps that in `drive_async_loop`.

So the boot chain becomes:

```
console_scripts: koskari = koskari.cli:main   (Python stub)
  -> run_target("koskari.cli.main:cli", argv=argv[1:], script_name=argv[0])
    -> drive_async_loop(hosted_run("koskari.cli.main", "cli", "koskari", argv))
      -> with Domain(): cli = (koskari.cli.main).cli; await cli()
```

`cli` (Koskari) reads `sys.argv` (set by `hosted_run`), parses, and dispatches.
This is the identical path a packaged `koskari_scripts` command uses -- the CLI
dogfoods the launcher.

Consequences to handle:
- **Domain ownership (open, not a spec).** `hosted_run` opens a `Domain` around
  the target. Whether `cli` reuses that and skips its own, or keeps its own
  `with Domain():`, is an implementation detail to settle during the port by
  what actually works -- nesting may be fine, or reusing the launcher's may be
  cleaner. This was inferred earlier, not required; do not treat "drop cli's
  Domain" as fixed.
- **`fn()` takes no args.** `hosted_run` calls the callable with no arguments;
  `cli` reads `sys.argv` itself (prog = `sys.argv[0]`, args = `sys.argv[1:]`).
- **Exit codes / early exits.** `--version` and `--compile-kio` just `return 0`
  (or the code); `hosted_run` returns it, `drive_async_loop` surfaces it.
- **`SystemExit` from argparse.** `parser.error()` / `parser.parse_args` on
  `--help`/bad args raise `SystemExit`; it propagates through `drive_async_loop`
  to the stub. Keep that behavior (argparse's standard contract).
- **`KeyboardInterrupt`.** Caught in the Python stub -> return 130, matching
  `run_resolved_script`.

---

## 4. The Python stub

```python
def main(argv=None):
    from koskari.launcher import run_target
    if argv is None:
        argv = sys.argv
    try:
        return run_target(
            "koskari.cli.main:cli",
            argv=argv[1:],
            script_name=argv[0])
    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130
```

Programmatic `main(['koskari', ...])` (used by tests) still works: `run_target`
forwards `argv` and `hosted_run` sets `sys.argv = [script_name] + argv`.

---

## 5. Migration inventory

Current `koskari/cli/__init__.py` (post REPL work):

| Symbol | Disposition |
|---|---|
| `main` | shrink to the launcher stub (§4) |
| `_await_user_form` | stays (boundary shim) |
| `_make_repl_prompter` (+ pt/readline/input backend) | stays (pt glue) |
| `REPL_INFRASTRUCTURE_MODULES`, `REPLCodeBoundary` | stays (shared constants) |
| `repl_async` wrapper | keep as public re-export (docs/embedding) or have `main.ki` call `repl.ki` directly; decide in phase 1 |
| `version_line`, `startup_lines` | -> `main.ki` |
| `option_parser` | -> `main.ki` (drive `argparse`) |
| `_CompileKioSourceNameAction` | delete; fold its "requires --compile-kio" check into resolve |
| `_reject_option_like_rest`, `_split_script_rest`, `_resolve_cli_target` | -> `main.ki` |
| `cli_async` | -> `main.ki` as `cli` (Domain ownership TBD; see §3) |
| `run_file_async`, `run_command_async` | -> `main.ki` (thin loaders) |
| `prepare_cli_async` | fold `--version` / `--compile-kio` short-circuits into `cli` |

### 5.1 argparse from Koskari

`argparse` is plain Python; Koskari drives it with `import-from argparse
ArgumentParser` and keyword-argument `add_argument` calls. The one awkward
piece is the custom `Action` subclass (`_CompileKioSourceNameAction`), which
only enforces that `--compile-kio-source-name` requires `--compile-kio`. Rather
than subclass `argparse.Action` in Koskari, **drop the Action** and perform that
validation in the resolve step (where `--compile-kio` is already inspected).
`REMAINDER`, `mutually_exclusive_group`, and `store_true`/`store` are ordinary
argparse calls and translate directly.

---

## 6. Phasing

1. **Orchestration first.** Add `main.ki` with `cli`, `run-file`,
   `run-command`, and the banner; keep the Python `option_parser` and have
   `cli` call it (import from `koskari.cli`). Repoint `main()` to
   `run_target("koskari.cli.main:cli", ...)`. Drop the `Domain` from `cli`
   (launcher owns it). Verify the full CLI + `make test`.
2. **Parsing into Koskari.** Move `option-parser` and the resolve helpers into
   `main.ki`, driving `argparse`; delete the custom `Action` (fold its check).
   Migrate `TestOptionParser` to call the Koskari entry (mirror
   `tests/cli/test_repl_ki.py`), or keep thin Python re-exports for back-compat.
3. **Cleanup / follow-ups.** Remove now-dead Python (`prepare_cli_async`,
   `cli_async`, etc.). Migrating the pt backend and formatters is out of scope.
   (`.kio` precompilation is a separate build/distribution task -- see Non-goals
   in §1.)

Keep Python as the source of truth for each piece until its Koskari replacement
has test coverage, then delete the Python copy -- the same discipline used for
the REPL port.

---

## 7. Testing

- `make test` green at every phase.
- `tests/cli/test_cli.py::TestOptionParser` (~20 cases) is the main migration
  tax: it calls the Python `option_parser` / `_resolve_cli_target` directly.
  In phase 2 these move to exercise the Koskari `option-parser` (lazy import,
  as in `test_repl_ki.py`), or the Koskari parser is re-exported through Python
  for back-compat.
- `TestMain` already drives `main()` end to end; it validates the launcher boot
  path unchanged.
- Add a test that boots `cli` via `run_target` with a scripted argv (command,
  file, and REPL targets) to cover the launcher integration directly.

---

## 8. Risks / open questions

- **`repl_async` public API.** `koskari.cli.repl_async` is documented and used by
  the embedding recipe. Keep the thin Python wrapper, or re-export the Koskari
  entry; do not silently drop it.
- **Import-hook availability at boot.** `hosted_run` runs inside the koskari
  runtime with the hook installed, so loading `main.ki` is fine; the stub still
  imports `koskari` first (which installs the hook). `repl_async` already ensures
  `install()` defensively.
- **`--help` output / prog name.** Driven from `sys.argv[0]` set by
  `hosted_run`; confirm `--help` and error text still show `koskari`.

# The end.
