# Hosting Koskari from Python

Run Koskari from inside a Python program: load `.ki` modules, execute a source
file into a namespace you control, or drop into a REPL — then call back into
Koskari from Python.

## Prerequisites

- [Quickstart](../quickstart.md)
- [From Python: Interop](../tutorials/from-python/interop.md)
- [From Python: Programs and modules](../tutorials/from-python/programs-and-modules.md)

## Import a `.ki` module (simplest)

`import koskari` installs the import hook, after which any `.ki` file on
`sys.path` imports like a Python module:

```python
import koskari                 # installs the .ki import hook
import mypkg.tools             # loads mypkg/tools.ki if present

result = mypkg.tools.SOME_CONSTANT
```

This is enough when your `.ki` code is packaged alongside Python and you just
want to import it. No event loop is required — imports run on the demand path
(see [Demand, async, and defer](demand-async-and-defer.md)).

## Run a file into a namespace you control (headless)

To execute a specific `.ki` file into a module object you hold — without pulling
in the REPL — use `koskari.module` directly:

```python
import asyncio
import koskari                                  # import hook
from koskari.module import (new_module, init_module,
                            load_module_async)

async def main():
    mod = new_module('__app__')                # a fresh Koskari module
    init_module(mod, 'app.ki')                 # attach file metadata + specials
    await load_module_async(mod)               # execute top-level forms
    # `mod` now holds everything app.ki defined
    greeting = await mod.greet('world')        # see the async note below
    print(greeting)

asyncio.run(main())
```

`init_module` + `load_module_async` is exactly what the CLI's
`run_file_async` does; calling them directly avoids the REPL dependency.

## Interactive REPL and the CLI wrappers

`koskari.cli` offers ready-made async entry points — but it imports the
prompt-toolkit REPL, so it pulls that dependency in:

```python
import asyncio
from koskari.cli import run_file_async, repl_async
from koskari.module import new_module

async def main():
    mod = new_module('__main__')
    await run_file_async(mod, 'app.ki')        # wraps init_module + load
    await repl_async(mod)                      # interactive REPL bound to mod

asyncio.run(main())
```

For a headless service, prefer the `koskari.module` path above so you don't
depend on prompt-toolkit.

## Calling back into Koskari — await the result

**Koskari functions are async-native.** Calling one from Python returns an
awaitable, not the final value — you must `await` it (from an async context):

```python
greeting = await mod.greet('world')            # -> 'hello, world'
```

Outside an event loop, or when handing the result to a raw asyncio API, bridge
it explicitly:

```python
from koskari.asyncio import as_coroutine
coro = as_coroutine(mod.greet('world'))        # a genuine Python coroutine
```

Which construct to reach for —
[Choosing an async construct](choosing-an-async-construct.md).

## Rules of thumb

| Topic | Guidance |
| ----- | -------- |
| Enable `.ki` imports | `import koskari` once, early; the hook installs itself |
| Just importing modules | Plain `import mypkg.mod`; no loop needed |
| Running a file headless | `new_module` + `init_module` + `load_module_async` |
| REPL / CLI wrappers | `koskari.cli.run_file_async` / `repl_async` (adds prompt-toolkit) |
| Calling a Koskari fn from Python | `await` the result — it is an awaitable |
| No event loop available | Demand path; suspension raises `KoskariSyncViolation` |

## See also

- [From Python: Interop](../tutorials/from-python/interop.md) — calling across the boundary
- [Demand, async, and defer](demand-async-and-defer.md) — the async host model
- [Choosing an async construct](choosing-an-async-construct.md) — defer, coroutine-fn, and bridges
- [Packaging an installed command](packaging.md) — ship a `.ki` app as a pip command
