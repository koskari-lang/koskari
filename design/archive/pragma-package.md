# Koskari Design: Package Pragma

## Status

Implemented (MVP -- see **Implementation** below). Formerly "meta-pragma."
Builds on the implemented pragma system ([../PRAGMA.md](../PRAGMA.md)): a
package declares a default pragma that its sub-packages and sub-modules
inherit, so a whole distributed package can set feature defaults without
repeating them in every file.

## Goal

Let a package establish a **package-wide default pragma** that flows down the
package tree. A module deep in the package starts from its nearest ancestor's
package pragma instead of the bare built-in default; its own `(pragma …)`
declarations still override, forward-only. Sub-packages may refine the default
for their own subtree.

This reuses `ModulePragma` and its `rebuild` machinery wholesale — the only new
pieces are a slot, a surface form, and loader plumbing.

## The slot and the surface form

- **`__package_pragma__`** — a `ModulePragma` bound on a package, holding the
  default that descendants inherit. It is a *settings template*: nothing
  subscribes to it (no reader/compiler watchers), so it needs no watcher set of
  its own.
- **`(package-pragma [ns/key value …] …)`** — a new surface macro, same setting
  grammar as `pragma`. Following the established two-slot rule, it writes
  **both**:
  - `__package_pragma__` — what descendants inherit, and
  - `__pragma__` — so the declaring module (typically a package `__init__`) also
    runs under the default it sets.

  The convention is **package-pragma first, then local `(pragma …)` overrides**:
  because `package-pragma` also writes the local slot, a later local override
  wins only if it comes after. A package `__init__` that wants stricter settings
  for itself than it hands down declares the package default first, then a local
  `(pragma …)`.

## Loader augmentation

Python's import system runs a package's `__init__` before any of its
submodules, so a parent's `__package_pragma__` is already bound by the time a
child loads — **no header scan or extra phase** is needed.

When the loader initializes a module, the module-load seam
(`build_module_load_facets`, `koskari/module.py`) resolves the **active source**:

1. Find the parent package (via `__package__` / the module-name hierarchy).
2. If the parent has a `__package_pragma__`, use **a copy of its settings** as
   this module's default `__pragma__` (seed); otherwise use the built-in
   default.
3. Store **both** on the module: the working `__pragma__` (which the reader and
   compiler subscribe to, with a **fresh** watcher set — watchers never cross
   modules) and a `__package_pragma__` (the inherited default) so this module's
   own descendants can pull it down further.

### Pull-down and override

A package's stored `__package_pragma__` is *the parent's resolved package pragma
plus this package's own `(package-pragma …)` declaration*. So:

- A module that never declares `package-pragma` simply carries the inherited one
  forward — a sub-sub-package keeps pulling the same value down.
- A sub-package that declares its own `package-pragma` refines the default for
  its subtree (accumulating onto the inherited one), without affecting its
  siblings or its parent.

Because each level stores the already-accumulated result, a deep module only
ever reads its **immediate** parent's `__package_pragma__` — no walking the full
chain at load time.

## Invariants

- **Settings inherit, watchers do not.** Seeding copies the package pragma's
  *settings* into a child's `__pragma__`; the child's reader/compiler subscribe
  fresh. A parent's reader is never announced about a child's change.
- **A default, not live inheritance.** `__package_pragma__` is a starting value,
  fully overridable per module. This is consistent with the non-goal of
  cross-module leakage of a *live* pragma — descendants get a snapshot, not a
  link to the parent's runtime pragma.
- **Only a module's own `(pragma …)` travels as its declaration.** The package
  pragma is the package's own recorded default; a consumer importing the package
  gets whatever that package baked in, which is the intended behavior for a
  distributed wheel.

## Edges

- A namespace package or one with no `__init__` has nowhere to declare a package
  pragma; the loader falls through to *its* parent's `__package_pragma__` (or
  the built-in default) — the chain skips the empty link.
- `package-pragma` in a non-package (leaf) module still writes the local
  `__pragma__` via the shared write, but its `__package_pragma__` is never
  consulted (no descendants) — a candidate future lint.

## Relation to the CLI pragma

Both feed the same `build_module_load_facets` active-source chokepoint. For the
**entry** module the CLI seed wins over the package pragma
([pragma-cli.md](pragma-cli.md)); for **imported** modules there is no CLI seed,
so the package pragma (or built-in default) is the source.

## Implementation (MVP)

The implemented shape is deliberately minimal; everything above falls out of two
small pieces plus the immutability of `ModulePragma`:

- **The macro is a wrapper.** `compile/package-pragma`
  (`koskari/bootstrap/_pragma.ki`) expands to the ordinary `pragma` set form
  followed by `(setq __package_pragma__ __pragma__)`. Because `rebuild`
  replaces rather than mutates, the `setq` captures a frozen snapshot: later
  local `(pragma ...)` overrides produce new objects and leave the recorded
  default untouched, and repeated `package-pragma` declarations accumulate
  naturally (each starts from the current `__pragma__`, which includes the
  prior declaration).
- **Seeding and stamping live at the facet seam.** `find_package_pragma`
  (`koskari/module.py`) walks the module-name hierarchy through
  `sys.modules`, skipping ancestors without the slot (namespace packages,
  plain Python packages). `build_module_load_facets` seeds the fresh
  module's `__pragma__` from the found pragma via `ModulePragma.copy()` (a
  settings copy with a fresh, empty watcher set), and -- only when the
  loading module is itself a package (`__path__` bound) -- stamps
  `__package_pragma__` with another copy, so silent intermediate packages
  pull the default down without declaring anything.
- **Persistence is free.** `__package_pragma__` is an ordinary module
  binding, deliberately absent from `MODULE_LOAD_FACET_NAMES`, so it
  survives the end-of-load detach that removes `__pragma__` -- it is exactly
  the pragma trace a later child load reads.

**Known limitation:** seeding copies resolved settings *values*; it never
runs setting functions and never announces. A setting whose effect is a side
effect of applying it -- today only `compiler/enable-asserts`, a
process-global gate in a bootstrap closure -- is not honored by inheritance.
The fix is to make the assert expansion read the compiling module's
`__pragma__` at expand time; see the TODO entry. The general invariant for
new settings: effects must derive from the pragma value (read at expand
time, or derived by the per-module reader/compiler), never from the act of
applying the setting.

## Open questions

- ~~Storage of `__package_pragma__` on non-package modules~~ — resolved:
  only packages carry the slot; a leaf's would never be consulted.
- ~~Whether `package-pragma` should be settable more than once in one
  `__init__`~~ — resolved: repeated declarations accumulate (see
  **Implementation**).
- Serialization: whether a `.kio` package artifact records its resolved package
  pragma (ties into the deferred `.kio` pragma snapshot).
- A submodule import placed *before* the `package-pragma` declaration in an
  `__init__` seeds that child from the attach-time stamp (the ancestors'
  defaults), not the declaration below it — forward-only, but a footgun.
  Convention: declare `package-pragma` first, before submodule imports; a
  candidate lint alongside the leaf-declaration one.

## Non-goals

- Cross-module inheritance of a *live* pragma (only a settings snapshot).
- Reaching across package boundaries other than parent → child.
- Session/run defaults — that is [pragma-cli.md](pragma-cli.md).

Ref: [../PRAGMA.md](../PRAGMA.md), [pragma-cli.md](pragma-cli.md).
