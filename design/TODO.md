# Koskari design TODO

Centralized list of open design and implementation work. See the referenced
design docs for detail.

---

## Continuations / transfer plans

### Interrupted transfer plans

Interrupted transfer plans are not wired in yet. When a transfer plan is
interrupted (e.g. by an exception or a new transfer being triggered), resumption
should use `kos_build_direct_install_plan` to rebuild or complete the plan; this
path is not yet connected.

Ref: `koskari/lib/dynchain.c` — `kos_build_direct_install_plan`;
[EVALUATOR.md](EVALUATOR.md) (transfer plans, revive_plan);
[interpreter/callcc.md](interpreter/callcc.md) (continuation invoke, plan build).

---

## GIL

### Safe GIL unlocking

Investigate and implement safe voluntary GIL release in the evaluator. Current
code uses a simple opcode-interval counter (every 255 dispatches) and on-exit
release. Design proposal: domain-TLS counter + timer, `maybe_cooperate()` /
`ensure_gil()`, with care for Py_BEGIN_ALLOW_THREADS / Py_END_ALLOW_THREADS
pairing (see design note on cross-block release/reacquire).

Ref: [interpreter/gil-cooperation.md](interpreter/gil-cooperation.md).

---

## Evaluator performance

### Fast local stack

Explore a fast local stack (or stack layout) to speed up the evaluator (C-local
eval state, freeze/thaw; see the design note). The compiler already performs
reducible constant folding, instruction peephole rewrites in `compress`, and
VAR→FAST lowering in `Compiler.complete`; a separate fast operand stack in the
VM remains open.

Ref: [interpreter/freeze-thaw-context-segments.md](interpreter/freeze-thaw-context-segments.md).

---

## Profiling / instrumentation

### Profiling compile-time options and runtime hooks

**Status:** Freelist tracing, **`KOS_TRACE_EVENTS`** (VM/dynchain counters via
`get_trace_report()["events"]`), and **`KOS_TRACE_TIMING`** (per-opcode and
post-handler timing) are implemented. Remaining work: optional opcode-level
histograms.

Ref: [api/tracing.md](api/tracing.md).

---

## Compiler

### Slice atom and slice syntax

Bring over Python-sibilant slicing support in both forms:

- **Slice atom form** — a first-class slice object form (atom-level) that can be
  used in expression trees and lowered consistently.
- **Slice surface syntax** — reader/compiler support for bracket slicing syntax
  (start/stop/step variants) so user code can write slices directly.

Ref: [archive/compiler-python-sibilant-historical.md](archive/compiler-python-sibilant-historical.md) § To Migrate.

### Fenced foreign literal

Add a compile-time fenced foreign-literal mechanism: the reader recognizes
triple-backtick fence syntax and lowers it to `compile/apply-fence`; that
special resolves a handler (e.g. `fence/json`) which transliterates raw fenced
source into Koskari source. V1 scope is reader support, the special, metadata
plumbing, and one built-in JSON fence with narrow extensions (bare identifiers
in value position only).

Ref: [fenced-foreign-literal.md](fenced-foreign-literal.md).

### Bundles and namespace facets

Introduce **bundles** as first-class namespace facet objects: a namespace of
implementation bindings, an exports list, and a scoped activation mechanism
(e.g. `with-bundle`). Exports act as forwarding facades that activate the
bundle and redispatch to bundle-local implementations. Bundles participate in
both compile-time resolution (specials, operators, fence/layout handlers) and
runtime name lookup, so features can ship as cohesive namespaces without
polluting the module.

Ref: [bundles-and-namespace-facets.md](bundles-and-namespace-facets.md).

### Self-hosted compiler and bootstrap (roadmap)

**Status:** Design only; phased implementation.

Split the Python compiler into a **bootstrap** package, reimplement the
compiler in **`.ki`**, compile to **`.kio`** with the bootstrap stack, then
recompile with **loaded `.kio`** (stage0 → final) and verify fixpoint. Native
**C** VM and a **thin Python** host layer remain; see the doc for package
layout, two-pass build, and ordered steps.

Ref: [self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md);
[interpreter/kio-format.md](interpreter/kio-format.md) (`.kio` format);
`koskari.serialize` (incremental compile and `dump_kio`).

### Extend existing syntax (prepend rules)

Investigate and implement a new special form that **copies** the current syntax
definition for a name (its transformer / merged rule table), **prepends** a new
rule or match group (first match wins globally), and **stores** the augmented
definition back into the **same** name slot—so existing surface syntax is
extended in place without replacing the whole transformer by hand.

Exact surface syntax is open (design sketches mention `extend-syntax`,
`transformer` + `prepend-rules`, and related merge constructors).

Ref: [language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md)
(Combining sections: merge, extend, copy; composable operators);
[MACROS.md](MACROS.md).

---

## Matcher

### Unified matcher rollup (`matcher_ng`)

**Status:** Design proposal. Stand-alone replacement matcher; lands as
`koskari/lib/matcher.c`, `koskari/lib/matcher.h`, and
`koskari/matcher_ng.py`. Existing `koskari/matcher.py` remains untouched
during initial implementation. Only after `matcher_ng` is validated will
specials and runtime sites be migrated; removal of the original
`koskari/matcher.py` follows successful migration and is out of scope for
the initial work.

A single matcher model serving syntax matching (macros, syntax
transformers, special-form dispatch, source-form diagnostics), runtime
matching (`match/case`, structured unpacking), hygiene extraction, and
composite matcher authoring (`FORMALS`, `BINDING`, `BINDINGS`). The model
layers surface matcher syntax, a Python builder API, a canonical
tagged-tuple frozen representation embedded as code constants, a thawed
native `Matcher` instance over a compact tagged-node array, and
invocation with subject plus extern bindings — all expressing the same
underlying rule program. Subject adapters (runtime `PySequence_Fast` and
compile-time `KosPair`) keep execution independent of input shape.
Phased implementation: native core and validator, subject adapters,
frozen schema and thaw, Python builder API, bootstrap composites, source
matcher-syntax parser, special-form dispatch integration, runtime
`match/case`, then hygiene extraction migration.

Ref: [language/matcher-ng-rollup.md](language/matcher-ng-rollup.md);
[language/unified-matcher-transformer-model.md](language/unified-matcher-transformer-model.md)
(current matcher to be replaced).

---

## Import / loader

### Python AST loader (transformative parser)

Load Python modules as Koskari modules at import time by using a special reader
that transforms Python AST into equivalent Koskari expression trees; the
existing Compiler and specials consume those forms unchanged. The new piece is
the transformative parser (AST → form tree); no compiler or special-form
changes required.

Ref: [python-ast-loader.md](python-ast-loader.md); [api/import-system.md](api/import-system.md) (module lifecycle, load path).

### .kio compiled cache

Implementation exists under `koskari.serialize` (read/write sectioned `.kio`,
incremental compile from `.ki`). Loader integration and packaging follow the
self-hosting roadmap.

Ref: [interpreter/kio-format.md](interpreter/kio-format.md);
[self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md);
[api/import-system.md](api/import-system.md).

### Import path selection

Add a feature flag (e.g. environment variable or loader knob) to choose at
runtime between demand imports and run_sync imports, instead of editing
`exec_module` in code.

Ref: [ideas.md](ideas.md); [api/import-system.md](api/import-system.md) (async vs demand path).

---

## Builtins

### help builtin

A **help** builtin that extends the core Pythonic `help` function: special-form
and layout aware, and dispatching to object-specific help data via a `__help__`
method when present. Design and implement the builtin, layout integration, and
the `__help__` protocol.

Ref: [api/builtins.md](api/builtins.md) (builtin catalog);
[api/dunder-adapter.md](api/dunder-adapter.md) (dunder/slot pattern for `__help__`);
[LAYOUTS.md](LAYOUTS.md) (layout syntax for layout-aware help).

### CLI startup version banner and --version

Add Python version, platform, and Koskari version to the Koskari CLI startup
banner. Also add a `--version` parameter that prints the same version/platform
information and exits.

---

## User documentation

### Bootstrap human-facing docs from meta spec

Create a top-level `docs/` tree (or external site) using the information
architecture, audiences, and maintenance rules in the documentation goals
meta-document (two on-ramps, shared guide, reference, VM/opcode tier,
patterns, contributor pages).

Ref: [documentation-goals.md](documentation-goals.md).

---

## To design (broader)

From [FEATURES.md](FEATURES.md) and related docs:

- Performance optimizations (general)

---

## Reference

- **Implemented specials:** [SPECIALS.md](SPECIALS.md)
- **Feature catalog and status:** [FEATURES.md](FEATURES.md)
- **Self-hosted compiler roadmap:** [self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md)
- **Ideas (uncommitted):** [ideas.md](ideas.md)
- **Variable resolution model (proposed):** [language/RESOLUTION-MODEL.md](language/RESOLUTION-MODEL.md)
