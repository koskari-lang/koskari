# Fence handler reference

Per-name compile-time transliteration for triple-backtick fenced literals.
Motivation and the "when does a format earn a fence" framing are in
[Fences](../concepts/fences.md); the dispatch special is
[`compile/apply-fence`](../specials/compile-apply-fence.md).

## Fence expansion

A triple-backtick fence is read into a `compile/apply-fence` form:

```
(compile/apply-fence NAME RAW META)
```

`NAME` is the fence format, `RAW` is the captured body text, and `META` is a
keyword plist carrying the payload origin (`filename:`, `line:`, `col:`) and
the opener-line `options:`. The **`compile/apply-fence`** special resolves
**`fence/<name>`** in the module namespace and invokes the handler as
**`(handler RAW META-DICT)`**, where `META-DICT` is `META` converted to a
plain dict (`name`, `filename`, `line`, `col`, `options`). The handler
returns ordinary Koskari source, which is compiled — and passed through
**`apply_layout`** — in place of the fence.

Handler resolution (**`find_fence`** in **`koskari.fences`**): a bare-name
binding resolves only when it is a **`FenceHandler`**; a **`fence/`**-prefixed
binding resolves when it is a **`FenceHandler`** or any callable, so a module
may define its own fence by binding a two-argument function to
**`fence/<name>`**. Handlers run at compile time and must not suspend.

## Self-contained expansions

Fence handlers lower to source that references only **universal builtins** —
never a helper that must already be bound in the compiling module. `fence/bash`
lowers onto the `run-process` builtin, so a bash fence works in any module
without imports or setup. This mirrors how the built-in handlers are authored:
in Koskari, in **`koskari/bootstrap/_fences.ki`**, via the **`define-fence`**
syntax.

## Adding new fences

1. Prefer defining the handler in **`.ki`** with **`define-fence`** (see
   `koskari/bootstrap/_fences.ki`). A handler is
   **`(define-fence fence/<name> [raw meta] BODY ...)`** returning Koskari
   source — usually a quasiquoted template.
2. Keep the expansion self-contained: reference only universal builtins so the
   fence needs no pre-existing bindings at the use site.
3. Validate opener-line options from **`(meta.get "options")`** and raise
   **`FenceError`** for unknown ones.
4. Hold the bar: a new fence should express something genuinely awkward in
   native Koskari (see [Fences](../concepts/fences.md)).
5. Add a handler page under **`docs/reference/fences/`**.

```{toctree}
:maxdepth: 1
:glob:

fence-*
```
