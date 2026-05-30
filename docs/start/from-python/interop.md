# Interop

Koskari and Python share one process. This chapter covers calling across the
boundary in both directions.

---

## Koskari calls Python

> **Q:** Import a Python module?
>
> **A:** Same names as Python:
>
> ```koskari
> (import os)
> (import-from os path getcwd)
> (import-from os (path as: osp))
> ```
>
> Relative imports use leading dots: `(import .sibling)`.

> **Q:** Call a Python function?
>
> **A:** Bind it, then call — or call attributes directly:
>
> ```koskari
> (import-from math sqrt)
> koskari> (sqrt 9)
> 3.0
> ```
>
> Method calls:
>
> ```koskari
> (define s "hello")
> (call-attr s upper)
> ```

> **Q:** Attribute access and subscripts?
>
> **A:**
>
> ```koskari
> (attr obj field)
> (item obj key)
> (set! (attr obj field) value)
> (set! (item obj key) value)
> ```

> **Q:** Are return values Python objects?
>
> **A:** Yes. A Koskari number may be a Python `int`; a Koskari string is a
> Python `str`. Pass them back to Python APIs without conversion.

---

## Python calls Koskari

> **Q:** Load a `.ki` module from Python?
>
> **A:** Enable the Koskari importer (default on `import koskari`), then:
>
> ```python
> import mypackage.mymodule   # loads mymodule.ki when present
> ```
>
> See [koskari.importer](../../api/koskari-importer.md) in the API reference.

> **Q:** Run a script programmatically?
>
> **A:** Use **`koskari.cli`** (`run_file_async`, `repl_async`) or build a
> module with **`koskari.module.new_module`** and load expressions — patterns
> the test suite uses. Embedding details live in the API docs, not this track.

> **Q:** Call a Koskari function from Python?
>
> **A:** Koskari functions exposed on a module namespace are callable from
> Python. Async Koskari callables integrate with **`asyncio`** (see
> `koskari.asyncio` in the API reference).

---

## Async boundary

> **Q:** Do I need `await` everywhere?
>
> **A:** Not for ordinary Python calls whose results are auto-awaited at the
> Koskari call site. You **`await`** explicitly for **`defer`** handles and
> other values the VM does not eagerly await. Crossing from Python **into**
> Koskari async code uses normal `asyncio` patterns.

> **Q:** Generators and async generators?
>
> **A:** Supported in the VM (`yield`, `yield-from`, async iteration). Python
> iterators and async iterators interoperate at the opcode layer. **Not yet:**
> full cookbook — treat as advanced interop.

---

## What is not seamless

> **Q:** Can I paste Python source into Koskari files?
>
> **A:** No. Syntax is Koskari (or layout), not Python. A Python AST loader is
> **proposed, not shipped** — do not plan on `.py` files executing on the
> Koskari VM without translation.

> **Q:** Can every Python library be used unchanged?
>
> **A:** Any **importable** module is fair game. C extensions, threading quirks,
> and context-manager edge cases follow Python rules — Koskari adds
> continuation and dynamic-wind semantics that can surprise you at the
> boundary. Reproduce in a small REPL form before relying on exotic control
> flow.

> **Q:** Embedding Koskari in a long-lived Python app?
>
> **A:** Supported via importer + module APIs. Document your init order
> (importer install, event loop, demand vs async load paths). Read
> `koskari-importer` and `koskari-module` API pages before production use.

---

## Quick smoke test

> **Q:** One line to prove Python interop?
>
> **A:** At `koskari>`:
>
> ```koskari
> (import-from math pi)
> (* pi 2)
> ```
>
> Predict a float near `6.28`, then check.

---

## What you should remember

Import and call Python like Python. Koskari values are Python objects. Load
`.ki` from Python via the importer; embed via documented CLI/module APIs.
Syntax is not Python; the runtime is.

Next: [Where next](where-next.md).
