# Koskari design TODO

Centralized list of open design and implementation work. See the referenced
design docs for detail.

---

## Evaluator architecture

### Event-driven eval handlers (RAISE / RETURN migration)

**Status:** Partially realized. YIELD and RETURN boundary delivery to
**`KOS_DYN_NEXT`** / **`KOS_DYN_FOREACH`** share eval-owned helpers; most
**`RETURN`** unwind/parent delivery still lives in the opcode. **RAISE** is not
fully unified: **`process_raise`** is the central hub, but **`eval_phase_entry`**
(**`KOS_MODE_EXCEPTION`**), **`run_transfer_plan_to_completion`**, and
**`KOS_MODE_RAISE`** still embed parallel exception policy.

**Review:** migrate further toward a single “exception received” state in the
eval loop — all PyErr / **`KOS_EV_RAISE`** ingress (normal eval, call results,
transfer-plan step outcomes, revive throw) should converge on one handler that
consults sub-states (normal vs mid-transfer vs continuation signal) instead of
special cases inside phase-specific functions. See
[interpreter/event-driven-eval-event-handlers.md](interpreter/event-driven-eval-event-handlers.md)
§3.3, §6.

---

## Continuations / transfer plans

- **Delimited invoke two-path model** — resume-in-place when the prompt is in
  live ancestry; PUSH graft when not. Addresses in-shift double wind/unwind on
  remote vs in-tree invoke. Design:
  [interpreter/delimited-invoke-model.md](interpreter/delimited-invoke-model.md).
  Regression spec: `test_reset_shift.py` (capture + invoke cases).
- **Async nested await** during transfer with non-empty `plan_stack` (§6.7 in
  interrupted-transfer-plans.md).
- **Full delimited PUSH replan** — deferred; §6.6 forbid remains until then.

Ref: [interpreter/callcc.md](interpreter/callcc.md),
[interpreter/delimited-invoke-model.md](interpreter/delimited-invoke-model.md),
[interpreter/interrupted-transfer-plans.md](interpreter/interrupted-transfer-plans.md).

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

**Near-term packaging:** add `.kio` beside `.ki` during `bdist_wheel` (additive;
sdist `.ki`-only); loaders prefer `.kio` when both exist. See
[interpreter/kio-setuptools-build.md](interpreter/kio-setuptools-build.md).

Ref: [interpreter/kio-format.md](interpreter/kio-format.md);
[self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md);
[api/import-system.md](api/import-system.md).

### Import path selection

Add a feature flag (e.g. environment variable or loader knob) to choose at
runtime between demand imports and run_sync imports, instead of editing
`exec_module` in code.

Ref: [ideas.md](ideas.md); [api/import-system.md](api/import-system.md) (async vs demand path).

---

## User documentation

### User documentation — remaining gaps

**Status:** Bootstrapped. The Sphinx `docs/` tree is live: both `start/` on-ramps
(beginner and from-python, each through `where-next.md`), the shared `guide/`
spine (00–10 plus optional bytecode chapters), `reference/` (glossary,
grammar shell, reader-layout, cli-and-config, concepts, specials/layouts/macros,
thin `runtime/`, monolithic `vm/index.md` plus per-opcode pages), `patterns/`
(five recipes), `corpus/` (schema, README, records, datasets), `project/`
(build-test, architecture-map, release-and-versioning), `api/` autodoc, `faq.md`,
and `changelog.md`.

**Still incomplete** (per [documentation-goals.md](documentation-goals.md)):

- **`reference/grammar.md`** — layout and handler links exist; the base
  s-expression grammar section is still marked TBD.
- **`reference/stdlib/`** — no per-topic user stdlib reference; `reference/runtime/`
  covers only a handful of bootstrap entry points (builtins surface mainly in
  `api/koskari-builtins.md` autodoc).
- **`reference/error-codes.md`** — exception-class guide only; no stable
  identifier or message-pattern catalog for deep links from REPL output.
- **`reference/vm/`** — contract lives in one large `index.md`; ~155 of 218
  `opcodes/*.md` pages are thin stubs. Split maintainer-facing pages from the
  spec (standalone bytecode-format, disassembly-and-debugging,
  peephole-and-lowering) are not separate; per-opcode public entries need depth.
- **`project/vm-and-opcodes.md`** — contributor opcode checklist (add to
  `opcodes.incl`, regen handlers, update VM reference) not written.
- **`patterns/recipes/`** — expand beyond the initial five recipes as usage
  surfaces gaps.
- **`exercises/`** — optional exercise track from the meta spec not started.
- **Maintenance process** — per-page stability labels not applied outside corpus
  records; tutorial/guide examples are not corpus-linked or verified; no CI guard
  for opcode-doc drift vs `koskari/lib/opcodes.incl`.
- **`changelog.md`** — skeleton only (single 0.1.0 entry); needs release-driven
  updates. **`faq.md`** is thin in places (e.g. free-threading answer incomplete).

---

## To design (broader)

From [FEATURES.md](FEATURES.md) and related docs:

- Performance optimizations (general)

---

## Recorded proposals (evaluate / implement)

The following items are captured from design discussion. Each has a short
design note and is open for evaluation and possible implementation.

### Seq-values kwargs (in-scope formals apply)

**TODO:** Specify bytecode and runtime surfaces for kwargs-capable seq-values /
in-scope formals apply; implement and test standalone, then consume from
runtime match and from `define-values` / `setq-values` kwargs paths.

Ref: [language/seq-values-kwargs-formals-apply.md](language/seq-values-kwargs-formals-apply.md).

### Runtime `match` form, `match/case` layout, and `MATCH_CASE` lowering

**TODO:** Reader layout, compile lowering per arm, opcode applier (analogous to
`SWITCH_EQ`), constant pools with extern names from local env, and
syntax-rules-style namespace mapping for committed arms; depends on seq-values
kwargs foundation where arm binding requires it.

Ref: [language/match-case-form.md](language/match-case-form.md).

### `RETURN` frame-only; `return-shift` for delim continuations

**TODO:** Split ordinary return from delimited-continuation rebound: `RETURN`
handles frame boundary only; introduce explicit `return-shift` (name TBD) for
the delim case; migrate any bundled behavior off `RETURN`.

Ref: [interpreter/return-return-shift.md](interpreter/return-return-shift.md).

### Destructuring formals, referencing formals, `define-values` fork

**TODO:** Nested patterns in lambda formals (e.g. `(lambda [a (b c) *: d] …)`);
`(ref …)` formals for rebind-to-Ref and write-through `set!`; if destructuring
and refs cannot share one formals grammar, limit unpacking to `define-values` /
`setq-values` with kwargs only (no ref binding there — refs stay on formals).

Ref:
[language/destructuring-and-referencing-formals.md](language/destructuring-and-referencing-formals.md).

### Multi-dispatch methods (matchers on formals)

**TODO:** Method tables dispatched by type/value/shape matchers over formals,
structured like `define-special` but feeding **runtime sequence values** into
the matcher arms instead of source forms.

Ref: [language/multi-dispatch-methods.md](language/multi-dispatch-methods.md).

### Example dispatch call timing harness

**TODO:** Add timed examples (Python, async Python, Koskari, demand Koskari
callees; Python and Koskari callers); triple timestamps with baseline
subtraction; ~1000 iterations per cell; aggregate averages.

Ref: [examples/dispatch-call-timing.md](examples/dispatch-call-timing.md).

### Demand in-eval denial (phase 1) — **Complete**

`KOS_FRAME_DEMAND` flag on the root frame; in-loop deny guards at all P0/P1
suspend sites; inject loop in `demand_sync_ex` removed; `demand_step_python_result`
enforces demand semantics for plain-Python callables at the boundary (branch
`demand_in_eval`).

Ref: [interpreter/demand-evalex-phase1.md](interpreter/demand-evalex-phase1.md).

### Demand call flattening (phase 2) — **Complete**

`KosDemandType` converted to `KosCallableTypeObject` (`KosDemandCallableType`)
with `tp_kos_install = demand_kos_install` and `tp_kos_wants_frame_fn =
demand_wants_frame`. When koskari bytecode calls `Demand(non_yielding_fn)`, the
CALL opcode's KosCallable branch fires, installs the inner fn's frame directly,
and sets `KOS_FRAME_DEMAND`. Python callable, yielding fn, and nested `Demand`
fall through to `demand_sync_ex`. `BoundMethod(Demand(fn))` flattening deferred.

Ref: [interpreter/demand-evalex-phase2.md](interpreter/demand-evalex-phase2.md).

### DynamicRef accessors on demand path

**TODO:** Make **DynamicRef** accessor handlers (`getfn`, `setfn`, `delfn`,
`emptyfn`) run on the demand path when they are Koskari callables: use in-eval
`demand_sync_ex` / eval-state injection where the VM is already on demand, and
route ref opcodes (e.g. **`DEREF`**) through **`KosRef_Deref`** /
**`KosRef_Store`** / **`KosRef_Clear`** instead of **`PyObject_CallMethod`**.

Ref: `koskari/lib/ref.c` (DynamicRef handlers); `koskari/lib/opcode/DEREF.c`;
`koskari/lib/demand.c`; [api/NAMESPACE-AND-REFS.md](api/NAMESPACE-AND-REFS.md);
[archive/demand-sync-await-violation-at-site.md](archive/demand-sync-await-violation-at-site.md).

---

## Packaging / installed CLI

### `koskari_scripts` setuptools integration

**v1 done:** **`koskari.extras.setuptools`** (`metadata.py`, `plugin.py` configure
hook, `.ki` package discovery, validation), **`koskari.launcher`**, **`import_module_async`**,
unit tests under **`tests/extras/setuptools/`** (`test_metadata.py`, **`test_plugin.py`**,
**`test_launcher.py`**).

**Follow-up:** end-to-end pip-install test; native binary for installed commands.

Ref: [api/koskari-scripts-packaging.md](api/koskari-scripts-packaging.md);
[api/import-system.md](api/import-system.md); `koskari/cli/__init__.py`;
`examples/life/`.

---

## Concurrent async (`async` form)

**Status:** Proposed — evaluate and possibly implement.

Add bootstrap macro **`compile/async`** and small runtime helpers for
concurrent fan-out/join on the current asyncio loop. Fixed job lists expand to
**`gather`** of **`(create-task (defer …))`**; streaming **`(async (each …))`**
and **`limit:`** variants expand to runtime **`async-each`** (iterator-driven
spawn with optional in-flight cap). Cooperative concurrency only — not multicore
parallelism.

**TODO:** Evaluate design; if accepted, implement in order: **`gather`** builtin,
**`with-limit`** / **`async-each`**, **`_async.ki`** macro, tests, user docs.

Ref: [api/async-form.md](api/async-form.md); `koskari/asyncio.py`;
[defer-coroutine.md](api/defer-coroutine.md).

---

## Reference

- **Implemented specials:** [SPECIALS.md](SPECIALS.md)
- **Feature catalog and status:** [FEATURES.md](FEATURES.md)
- **Self-hosted compiler roadmap:** [self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md)
- **Ideas (uncommitted):** [ideas.md](ideas.md)
- **Variable resolution model (proposed):** [language/RESOLUTION-MODEL.md](language/RESOLUTION-MODEL.md)
