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
(compile/pragma key value ...)
```

Properties:

* Always parseable under the base grammar.
* May appear multiple times.
* Later invocations replace the previous pragma.
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

When `(compile/pragma ...)` is encountered during load:

1. A new immutable pragma object is constructed (defaults + overrides).
2. `module.__pragma__` is replaced.
3. `module.__reader__.reload_pragma(module.__pragma__)` is called.
4. `module.__compiler__.reload_pragma(module.__pragma__)` is called.

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

## Atom Gating (Initial Focus)

The initial use case for pragma is gating syntax-level atoms.

### Candidate Gated Forms

* `->` deref-attr separator
* `&foo` and place-aware `&foo.bar`
* `*identifier`-style deref atoms (if controlled)

Model:

* The reader rebuilds atom recognizers and operator tables based on the current
  pragma.
* The compiler lowers based on flags enabled at reload time.

---

## Minimal Implementable Subset (v0.1)

1. Provide `koskari.pragma` defining an immutable `KoskarV01Pragma` schema.
2. Implement `(compile/pragma ...)` to construct and install a new pragma.
3. On installation, call parser and compiler reload hooks.
4. Gate the following parser features:

   * `parser_atom_ref`
   * `parser_atom_deref_attr`
5. Gate the following compiler feature:

   * `compiler_pass_convert_fast`
6. Store user-defined reader macros and atoms separately so they can be
   re-applied after a pragma reload.

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

`(compile/pragma key value ...)`

* Builds a new immutable pragma object.
* Replaces `module.__pragma__`.
* Calls reader and compiler reload methods.
* Affects only future parsing and compilation.

### Parser Flags (v0.1)

* `parser_atom_ref` — enables `&foo` and place-aware `&foo.bar`.
* `parser_atom_deref_attr` — enables `foo->bar`.

### Compiler Flags (v0.1)

* `compiler_pass_convert_fast` — enables or disables the `convert-fast`
  compression pass.

### Future: resolution_mode

* `resolution_mode` — selects resolution semantics: Koskari (strict), Scheme
  (unsatisfied writes promote to global), or Python (implicit local on first
  write unless explicit global). Implemented via pluggable `ResolutionPragma`.

The pragma model is intentionally minimal, forward-only, and module-local.
