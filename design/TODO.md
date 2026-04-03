# Koskari design TODO

Centralized list of open design and implementation work. See the referenced
design docs for detail.

---

## VM / opcodes

### Unimplemented opcodes (stubs)

See [OPCODES.md](OPCODES.md) §1.6 for the authoritative list of stub opcodes.
That document is the single reference for implementation status.

### Switch / equality dispatch (SWITCH_EQ family)

Proposal: opcodes **SWITCH_EQ** (stack key + discriminant) and compressed
variants (**SWITCH_EQ_CONST**, **SWITCH_EQ_VAR**, **SWITCH_EQ_FAST**,
**SWITCH_EQ_VAR_ATTR**, **SWITCH_EQ_FAST_ATTR**) for multi-way equality branching
without re-pushing the discriminant after each failed arm. Possible compiler
special **compile/switch** mirroring **compile/cond** syntax.

Ref: [switch-eq-opcodes.md](switch-eq-opcodes.md).

---

## Generator / yield

### Dynamic wind-unwind at yield (Phase 9)

**Status:** Under consideration. May be a bad idea or cause too great a
performance impact; no implementation planned until resolved.

Decide whether to run dynamic-wind (and param/dyn-with) enter/exit at
generator install and freeze (so `(dynamic-wind before body after)` that
spans a yield runs `after` on yield and `before` on resume).

Ref: [plan/09-phase-gen-wind-unwind.md](plan/09-phase-gen-wind-unwind.md);
[interpreter/extent-wind-unwind.md](interpreter/extent-wind-unwind.md) (wind/unwind semantics).

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

Explore a fast local stack (or stack layout) to speed up the evaluator. Ideas
include: VAR→FAST conversion in the compiler (indexed locals instead of
name lookup), and/or a dedicated fast operand stack (e.g. linear array for
current frame). Compiler optimization passes (constant folding, dead code
elimination, VAR→FAST) are still to migrate.

Ref: [interpreter/freeze-thaw-context-segments.md](interpreter/freeze-thaw-context-segments.md).

---

## Profiling / instrumentation

### Profiling compile-time options and runtime hooks

Add profiling support via compile-time options and runtime hooks. Goals:
timing information for opcodes, the eval loop, and usage of freelists (and
related allocator caches). Design and implement the knobs and hook points so
we can measure where time is spent and how freelists behave.

Ref: [api/tracing.md](api/tracing.md).

---

## Runtime / execution

### Run and iter use demand

Replace the `run` and `iter` methods of **KosFunction** and **KosPyAsyncGenerator**
with calls to the demand execution path rather than the sync path.

Ref: [api/dunder-adapter.md](api/dunder-adapter.md) (demand_sync, demand path);
[api/import-system.md](api/import-system.md) (demand path for module loading).
---

## Compiler

### Remaining specials

- **Variadic ordering checks** — variadic `>`, `>=`, `<`, `<=` that assert
  strict/non-strict chain (e.g. `(> 9 2 0)`, `(<= 0 0 3 4 5 5 8)`).

Ref: [FEATURES.md](FEATURES.md) § Ready to Migrate;
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

### Macro expansion and optimization

- Macro expansion: reader macros; defmacro, defalias (compile/define-syntax
  is implemented).
- Optimization passes: constant folding, dead code elimination, VAR→FAST
  conversion.

Ref: [interpreter/compiler.md](interpreter/compiler.md) (current compiler);
[language/special-forms.md](language/special-forms.md) (special forms catalog);
[archive/compiler-python-sibilant-historical.md](archive/compiler-python-sibilant-historical.md) § To Migrate.

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

Suffix and loader for `.kio` compiled cache are not yet integrated. Design in
[interpreter/kio-format.md](interpreter/kio-format.md).

Ref: [api/import-system.md](api/import-system.md).

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
- Additional language features (e.g. comprehensions, pattern matching, let*)
- Performance optimizations (general)
- Pattern matching: [api/pattern-matching.md](api/pattern-matching.md)
- Control flow (comprehensions, etc.): [language/control-flow.md](language/control-flow.md)

---

## Reference

- **Implemented specials:** [SPECIALS.md](SPECIALS.md)
- **Feature catalog and status:** [FEATURES.md](FEATURES.md)
- **Generator plan (phases 1–8 done, 9 under consideration):** [plan/README.md](plan/README.md)
- **Ideas (uncommitted):** [ideas.md](ideas.md)
- **Variable resolution model (proposed):** [language/RESOLUTION-MODEL.md](language/RESOLUTION-MODEL.md)
