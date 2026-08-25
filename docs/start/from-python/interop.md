# Interop

Koskari and Python share one process. Calling across the boundary works in
both directions — the workflow you need when mixing `.ki` with Python
libraries or hosting Koskari inside an app.

---

## Koskari calls Python

> **Q:** Import a Python module?
>
> **A:**
>
> ```koskari
> (import os)
> (import-from os path getcwd)
> (import-from os (path as: osp))
> ```

> **Q:** Call a Python function?
>
> **A:** Bind it, then call:
>
> ```koskari
> (import-from math sqrt)
> (sqrt 9)
> ```
>
> Method calls: `(s.upper)` or `(call-attr s upper)` — dotted callees desugar
> through attribute access.

> **Q:** Use `@dataclass`, `functools`, FastAPI, etc.?
>
> **A:** Import and use them like Python — stack `@dataclass` on `define-class`,
> wrap functions with `functools.lru_cache`, construct `fastapi.FastAPI`, and so
> on. See [Classes and objects](classes-and-objects.md) and the
> [Point class](../../patterns/recipes/point-class.md) recipe.

> **Q:** Attribute access and subscripts?
>
> **A:** `obj.field` in source where supported; `(attr obj field)`,
> `(item obj key)`, and `set!` on those targets for dynamic cases.

> **Q:** Are return values Python objects?
>
> **A:** Yes. Pass them back to Python APIs without conversion.

---

## Python calls Koskari

> **Q:** Load a `.ki` module from Python?
>
> **A:** `import koskari` enables the importer by default, then:
>
> ```python
> import mypackage.mymodule   # loads mymodule.ki when present
> ```
>
> See [koskari.importer](../../api/koskari-importer.md).

> **Q:** Call a Koskari function from Python?
>
> **A:** Functions bound at module level are callable from Python. Async
> Koskari callables integrate with **`asyncio`** (see `koskari.asyncio` in the API
> reference).

> **Q:** Run a script programmatically?
>
> **A:** `koskari.cli` (`run_file_async`, `repl_async`) and `koskari.module` —
> for embedding, read the API pages after this track.

---

## Async boundary

> **Q:** Do I need `await` everywhere?
>
> **A:** Not for ordinary Python calls whose results are auto-awaited at Koskari
> call sites. Use **`await`** for **`defer`** and other values the VM does not
> eagerly await. See
> [Demand, async, and defer](../../reference/concepts/demand-async-and-defer.md).

---

## What is not seamless

> **Q:** Can I paste Python source into `.ki` files?
>
> **A:** No. Syntax is Koskari layout and forms, not Python statements.

> **Q:** Can every Python library be used unchanged?
>
> **A:** Any **importable** module is fair game. Reproduce exotic control flow in
> a small REPL form before relying on it in production.

---

## Quick smoke test

At `koskari>`:

```koskari
(import-from math pi)
(* pi 2)
```

Predict a float near `6.28`, then check.

---

## Try it

At `koskari>` (already in the smoke test above), import `math` and use a
Python constant in a Koskari form. Then, if you have a `.ki` module on your
path, import it from Python and call a bound function — same as any mixed
project.

---

## Remember

Import and call Python like Python. Koskari values are Python objects. Load
`.ki` from Python via the importer; use API docs when you embed, not when you
only write scripts.

Next: [Where next](where-next.md).
