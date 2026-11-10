# Supported platforms

Koskari is pre-alpha, so every platform here carries the expectations in
[Release and versioning](release-and-versioning.md). WebAssembly is less
settled than the rest.

| Platform | Architectures | Python | Status |
|---|---|---|---|
| Linux (glibc and musl) | x86_64, aarch64 | CPython 3.9 to 3.14 | Supported, wheels on PyPI |
| macOS 11 and newer | arm64, x86_64 | CPython 3.9 to 3.14 | Supported, wheels on PyPI |
| Pyodide 314 | wasm32 | CPython 3.14 | Experimental, wheel on PyPI |
| Windows | any | any | Untested, no wheels |
| Other 32-bit platforms | any | any | Untested, no wheels |

Free-threaded CPython is not supported. A source build on any platform
not listed needs a C compiler with musttail support and is untested. See
[Build and test](build-test.md).

## WebAssembly

The Pyodide wheel runs Koskari in a browser or under Node.js. It is
experimental beyond the rest of the `0.2.0` development line:

- It may change or break in any snapshot without a changelog entry.
- A snapshot may ship without it. A build or test failure on this
  platform does not hold back the other wheels, so pin an exact version
  and check that the version has a `wasm32` file.
- It is the only 32-bit platform.
- It covers one Python version, the one Pyodide 314 provides. A new
  Pyodide release needs a new wheel.

Install it from inside Pyodide with micropip. `pre=True` is needed while
every release is a snapshot:

```python
import micropip
await micropip.install("koskari", pre=True)
```

### Differences from native builds

- No read-only code. See
  [Builds without read-only code](../guide/runtime-tuning.md#builds-without-read-only-code).
- Threads and subprocesses are unavailable, as they are for all Python
  code under Pyodide.
- There is no terminal, so `koskari` does not run as a command-line REPL.
  An embedding page supplies its own input and output.
- Deep native recursion overflows the JavaScript stack. The interpreter
  dies rather than raising `RecursionError`, and the page has to start a
  new one.
- The frame depth limit defaults to 1000 and is settable up to 2000,
  against 10000 and 65535 natively; `koskari.lib.build_flags` reports
  both as `KOS_FRAME_DEPTH_DEFAULT` and `KOS_FRAME_DEPTH_MAX`. Every
  Koskari frame an exception crosses adds an entry to its traceback, and
  releasing a traceback much longer than the cap overflows the
  JavaScript stack in the same way. A tail call adds no frame, so a
  recursion written as a tail call or a named `let` is not bounded.
- A `KeyboardInterrupt` that escapes the main program ends the Pyodide
  process.
