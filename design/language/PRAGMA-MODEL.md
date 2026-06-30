# Koskari Pragma Model

## Overview

The Koskari pragma model provides a clear, deterministic mechanism for controlling
module-level reader and compiler behavior. Pragmas gate syntax and lowering
features (such as `->`, `&foo.bar`, or specific compiler passes) without
introducing global state or per-form runtime checks.

Pragmas are:

* **Module-scoped** — each module owns its own pragma object.
* **Immutable but replaceable** — a new pragma replaces the old one.
* **Load-time effective** — changes apply only to subsequent parsing and
  compilation.
* **Serializable** — compiled artifacts store a snapshot of the active pragma.

Non-goals:

* Runtime feature toggles after module load.
* Header-scan or multi-phase compilation models.
* Implicit inheritance of pragma settings across modules.

---

## Core Concepts

* **Pragma**: An immutable object attached to a module that defines reader and
  compiler feature flags.
* **Feature**: A boolean or keyed setting that controls parsing or compilation
  behavior.
* **Reload**: The act of rebuilding parser/compiler internal tables after a
  pragma replacement.

The pragma object itself contains structured fields (v0.1 schema) and may
include flexible extension keys for development.

---

## Where Pragmas Live

Pragmas are updated via a normal top-level special form, not a header directive.

### Special Form

```
(compile/pragma key: value ...)
```

Keys are keyword-style (`resolution_mode:`, `bracket_calls:`, …). Values are
literal keywords, symbols, or booleans, read structurally and **never
evaluated** — a pragma cannot depend on runtime state, which is what keeps the
form "always parseable under the base grammar." Example:

```
(compile/pragma resolution_mode: scheme bracket_calls: error tco: #f)
```

Properties:

* Always parseable under the base grammar.
* May appear multiple times.
* Later invocations replace the previous pragma (overrides layer onto the
  current `module.__pragma__`, not onto the defaults).
* Changes affect only subsequent forms.

---

## Load-Time Evaluation Model

Koskari interleaves read, parse, and compile during module load. There is no
separate header phase.

### Baseline

When a module is created:

* `module.__pragma__` is set to a default pragma object.
* The parser and compiler build internal tables derived from that pragma.

### Pragma Update

The work splits across two phases of the same top-level form, which is what
makes the update forward-only without any retroactive machinery:

**Compile time** (while the pragma form itself is compiled):

1. Keys are validated against the registry; an unknown key raises a
   `CompilerError` here.
2. A new immutable pragma object is constructed (current `module.__pragma__` +
   overrides) and stashed as a constant.

**Eval time** (when the compiled pragma form executes during load):

3. `module.__pragma__` is replaced.
4. `module.__reader__.reload_pragma(module.__pragma__)` is called.
5. `module.__compiler__.reload_pragma(module.__pragma__)` is called.

Because module load is strictly read → compile → **eval** → read-next, the
*next* top-level form is read by the reconfigured reader and compiled by the
reconfigured compiler. Application at eval time (rather than at compile time of
the pragma form) is deliberate: it gives identical behavior in the REPL, where
each entry is independently read, compiled, and evaluated, and where `repl.ki`
already rebinds `__reader__`.

### Effects

* Changes apply only to subsequent tokenization, parsing, and compilation.
* No retroactive rewriting occurs.
* No per-form pragma checks are performed.
* Reload fully rebuilds pragma-derived tables.

---

## Import Interaction

Pragmas are strictly per-module.

* A module's pragma does not affect imported modules.
* Imported modules use their own stored pragma snapshot.
* Build systems may enforce defaults externally, but the module's own pragma
  always wins.

This avoids cross-module semantic leakage and simplifies caching.

---

## Failure Modes and Diagnostics

Recommended behavior:

* Unknown key: error.
* Conflicting settings: last wins, with a diagnostic.
* Pragma after code that depends on a different grammar: allowed, but only
  forward-effective.

Error messages should include:

* Module name or path.
* Location of the pragma form.
* A summary of active pragma settings (or digest).

---

## Feature Matrix and Routing Registry

The pragma object holds a resolved value for every known key. The *design* is a
single registry: each key declares its **target** (which load facet it touches,
`__reader__` or `__compiler__`) and its **kind** —

* **flag** — a cheap scalar (bool / enum) that is set and done.
* **drop-in** — a behavior object or table that `reload_pragma` swaps or
  rebuilds (e.g. the resolution pragma class, the bracket-reading mode).

Adding a key is one registry row plus one line in the target's `reload_pragma`.

| Key | Target | Kind | Backing knob |
|---|---|---|---|
| `resolution_mode` | compiler | drop-in | `resolution_pragma_class` (Koskari / Scheme / Python) |
| `bracket_calls` | compiler | flag | `bracket_calls` (`ignore` / `warn` / `error`) |
| `tco` | compiler | flag | `tco_enabled` |
| `layout` | reader | flag | `layout_enabled` |
| `layout_implicit_calls` | reader | flag | `layout_implicit_calls` |
| `bracket_pairs` | reader | drop-in | `bracket_pairs` (also propagated to the inner reader) |
| `convert_fast` | compiler | flag | *reserved — `convert-fast` pass, not yet built* |
| `compress` | compiler | flag | *reserved — bytecode compression pass* |

`convert_fast` and `compress` are validated but inert in v0.1: the keys are
reserved so source can name them without error before the passes exist.

### Later: atom gating

A second wave gates syntax-level reader atoms — `&foo` / place-aware `&foo.bar`
(ref), `foo->bar` (deref-attr), and `*identifier` deref atoms. These need the
reader to rebuild its atom recognizers (via `set_atom_pattern`) on reload, and
user-defined reader macros and atoms must be stored separately so a reload does
not silently discard them (see **User Overlays**). Deferred until the v0.1
routing registry is in place.

---

## Minimal Implementable Subset (v0.1)

1. Provide an immutable `ModulePragma` value object in `koskari.compile.pragma`
   with a `replace(**overrides)` that layers onto the current pragma, plus the
   key registry above.
2. Add `Compiler.reload_pragma(pragma)` (mirrors the reader's), applying the
   compiler-side keys and storing `module.__pragma__`. Set a default
   `module.__pragma__` in `Compiler.__init__`.
3. Extend `LayoutReader.reload_pragma(pragma)` (today only handles
   `layout_implicit_calls`) to also apply `layout` and `bracket_pairs`.
4. Add a small runtime applier (`__pragma_apply__`) that, running in the module
   namespace, pulls the live `__reader__` / `__compiler__` and calls both
   `reload_pragma`s.
5. Implement the `compile/pragma` special: validate keys, build the new pragma
   at compile time, emit the eval-time applier call.
6. Defer serialization of the pragma snapshot into `.kio` — a compiled artifact
   already bakes the resolution/bracket decisions into its bytecode and does no
   reading, so the snapshot is only needed for introspection/recompile.

---

## Storage Model

* `module.__pragma__` is immutable but replaceable.
* Parser and compiler maintain internal tables derived from the current pragma.
* Reload reconstructs these tables entirely.
* Compiled artifacts store a serialized snapshot of the pragma.

---

## User Overlays

User-defined reader macros and atoms are stored separately from pragma-defined
grammar.

On pragma reload:

1. Rebuild pragma-defined grammar and operator tables.
2. Re-apply user-defined overlays.

This ensures that resetting or replacing a pragma does not silently discard
user-level customizations.

---

## Minimal Summary

### Special Form

`(compile/pragma key: value ...)`

* Validates keys and builds a new immutable pragma at compile time.
* At eval time, replaces `module.__pragma__` and calls the reader and compiler
  reload methods.
* Affects only future parsing and compilation.

### Reader Keys (v0.1)

* `layout` — enables layout (indentation) reading.
* `layout_implicit_calls` — multiple values on one layout line form a call.
* `bracket_pairs` — `[ ]` reads into distinct bracket-pair / bracket-nil values
  (vs. plain pairs).

### Compiler Keys (v0.1)

* `resolution_mode` — selects resolution semantics: Koskari (strict), Scheme
  (unsatisfied writes promote to global), or Python (implicit local on first
  write unless explicit global). Implemented via pluggable `ResolutionPragma`.
* `bracket_calls` — policy for a bracket-pair reaching call position
  (`ignore` / `warn` / `error`).
* `tco` — enables the tail-call optimization.

### Reserved Compiler Keys (validated, inert in v0.1)

* `convert_fast` — the `convert-fast` compression pass (not yet built).
* `compress` — the bytecode compression pass.

The pragma model is intentionally minimal, forward-only, and module-local.
