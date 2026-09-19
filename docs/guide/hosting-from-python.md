# Hosting Koskari from Python

Run Koskari from inside a Python program: import `.ki` modules, execute
a file into a namespace you hold, or open a REPL, and call back into
Koskari from Python.

## Importing a module

`import koskari` installs the import hook, after which any `.ki` file on
`sys.path` imports like a Python module:

```python
import koskari                 # installs the .ki import hook
import mypkg.tools             # loads mypkg/tools.ki

result = mypkg.tools.SOME_CONSTANT
```

This covers `.ki` code packaged beside Python. No event loop is needed;
imports run on the demand path. See
[Demand, async, and defer](demand-async-and-defer.md).

## Running a file into a namespace

To execute one `.ki` file into a module object you hold, without the
REPL, use `koskari.module`:

```python
import asyncio
import koskari
from koskari.module import (new_module, init_module,
                            load_module_async)

async def main():
    mod = new_module('__app__')                # a fresh Koskari module
    init_module(mod, 'app.ki')                 # attach the file and the specials
    await load_module_async(mod)               # run the top-level forms
    greeting = await mod.greet('world')        # a Koskari function is awaited
    print(greeting)

asyncio.run(main())
```

`init_module` followed by `load_module_async` is what the CLI's
`run_file_async` does; calling them directly avoids the REPL dependency.

## The REPL and the CLI wrappers

`koskari.cli` has async entry points for running a file and for the
REPL. It imports the prompt-toolkit REPL, so it brings that dependency
in:

```python
import asyncio
from koskari.cli import run_file_async, repl_async
from koskari.module import new_module

async def main():
    mod = new_module('__main__')
    await run_file_async(mod, 'app.ki')        # init_module plus load
    await repl_async(mod)                      # a REPL bound to mod

asyncio.run(main())
```

A headless service takes the `koskari.module` path above and leaves
prompt-toolkit out.

## Calling into Koskari

A Koskari function called from Python returns an awaitable, not a
value, because every Koskari function can suspend. From an async
context, await it:

```python
greeting = await mod.greet('world')            # 'hello, world'
```

A function defined with `define-demanding`, or `@demand`, returns the
value directly and can be called with no event loop. Outside a loop, or
when handing a result to a raw asyncio API, bridge it:

```python
from koskari.asyncio import as_coroutine
coro = as_coroutine(mod.greet('world'))        # a Python coroutine
```

Which construct fits which situation is the
[decision table](demand-async-and-defer.md#choosing-a-construct).

## Summary

| Need | Use |
| ---- | --- |
| `.ki` imports | `import koskari` once, early |
| importing modules | `import mypkg.mod`; no loop needed |
| running a file headless | `new_module`, `init_module`, `load_module_async` |
| the REPL or CLI behaviour | `koskari.cli.run_file_async` and `repl_async`, with prompt-toolkit |
| calling a Koskari function | `await` the result, or define it with `define-demanding` |
| no event loop | the demand path; a suspension raises `KoskariSyncViolation` |

## See also

- [Interop](../tutorials/from-python/interop.md): calls across the boundary
- [Demand, async, and defer](demand-async-and-defer.md): the host model
- [Packaging a Koskari application](packaging.md): a `.ki` program as a pip command
