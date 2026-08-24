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
  *defaults* are a separate, explicit mechanism — see **Package Pragma**).
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
| `reader/index-brackets` | reader | `True` | `True` | bool | a `[ ... ]` group attached directly to a value (no whitespace) desugars into `(item VALUE KEY)`, chaining left to right |
| `reader/dot-attrs` | reader | `True` | `True` | bool | `.` reads as attribute access, both within an atom (`foo.bar`) and attached to any value (`(get-fn x).baz`), producing `(compile/attr ...)`; off, a dot is inert in both spellings |
| `reader/string-segments` | reader | `True` | `True` | bool | a `-` attached to a string literal's closing quote continues it into the next quoted segment, joining into `(compile/#str ...)` |
| `reader/layout` | reader | `True` | `True` | bool | layout (indentation) block reading |
| `reader/implicit-calls` | reader | `True` | `True` | bool | multiple values on one layout line form an implicit call |
| `reader/pass-none` | reader | `False` | `True` | bool | the `pass` symbol reads as the `None` literal (mention-to-enable) |
| `reader/fences` | reader | `True` | `True` | bool | triple-backtick fences read into `compile/apply-fence` forms |
| `reader/decorators` | reader | `True` | `True` | bool | leading `@` layout lines read as decorators, lowering to `compile/decorate`; a raw reader starts with this off, and `(compile/decorate ...)` stays available as a macro regardless |
| `compiler/resolution` | compiler | `koskari` | *(required)* | `koskari` / `scheme` / `python` | name-resolution semantics for writes |
| `compiler/call-brackets` | compiler | `warn` | `warn` | `ignore` / `warn` / `error` | policy when a bracket-pair reaches call position |
| `compiler/stray-colon` | compiler | `warn` | `warn` | `allow` / `warn` / `error` | policy when a bare `:` symbol is compiled as a name |
| `compiler/keyword-literals` | compiler | `warn` | `warn` | `allow` / `warn` / `error` | policy when an unquoted keyword is compiled as a literal value |
| `compiler/enable-asserts` | compiler | `True` | `True` | bool | `(assert …)` forms expand to their check; off, they compile away entirely (Python `-O` semantics) |
| `compiler/bare-control-words` | compiler | `convert` | `convert` | `convert` / `warn` / `error` | policy when a bare `return`, `continue`, `break`, `raise`, or `yield` symbol is compiled: silently expand to the call form, warn and expand, or refuse |
| `compiler/rewrite-limit` | compiler | `32` | `32` | int, `0` = unlimited | settling limit for the full_compile macro-expansion rewrite loop; a chain past the limit reports divergent growth, while `0` disables the limit and leaves the equality cycle check in force |
| `compiler/tco-enable` | compiler | `True` | `True` | bool | tail-call optimization |
| `compiler/convert-fast` | compiler | `True` | `True` | bool | VAR→FAST local-access lowering pass |
| `compiler/compress` | compiler | `False` | `False` | bool | bytecode compression pass — registered but inert (pass not yet built) |
| `compiler/cache-expression` | compiler | `False` | `True` | bool | per-site opcode caches on top-level expression code (mention-to-enable) |
| `compiler/cache-function` | compiler | `True` | `True` | bool | per-site opcode caches on function and lambda code |
| `compiler/protect-code` | compiler | `False` | `True` | bool | each Code block page-aligned and mprotect-sealed read-only at construction (mention-to-enable) |
| `compiler/pack-module` | compiler | on, unbounded | on, unbounded | bool + `max-count:` / `max-bytes:` | durable code relocated into shared, singly-sealed arenas when the module seals — see [archive/code-object-arena-packing.md](archive/code-object-arena-packing.md) |
| `pragma/strict-settings` | pragma | `True` | `True` | bool | an unknown setting raises (else warns) |
| `pragma/strict-watchers` | pragma | `True` | `True` | bool | a watcher error raises (else warns) |

`reader/*` settings drive the reader, `compiler/*` the compiler, and `pragma/*`
govern the pragma machinery itself. Third-party tools register their own
`toolname/*` settings the same way (see **The Registry**).

`reader/index-brackets`, `reader/dot-attrs`, and `reader/string-segments` gate
one family: **suffix reader macros**, which attach postfix syntax to a value
already read. Each is a registered macro whose predicate consults its own
setting, so the three compose without knowing about each other and a fourth can
be added the same way — see
[archive/suffix-macros.md](archive/suffix-macros.md).

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
and registers `compiler/call-brackets`. Registration is not Python-only: the
assert expansion gate registers `compiler/enable-asserts` from the bootstrap
itself (`koskari/bootstrap/_assert.ki`), calling `koskari.pragma.register` with
a `demand`-wrapped Koskari closure as the setting function. The surface forms
(`compile/pragma`, `compile/package-pragma`) are `define-syntax` macros in
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

`compile/pragma` and `compile/package-pragma` are `define-syntax` **macros**;
their short forms `pragma` and `package-pragma` resolve through the compiler's
`compile/` prefix — the same surface convention MACROS.md uses (e.g. `?` for
`compile/?`).

### Catalog

See **[Available settings](#available-settings)** above for the current catalog
with baselines, bare-form values, and domains. Each is registered `active` —
its refresh reconfigures the reader or compiler — except `compiler/compress`,
whose pass is not yet built (registered but inert), and
`compiler/enable-asserts` and `compiler/bare-control-words`, whose setting
functions are pure coercers: their gates read the value per expansion through
`active-pragma`, so no facet refresh or watcher state is involved.
`compiler/convert-fast`,
`compiler/tco-enable`, and `compiler/compress` are candidates to collapse into a
single graduated `compiler/optimize` level later (see **Future**).

---

## Value Model

A setting behaves like a small function, and **declaring it is calling it.**

* **Do not call it** (setting absent) → it keeps its **baseline**. The baseline
  is dynamic: the built-in default, unless an inherited package pragma or an earlier
  declaration in this module already moved it.
* **Call it** (setting present) → parameters fill from the given arguments; any
  omitted argument takes its **parameter default**.

Baseline and parameter default are independent — `compiler/protect-code` has
baseline `False` but parameter default `True`:

* not mentioned → stays at baseline (`False` — sealing is off);
* `[compiler/protect-code]` → `enabled` omitted → parameter default → `True`;
* `[compiler/protect-code False]` → `enabled` explicit → `False`.

That independence is the useful part: a feature can stay quiet until named, but
the moment you opt in by naming it you get its sensible active value with no
argument to recall: mention-to-enable (`reader/pass-none` and
`compiler/cache-expression` work the same way). Most other settings keep the
two aligned — e.g. `compiler/call-brackets` has baseline `warn` and parameter
default `warn`, so bare mention is a no-op and only an explicit `'error` or
`'ignore` moves it.

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

* **Pull** — read the pragma on demand. For consumers that hold no derived
  state: user macros, passive features, introspection. No subscription. The
  read has two spellings with different subjects: `(pragma KEY)` reads
  `__pragma__`, the pragma of the module the form is *written in*, while
  `(active-pragma KEY)` reads the pragma of the module *being compiled*.
  Inside a macro body — a closure over its defining module — the second is
  almost always the correct one: the compiler binds its `ModulePragma` into
  the `active-compiler-pragma` parameter around each compilation turn (a
  host-side `parameterize` in `Compiler.compile`), so an expansion-time read
  answers for the consumer. The assert gate and the bare-control-word gate
  are the reference consumers.
* **Push** — subscribe and get announced. For consumers that cache derived state
  and must rebuild: the reader (atom tables) and compiler (flags, resolution
  instance). They cannot re-derive on every access, so they want notification.

### Intra-module only

Watchers propagate across **replacements within one module's load** and *never*
across modules. When a child seeds its baseline from a parent package's
`__package_pragma__`, it copies the **settings** but starts a **fresh watcher
cell** — otherwise the parent's reader would be announced about the child's
changes, a cross-module leak. The rule is precise: **replacement shares the
cell; package-pragma inheritance does not.**

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
pragma  = <active source>                 ; default, or inherited package pragma
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
2. **Package pragma** — a package-wide baseline inherited by sub-packages and
   sub-modules, declared with `(package-pragma …)` in a package `__init__`.
   See [pragma-package.md](archive/pragma-package.md).
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

## Package Pragma

**Implemented.** Design doc: **[pragma-package.md](archive/pragma-package.md)**
(and the CLI seed in [pragma-cli.md](language/pragma-cli.md)). This mechanism
was formerly called "meta-pragma"; the current names are `package-pragma` /
`__package_pragma__`.

A package declares defaults for all modules within it — e.g. a Koskari-written
package distributed as a wheel sets a baseline at the top of its `__init__`:

* `compile/pragma` → writes `__pragma__` (this module's local pragma).
* `compile/package-pragma` → the `pragma` set form plus
  `(setq __package_pragma__ __pragma__)`, so the declaring `__init__` also
  runs under the default it hands down. Declare it before local `(pragma …)`
  overrides and before importing submodules.

No header scan or new phase: Python runs a package's `__init__` before its
submodules, so the loader seam (`build_module_load_facets`) seeds each fresh
module from the nearest ancestor package's `__package_pragma__` — a settings
copy with a fresh watcher cell — and stamps packages with an inherited copy so
silent intermediate packages keep pulling the default down. A package pragma
is a **default**, overridable per module, not inheritance of another module's
live pragma. Details, invariants, and edges: the design doc.

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
5. **Surface macros** — `compile/pragma` (and `compile/package-pragma`) as
   `define-syntax` macros in `_pragma.ki`; the short forms `pragma` /
   `package-pragma` resolve via the `compile/` prefix. Each expands to a
   `(setq __pragma__ (.rebuild __pragma__ …))`; `rebuild` validates against the
   registry and announces at eval time.
6. **Defer** — `.kio` pragma serialization, ~~the package pragma loader
   integration~~ (landed; see [pragma-package.md](archive/pragma-package.md)),
   CLI/REPL sources, profiles, and the reproducibility lint.

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
