# Koskari design TODO

Centralized list of open design and implementation work. See the referenced
design docs for detail.

---

## VM / opcodes

### Switch / equality dispatch (SWITCH_EQ family)

Proposal: opcodes **SWITCH_EQ** (stack key + discriminant) and compressed
variants (**SWITCH_EQ_CONST**, **SWITCH_EQ_VAR**, **SWITCH_EQ_FAST**,
**SWITCH_EQ_VAR_ATTR**, **SWITCH_EQ_FAST_ATTR**) for multi-way equality branching
without re-pushing the discriminant after each failed arm. Possible compiler
special **compile/switch** mirroring **compile/cond** syntax.

Ref: [switch-eq-opcodes.md](switch-eq-opcodes.md).

---

## Generator / yield

### Dynamic wind-unwind at yield

**Status:** Proposal Only. Under consideration. May be a bad idea or cause too
great a performance impact; no implementation planned until resolved.

Decide whether to run dynamic-wind (and param/dyn-with) enter/exit at
generator install and freeze (so `(dynamic-wind before body after)` that
spans a yield runs `after` on yield and `before` on resume).

Ref: [interpreter/extent-wind-unwind.md](interpreter/extent-wind-unwind.md) (wind/unwind semantics).

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

**Status:** Freelist tracing and **`KOS_TRACE_EVENTS`** (VM/dynchain counters via
`get_trace_report()["events"]`) are implemented. Remaining work: timing,
intern cache, and optional opcode-level histograms.

Ref: [api/tracing.md](api/tracing.md).

---

## Compiler

### Remaining specials

- **Variadic ordering checks** — variadic `>`, `>=`, `<`, `<=` that assert
  strict/non-strict chain (e.g. `(> 9 2 0)`, `(<= 0 0 3 4 5 5 8)`).

Ref: [FEATURES.md](FEATURES.md) § Further migration / backlog;
[language/async.md](language/async.md) (await syntax/semantics);
[language/special-forms.md](language/special-forms.md).

### Variadic compiled object ops (`*` keyword form)

Variadic compiled object ops need to support the `*` keyword form. In that case
they should lower left-most first, then expand to the run-time form of the
operation.

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

---

## To design (broader)

From [FEATURES.md](FEATURES.md) and related docs:

- JIT integration
- Additional language features
- Performance optimizations (general)

---

## Reference

- **Implemented specials:** [SPECIALS.md](SPECIALS.md)
- **Feature catalog and status:** [FEATURES.md](FEATURES.md)
- **Self-hosted compiler roadmap:** [self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md)
- **Ideas (uncommitted):** [ideas.md](ideas.md)
- **Variable resolution model (proposed):** [language/RESOLUTION-MODEL.md](language/RESOLUTION-MODEL.md)
