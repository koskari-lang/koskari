# Koskari Design: Fenced Foreign Literal Expansion

## Status

Implemented. `fence/bash` ships as the sole built-in fence; a JSON fence
was prototyped and then dropped (see *Deferred: JSON*).

The durable part of this design is the **mechanism** — a reader-to-compiler
pipeline for capturing raw foreign source and lowering it to Koskari source
at compile time. Which foreign formats earn a built-in fence is a separate,
case-by-case question.

## Summary

A triple-backtick fence captures raw source in a declared foreign format
and lowers it, at compile time, into ordinary Koskari source. This is not a
runtime parser feature: a fence handler is a compile-time transliterator
that receives the raw fenced source plus metadata and returns Koskari
source to compile in place.

The core special, `compile/apply-fence`, is deliberately parallel to
`:block:` layout expansion (`koskari.reader.layouts.apply_layout`): a
compiler form that turns captured structure into ordinary source before
further compilation.

## Architecture

The reader does as little policy work as possible; semantics live in
compiler space, where Koskari already expects source-to-source transforms.

* the **reader** recognizes triple-backtick fence syntax, captures the
  fence name, the raw body, and source-location metadata, and lowers the
  whole thing to a `(compile/apply-fence NAME RAW META)` form — it does not
  parse the foreign body or invoke handlers
* the **`compile/apply-fence` special** resolves `fence/<name>` in the
  module namespace, calls it with the raw source and a metadata dict, and
  requires it to return valid Koskari source
* the returned source is compiled in place (and passed through
  `apply_layout`, so a Koskari-defined handler may use layout forms in its
  template)

### Reader surface

```text
```NAME OPTION: VALUE ...
raw fence body
```
```

The fence name is the first word after the backticks (whitespace optional,
so ` ```bash ` and ` ``` bash ` are equivalent). The rest of the opener
line is read as keyword/value option pairs, each requiring a value on the
same line. The body is captured verbatim until a line containing only three
backticks; body lines are dedented by the closing line's indentation, so an
indented fence does not leak indentation into the payload. A fence reads as
a single value and may appear in expression position.

Fence reading is governed by the `reader/fences` pragma (baseline on);
`(pragma [reader/fences False])` restores backtick's plain quasiquote
readings.

### Handler contract

A handler is `(handler RAW META)` returning Koskari source. `META` is a
dict with `name`, `filename`, `line`, `col` (the payload origin and dedent
base), and `options` (the opener-line pairs) entries; a handler may attach
`line`/`col`-derived positions to a `FenceError` so it points into the
fence body.

Handler resolution (`find_fence` in `koskari.fences`): a bare-name binding
resolves only when it is a `FenceHandler`; a `fence/`-prefixed binding
resolves when it is a `FenceHandler` or any callable, so user code may
define fences as plain functions. Koskari handlers are invoked via
`demand_call` and must not suspend. Handlers are compile-time plugins —
compiling a module that uses a custom fence runs that fence's handler.

Built-in fences are defined in Koskari itself
(`koskari/bootstrap/_fences.ki`, via the `define-fence` syntax). Fence
expansions are self-contained: they reference only universal builtins, so a
fence works without pre-existing helper bindings in the compiling module.

## Built-in: `fence/bash`

A bash fence captures a shell script and lowers onto the `run-process`
builtin (`koskari.asyncio`).

* by default the fence produces an async **callable**; invoking it runs the
  script via `bash -c`, and positional arguments passed to the callable
  become the script's `$1`, `$2`, ... (str-coerced)
* with `immediate: True` the fence expands to the `run-process` call in
  place, so the fence expression itself evaluates to the finished result

A run yields a `ProcessResult` — the subprocess analogue of an HTTP
Response — carrying `args`, `returncode`, raw `stdout`/`stderr`, decoded
`text`/`stderr_text`, and `json`/`jsonl`/`yaml` parsers. A nonzero exit is
reported on the result, never raised; `raise_for_status` opts into raising
`CalledProcessError` and returns the result on success for chaining.

This is the case that justifies a fence: "capture a shell script, launch it
async, get a result object back" is awkward to express as s-expressions,
reads cleanly at the call site, and drops into the scheduler's job model.

## Deferred: JSON

A `fence/json` (extended JSON — bare identifiers in value position lowering
to name references, lowered to `#dict`/`#list` builders) was implemented and
then removed.

It did not earn its keep. Koskari's native literal forms win in every case:
`{ "a": 1 }` is barely different from `#{"a" 1}`, native forms give
compile-time structure the fence can't, and the bare-identifier extension —
the one thing distinguishing it from pasting real JSON — fires exactly when
native syntax is already the better choice. The handler was also ~200 lines
of hand-rolled recursive-descent parser existing only because bare
identifiers aren't legal JSON.

The bar for a foreign-literal fence: the notation must be **genuinely
difficult to express in native Koskari**. JSON isn't. Revisit fences for
data notation only when a format clears that bar (a large, awkward-to-quote
template language, say), and prefer leaning on a stdlib parser over a
bespoke one.

## Invariants

* Fence expansion is compile-time, not runtime.
* The reader captures fenced source but does not parse foreign formats.
* `compile/apply-fence` is the dispatch point for fence expansion.
* Fence handlers return source, not opaque runtime results.
* Fence expansions reference only universal builtins.

## Reference

See `docs/reference/specials/compile-apply-fence.md` for the user-facing
reference.

## The end.
