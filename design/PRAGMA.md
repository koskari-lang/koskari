# Koskari Pragma

Rollup for the module pragma system: how to set a pragma, the settings that
exist today, and the model behind them. For the surface macro see
[MACROS.md](MACROS.md) (`pragma`); for the bracket convention a pragma gates see
[SYNTAX.md](SYNTAX.md).

## Overview

The Koskari pragma model is the official home for the language's optional
features — the flags and switches that gate reader and compiler behavior. It
provides a deterministic, module-scoped mechanism for turning features on, off,
or into a different mode, without global state, runtime toggles, or per-form
checks.

Pragmas are:

* **Module-scoped** — each module owns its own pragma; one module's pragma does
  not affect another's.
* **A value plus an envelope** — the *settings* are an immutable value that is
  replaced (never mutated) on change; a *watcher set* rides alongside as runtime
  plumbing and is not part of the value.
* **Load-time effective, forward-only** — a change applies only to subsequent
  reading and compilation; nothing already read or compiled is rewritten.
* **Extensible** — features are *registered*, not hardcoded. The reader, the
  compiler, and third-party tools all register their own features into one
  catalog.

Non-goals:

* Runtime feature toggles after module load.
* Header-scan or multi-phase compilation.
* Implicit inheritance of a module's *live* pragma across modules (package-level
  *defaults* are a separate, explicit mechanism — see **Package Meta-Pragma**).
* Region/lexical scoping of a pragma (`push`/`pop`); the module is the unit.
  Use a submodule for finer scope.

---

## Setting a pragma

Pragmas are set with the `compile/pragma` macro, usually written with its short
form `pragma` (resolved through the compiler's `compile/` prefix, like `?` for
`compile/?`). Each setting is a bracketed data fragment — a slash-namespaced key
followed by positional and/or keyword values; a bare `[key]` applies the
setting's parameter defaults:

```
(pragma [compiler/call-brackets 'error]
        [reader/brackets False]
        [compiler/resolution 'scheme])
```

The form is forward-only and module-scoped: it rebuilds the module's
`__pragma__` and the change applies to everything read and compiled *after* it,
never retroactively.

The same macro also **reads** the pragma:

* `(pragma NS/KEY)` — the current value of one setting (expands to
  `(item __pragma__ "NS/KEY")`).
* `(pragma)` — the module's live `ModulePragma` object (`__pragma__`).

And `help` reaches both:

* `(help pragma)` — the current settings and every registered setting with its
  default.
* `(help (pragma NS/KEY))` — one setting: the signature and docstring of its
  registered function.

---

## Available settings

Every registered setting, its target facet, its **baseline** (value when the
setting is not declared), the value a bare `[key]` produces, and its domain:

| Setting | Target | Baseline | Bare `[key]` | Domain | Effect |
|---|---|---|---|---|---|
| `reader/brackets` | reader | `True` | `True` | bool | `[ ]` reads into distinct bracket-pair values (vs. plain pairs) |
| `reader/layout` | reader | `True` | `True` | bool | layout (indentation) block reading |
| `reader/implicit-calls` | reader | `True` | `True` | bool | multiple values on one layout line form an implicit call |
| `compiler/resolution` | compiler | `koskari` | *(required)* | `koskari` / `scheme` / `python` | name-resolution semantics for writes |
| `compiler/call-brackets` | compiler | `ignore` | `warn` | `ignore` / `warn` / `error` | policy when a bracket-pair reaches call position |
| `compiler/tco-enable` | compiler | `True` | `True` | bool | tail-call optimization |
| `compiler/convert-fast` | compiler | `True` | `True` | bool | VAR→FAST local-access lowering pass |
| `compiler/compress` | compiler | `False` | `False` | bool | bytecode compression pass — registered but inert (pass not yet built) |
| `pragma/strict-settings` | pragma | `True` | `True` | bool | an unknown setting raises (else warns) |
| `pragma/strict-watchers` | pragma | `True` | `True` | bool | a watcher error raises (else warns) |

`reader/*` settings drive the reader, `compiler/*` the compiler, and `pragma/*`
govern the pragma machinery itself. Third-party tools register their own
`toolname/*` settings the same way (see **The Registry**).

---

## Core Concepts

* **Setting** — one namespaced, configurable feature (e.g. `compiler/call-brackets`).
  A setting takes zero or more **parameters**, optionally keyworded.
* **Feature registration** — declaring that a setting *exists*: its name, owner,
  target, domain, defaults, and stability. Done at *import* time. Static.
* **Watcher subscription** — a stateful consumer asking to be *notified* when the
  active pragma changes. Done at *construction* time. Runtime.
* **Baseline** — a setting's value when it is *not* declared (subject to
  inheritance and earlier declarations in the same module).
* **Parameter default** — the value of an *omitted argument* when a setting *is*
  declared.
* **Announce** — a replacement pragma notifying its watchers that it is now
  active, so they can refresh derived state.

Keep the two registrations distinct: feature registration populates the
*catalog* ("what can be set"); watcher subscription wires up *notification*
("tell me when the active values change"). The reader does both — it registers
its `reader/*` features at import and subscribes itself when constructed.

---

## Home: `koskari.pragma`

The pragma framework lives in a **top-level leaf package**, `koskari.pragma`,
because both the reader and the compiler must register into it — a
`reader → compile` import to reach the registry would be backwards layering.

```
koskari.pragma          (leaf: ModulePragma, the registry, PragmaFeature, stability tiers)
   ↑              ↑                 ↑
koskari.reader   koskari.compile    third-party packages
 registers        registers          register their own
 reader/* keys    compiler/* keys    features
```

`koskari.pragma` imports nothing heavy. Reader, compiler, and outsiders all
import *it* and register inward, so there are no cycles and core gets no special
status in the mechanism — it is simply the first registrant.

Behaviors stay with their owners: the `ResolutionPragma` implementations remain
compiler-side (they manipulate `ByteCodeFragment` internals) and merely
*register* `compiler/resolution`; the bracket-call check stays in `compile_pair`
and registers `compiler/call-brackets`. The surface forms (`compile/pragma`,
`compile/meta-pragma`) are `define-syntax` macros in
`koskari/bootstrap/_pragma.ki`. Model at the bottom, behaviors with their
owners, syntax in the bootstrap.

---

## The Registry

The registry is the single source of truth and the thing every other surface
derives from — validation, refresh dispatch, introspection, completion, and the
generated documentation table all read from it.

### Feature metadata

Each registered feature declares:

* **name** — the slash-namespaced key (e.g. `compiler/call-brackets`).
* **owner** — the namespace that registered it (`reader`, `compiler`, or a
  third-party prefix). Core owns bare prefixes; outsiders use their own
  (`mytool/trace`).
* **target** — what it reconfigures: the reader, the compiler, or nothing.
* **kind** — **active** or **passive**:
  * **active** — carries a refresh effect; when the pragma changes, the relevant
    watcher (reader/compiler) re-derives state. Active features further split
    into *flag* (a scalar set-and-done) and *drop-in* (a behavior object or
    table that is rebuilt).
  * **passive** — no refresh effect; just a registered key that sits on
    `__pragma__` for its owner's own macros to read. A third-party tool needs no
    privileged access: it registers a passive key and reads it in its macros.
* **baseline** — the built-in value when the setting is undeclared.
* **parameters** — each with a name, a value domain, and a **parameter default**.
* **stability** — `stable`, `experimental`, or `reserved`; optionally a
  `__future__`-style lifecycle (available-since / default-since version).
* **summary** — one line for diagnostics and generated docs.

### Registration rules

* **Import-time and idempotent.** Re-registering an identical definition is a
  no-op; a *conflicting* redefinition of the same name is an error.
* **Namespacing.** Bare prefixes (`reader/…`, `compiler/…`) are reserved for
  core; third parties use their own prefix. A soft rule with a warning, a hard
  error only on actual collision.
* **Unknown key = not registered yet.** A key is unknown exactly when its
  registrant has not been imported before the pragma form compiles — the same
  forward-only ordering as macros. The diagnostic should hint: "key `X` not
  registered — is its providing package imported?"

---

## Keys and Surface Syntax

### Surface

A pragma is a series of bracketed **data** settings — each a non-call fragment,
so a `(pragma …)` form is written entirely in brackets-as-data and passes its
own `compiler/call-brackets` check:

```
(pragma [reader/brackets]
        [compiler/call-brackets 'warn]
        [compiler/tco-enable False])
```

* Keys are slash-namespaced, kebab-case, one token each — the key's identity as
  a single string is what the registry, error messages, `__pragma__` lookups,
  and completion all use. (Do **not** group by namespace into nested lists; the
  slash *is* the grouping, at the token level.)
* Values are ordinary **expressions** — the macro lowers each into a `values`
  call, so they are evaluated when the setting is applied. A literal symbol must
  therefore be quoted (`'error`, `'scheme`); booleans, numbers, and strings are
  self-carrying, and a computed value (a variable, a call) works too.
* A setting's parameters may be positional, or keyworded for clarity:

```
[compiler/optimize speed: 3 safety: 0]              ; keyword params
[compiler/call-brackets 'error exempt: [let cond]]   ; positional value + keyword options
```

Three setting shapes: `[key]` (bare → parameter defaults), `[key value …]`
(positional), and `[key value? kw: v …]` (positional value plus keyword
options). Nesting is reserved for a single feature's own parameters (the
`optimize` case), never for owner grouping.

`compile/pragma` and `compile/meta-pragma` are `define-syntax` **macros**; their
short forms `pragma` and `meta-pragma` resolve through the compiler's `compile/`
prefix — the same surface convention MACROS.md uses (e.g. `?` for `compile/?`).

### Catalog

See **[Available settings](#available-settings)** above for the current catalog
with baselines, bare-form values, and domains. Each is registered `active` —
its refresh reconfigures the reader or compiler — except `compiler/compress`,
whose pass is not yet built (registered but inert). `compiler/convert-fast`,
`compiler/tco-enable`, and `compiler/compress` are candidates to collapse into a
single graduated `compiler/optimize` level later (see **Future**).

---

## Value Model

A setting behaves like a small function, and **declaring it is calling it.**

* **Do not call it** (setting absent) → it keeps its **baseline**. The baseline
  is dynamic: the built-in default, unless an inherited meta-pragma or an earlier
  declaration in this module already moved it.
* **Call it** (setting present) → parameters fill from the given arguments; any
  omitted argument takes its **parameter default**.

Baseline and parameter default are independent — `compiler/call-brackets` has
baseline `ignore` but parameter default `warn`:

* not mentioned → stays at baseline (`ignore` — the check is off, pure
  convention);
* `[compiler/call-brackets]` → `level` omitted → parameter default → `warn`;
* `[compiler/call-brackets 'error]` → `level` explicit → `error`.

That independence is the useful part: leaving the check unmentioned keeps it
silent, but the moment you opt in by naming it you get a sensible active default
(`warn`) instead of having to recall the severity vocabulary — while an explicit
value still wins. (A purely illustrative inverse would be a feature whose
baseline is *off* but whose bare form turns it *on*: mention-to-enable.)

### Replace, not merge

A declaration is a *full* call: stated arguments win, omitted arguments take
their parameter defaults, and the result **replaces the setting's whole value**.
For a multi-parameter setting, `[compiler/optimize speed: 3]` resets `safety` to
its parameter default — it does not inherit `safety` from an earlier call. The
granularity of "what persists vs. resets" is the **setting**, matching the
replace-by-key layering: an *undeclared* setting keeps its prior value; a
*declared* setting is recomputed fresh.

A pragma declaration means "make it work like THIS," not "adjust from where it
is." Accumulating onto current settings is deliberately outside the DSL and
discouraged — so a setting's resulting state is always readable from the call
plus known parameter defaults, with no need to know history.

---

## The Pragma Object: Registration and Refresh

### Structure

A pragma is an **immutable settings value** plus a reference to a **shared
watcher cell**:

```
pragma          = (settings_value, watcher_cell)
replace(*over)  → (settings.with(over), SAME watcher_cell)   ; intra-module
subscribe(w)    → watcher_cell.add(w)
announce()      → for w in watcher_cell: w(self)
```

The settings half is a pure immutable value — clean equality (over settings
only) and clean `.kio` serialization (settings only; watchers are runtime). The
watcher cell is shared by reference across a module's whole pragma lineage, so a
subscription persists across every `replace` without a copy step, and
subscribing to *any* version registers everyone (no stale-copy hazard).

A **watcher** is just a callable taking the now-active pragma — `w(pragma)` — so
the reader and compiler pass a bound method; no required base class.

### Two consumption models

Pull and push are not competitors; they serve different consumers, and both are
supported:

* **Pull** — read `__pragma__` on demand. For consumers that hold no derived
  state: user macros, passive features, introspection. No subscription; just
  read the slot at expand time. This is all a passive feature needs.
* **Push** — subscribe and get announced. For consumers that cache derived state
  and must rebuild: the reader (atom tables) and compiler (flags, resolution
  instance). They cannot re-derive on every access, so they want notification.

### Intra-module only

Watchers propagate across **replacements within one module's load** and *never*
across modules. When a child seeds its baseline from a parent package's
`__meta_pragma__`, it copies the **settings** but starts a **fresh watcher
cell** — otherwise the parent's reader would be announced about the child's
changes, a cross-module leak. The rule is precise: **replacement shares the
cell; meta-pragma inheritance does not.**

### Teardown and granularity

* **Cycles** — a pragma references its watchers; the reader/compiler should
  retain only the state they derived, not the pragma object, and
  `detach_module_load_facets` clears the watcher cell at end of load (or the cell
  holds weak references).
* **Coarse announce** — every watcher is notified on any change; each watcher
  self-filters (diff its own keys, or rebuild idempotently). Per-key
  subscription is a possible future optimization; pragma changes are rare
  (load-time), so coarse is fine.

---

## Load-Time Lifecycle

Koskari interleaves read → compile → eval per top-level form; there is no
separate header phase.

### Initialization (the seam)

Module and REPL init build the pragma **first**, then construct the reader and
compiler *with* it:

```
pragma  = <active source>                 ; default, or inherited package meta-pragma
module.__pragma__ = pragma
reader  = Reader(pragma)                  ; subscribes itself as a watcher
compiler = Compiler(pragma)               ; subscribes itself as a watcher
```

This single chokepoint is where every pragma *source* plugs in (see
**Sources and Precedence**). It is worth establishing even while the only source
is the built-in default, so later sources are additive rather than a retrofit.

### Update

`compile/pragma` is a `define-syntax` macro (in `koskari/bootstrap/_pragma.ki`).
Its *set* form expands mechanically to

```
(setq __pragma__ (.rebuild __pragma__ #('NS/KEY (values VALUE …)) …))
```

one `#('NS/KEY (values …))` tuple per setting (the read forms `(pragma NS/KEY)`
and `(pragma)` expand to `(item __pragma__ "NS/KEY")` and `__pragma__`
respectively, and do no work). For the set form all the work happens at **eval
time**, when that form runs during load:

1. `rebuild` starts from the current settings and, for each tuple, resolves the
   setting against the registry and applies its function to the `values`
   (parameter defaults fill omitted arguments). An unknown key raises — or
   warns, per `pragma/strict-settings`; an out-of-domain value raises from the
   setting's own function (e.g. `BracketCalls(level)`).
2. `rebuild` announces the resulting immutable pragma to its watchers.
3. `setq` installs it as `__pragma__`.

The macro does **not** name the reader or compiler — the announce does. They
refresh because they *subscribed*; a stateful third-party watcher gets the same
notification for free.

Because load is strictly read → compile → **eval** → read-next, the *next*
top-level form is read by the refreshed reader and compiled by the refreshed
compiler. Applying at eval time (not at compile time of the pragma form) is
deliberate: it gives identical behavior in the REPL, where each entry is
independently read, compiled, and evaluated.

### Effects

* Changes apply only to subsequent tokenization, parsing, and compilation.
* No retroactive rewriting; no per-form pragma checks.
* A refresh fully rebuilds the affected watcher's derived state.

---

## Sources and Precedence

The *registry* defines what keys exist; **sources** define where a key's value
comes from before the module's own declaration speaks. Lowest to highest:

1. **Built-in default** — what the registry declares as each feature's baseline.
2. **Package pragma** *(future)* — a package-wide baseline inherited by
   sub-packages and sub-modules. See [pragma-package.md](pragma-package.md).
3. **CLI / REPL session default** *(future)* — a pragma seeded for the entry or
   REPL module from flags or a settings file. See [pragma-cli.md](pragma-cli.md).
4. **Module declaration** — `(pragma …)` in the module; always wins; forward-only.

The invariant that makes this safe to distribute: **only #4 travels with the
code.** CLI/REPL/package defaults shape authoring and running, never the
semantics another consumer of the module sees. Users should be encouraged to
declare pragma explicitly in each distributed module.

Two guards fall out and are worth reserving namespace for:

* **Reproducibility lint** *(future)* — a module that *relies* on a non-default
  setting it never declared is leaning on an ambient default and will behave
  differently for an importer. A natural lint.
* **`.kio` snapshot** *(future)* — a compiled artifact recording its *effective*
  pragma is reproducible regardless of the ambient default.

---

## Package Pragma *(future)*

> Now a dedicated design doc: **[pragma-package.md](pragma-package.md)** (and the
> CLI seed in [pragma-cli.md](pragma-cli.md)). This mechanism was formerly called
> "meta-pragma"; the current names are `package-pragma` /
> `__package_pragma__`. The sketch below predates that rename — read the design
> doc for the authoritative naming.

A package may declare defaults for all modules within it — e.g. a Koskari-written
package distributed as a wheel sets a baseline at the top of its `__init__`.

### Two forms, differing only in which slots they write

* `compile/pragma` → writes `__pragma__` (this module's local pragma).
* `compile/meta-pragma` → writes **both** `__pragma__` *and* `__meta_pragma__`,
  using the same override application.

So a meta-pragma "applies to itself as well" simply because it also writes the
local slot. The convention is **meta first, then local overrides**, and it is
not arbitrary: because a meta-pragma *also* writes local, a meta declared *after*
a local override on the same key would clobber it — meta-first is the only order
where local wins.

### Package-aware loading, no new phase

Meta-pragma needs no header scan because Python's import system already runs a
package's `__init__` before any of its submodules. So:

* A package's `__init__` runs first; its `(compile/meta-pragma …)` is an ordinary
  load-time side effect that stamps `package.__meta_pragma__`.
* When the loader initializes a submodule, the init seam's "active source"
  becomes `parent_package.__meta_pragma__` (if present) instead of the built-in
  default. It copies the **settings** only and starts a fresh watcher cell.

Each package stores its `__meta_pragma__` as *parent's resolved meta + this
package's own meta declaration*, so a deep submodule reads only its immediate
parent — the baseline is already accumulated.

A meta-pragma is a **default**, overridable per module, not inheritance of
another module's live pragma — which is what keeps it consistent with the
non-goal of cross-module leakage.

### Edges

* A namespace package, or one with no `__init__`, has nowhere to declare a
  meta-pragma and transparently falls through to *its* parent's resolved meta
  (or the built-in default).
* `meta-pragma` in a non-package module still applies locally via the shared
  write, but its `__meta_pragma__` is never consumed (no descendants) — a
  candidate future lint.

---

## Failure Modes and Diagnostics

* **Unknown key** — at eval time `rebuild` misses the registry and raises (or
  warns, per `pragma/strict-settings`); hint whether the providing package is
  imported.
* **Out-of-domain value** — the setting's own function rejects it at eval time
  (e.g. `BracketCalls(level)` / `ResolutionMode(mode)` raise on a bad value).
* **Conflicting settings within a form** — last wins.
* **Pragma after code that assumed a different grammar** — allowed, but only
  forward-effective.

Error messages should include the module name/path, the location of the pragma
form, and a summary (or digest) of the active settings — the same data the
introspection surface reports.

---

## Advertising the Catalog

Because the registry is authoritative, the "official" surface comes almost for
free:

* **Validation** — unknown-key / out-of-domain errors from the registry.
* **Introspection** — a no-arg `(pragma)` reports the active settings; the same
  registry + state feed REPL/LSP completion and diagnostics.
* **Generated docs** — the FEATURES table and reference page render from the
  registry, so the advertised catalog cannot drift from the real keys, defaults,
  and domains.
* **Stability tiers** — every key is `stable` / `experimental` / `reserved`, so a
  flag can be advertised the day it is useful without freezing its spelling; an
  optional `__future__`-style version lifecycle records available-since /
  default-since.

---

## Minimal Implementable Subset (v0.1)

Phased-authoritative: the registry is authoritative for the new pragma path now;
the existing `Compiler` / `Reader` constructor kwargs migrate to derive from
registry defaults in a follow-up.

1. **`koskari.pragma`** — `ModulePragma` (immutable settings value + shared
   watcher cell, with `replace` / `subscribe` / `announce`), the registry and
   `PragmaFeature` metadata (owner, target, kind, baseline, params + defaults,
   stability), `register_feature`, and the stability tiers.
2. **Reader** registers its `reader/*` features at import; subscribes itself when
   constructed; its refresh callback re-derives the layout and bracket-reading
   switches from the announced pragma.
3. **Compiler** registers its `compiler/*` features at import; subscribes itself;
   its refresh callback applies the flags and swaps the resolution class.
4. **Init seam** — reorder module/REPL init to build the pragma first and pass it
   into the reader and compiler.
5. **Surface macros** — `compile/pragma` (and `compile/meta-pragma`) as
   `define-syntax` macros in `_pragma.ki`; the short forms `pragma` /
   `meta-pragma` resolve via the `compile/` prefix. Each expands to a
   `(setq __pragma__ (.rebuild __pragma__ …))`; `rebuild` validates against the
   registry and announces at eval time.
6. **Defer** — `.kio` pragma serialization, the package meta-pragma loader
   integration (the built-in-default branch ships; meta is additive), CLI/REPL
   sources, profiles, and the reproducibility lint.

---

## Future

* **Profiles / presets** — named override bundles (`[profile strict]`) that
  expand to a known set, each key still individually overridable via last-wins.
  The precedence chain is already profile-ready; ship the mechanism before any
  curated profile.
* **Graduated optimize level** — fold `compiler/tco-enable` /
  `compiler/convert-fast` / `compiler/compress` into one `compiler/optimize`
  level (à la `declaim (optimize (speed 3) (safety 0))`) where booleans do not
  compose.
* **Atom gating** — a later wave gating reader atoms (`&foo` ref, `foo->bar`
  deref-attr, `*ident` deref), which need the reader to rebuild atom recognizers
  on refresh and to keep user-defined reader macros/atoms in a separate overlay
  so a refresh does not discard them.
* **Lifecycle versions** — `__future__`-style available-since / default-since
  metadata per feature.
* **`.kio` snapshot** and the **reproducibility lint** described under
  **Sources and Precedence**.

The pragma model is intentionally declarative, forward-only, and module-local,
with one registry as the catalog and a watcher announce as the only refresh
mechanism.
