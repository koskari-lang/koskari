# Koskari design TODO

Centralized list of open design and implementation work. See the referenced
design docs for detail.

---

## Evaluator architecture

- **RAISE ingress unification** — `process_raise` is the central hub, but
  `eval_phase_entry` (`KOS_MODE_EXCEPTION`), `run_transfer_plan_to_completion`,
  and `KOS_MODE_RAISE` still embed parallel exception policy. Migrate toward a
  single “exception received” path: all PyErr / `KOS_RES_RAISE` ingress (normal
  eval, call results, transfer-plan step outcomes, revive throw) should
  converge on one handler that consults sub-states (normal vs mid-transfer vs
  continuation signal).
- **Generic boundary dynnode dispatch** — evolve `kos_dynnode_on_control_returned`
  or add `kos_dynnode_on_boundary_event` so NEXT / FOREACH / YIELD_FROM
  consumers route through one capacity-driven dispatcher instead of scattered
  calls in `dyn_next.c`, `dyn_yield_from.c`, and `eval_parent_return_deliver`.
  Ref: [interpreter/boundary-control-reemit.md](interpreter/boundary-control-reemit.md) §5.2.

---

## Continuations / transfer plans

- **Delimited invoke two-path model** — resume-in-place when the prompt is in
  live ancestry; PUSH graft when not. Addresses in-shift double wind/unwind on
  remote vs in-tree invoke. Design:
  [interpreter/delimited-invoke-model.md](interpreter/delimited-invoke-model.md).
  Regression spec: `test_reset_shift.py` (capture + invoke cases).
- **Async nested await during transfer** — when a wind/unwind thunk under an
  active or suspended transfer returns `KOS_XFER_SUSPENDED_AWAIT`, `plan_stack`
  must remain coherent across `KOS_RES_AWAIT` freeze/thaw, including when
  `plan_stack` depth > 0. Verify prologue ordering, abandon paths after resume,
  and `dyn_wind` on_chain / EXTENT_ACTION guards. Tests: async before/after thunk
  during undelimited nested interrupt (`let/cc` + `dyn-wind` + async Koskari
  await). Archived spec: [archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md) §6.7.
- **Full delimited PUSH replan** — deferred; §6.6 forbid remains until then.

Ref: [interpreter/callcc.md](interpreter/callcc.md),
[interpreter/delimited-invoke-model.md](interpreter/delimited-invoke-model.md),
[archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md).

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

- Optional opcode-level histograms (freelist tracing, `KOS_TRACE_EVENTS`, and
  `KOS_TRACE_TIMING` already exist).

Ref: [api/tracing.md](api/tracing.md).

---

## Compiler

### Tail-call promotion bytecode pass

Move tail-call optimization out of special forms into a `complete()` bytecode
pass: specials always emit `CALL*`; `promote_tail_calls` rewrites eligible sites
to `TAIL_CALL*` using extent-aware heuristics. Removes the `tc` parameter from
all special signatures and simplifies `compile/call` and `compile/call-attr`.
Gate with `Compiler.tco_enabled` for now; pragma gating deferred. v1 promotes
only opcodes that already have `TAIL_CALL` analogues (`CALL`, `CALL_NAMES`,
`CALL_STAR`).

Ref: [language/tail-call-bytecode-pass.md](language/tail-call-bytecode-pass.md).

### Const-run squashing for `#list` and `#tuple`

Partition collection literal elements into compile-time constant runs vs dynamic
expressions; emit tuple constants from the pool plus `BUILD_LIST_EXTEND` (new
opcode) for lists, and `CONST` / `COMBINE_ARGS` for tuples. `compile/#tuple`
and `compile/#list` today use naive per-element compile + `BUILD_ARGS` /
`BUILD_LIST`.

Ref: [language/const-collection-squashing.md](language/const-collection-squashing.md).

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

### Wildcard import (`import-all-from` / `IMPORT_STAR`)

Add `IMPORT_STAR` opcode and `import-all-from` special for Python
`from MODULE import *` semantics. Ref:
[language/import-all-from.md](language/import-all-from.md).

### Python AST loader (transformative parser)

Load Python modules as Koskari modules at import time by using a special reader
that transforms Python AST into equivalent Koskari expression trees; the
existing Compiler and specials consume those forms unchanged. The new piece is
the transformative parser (AST → form tree); no compiler or special-form
changes required.

Ref: [python-ast-loader.md](python-ast-loader.md); [api/import-system.md](api/import-system.md) (module lifecycle, load path).

### .kio compiled cache

Loader integration and packaging follow the self-hosting roadmap.

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

Gaps per [documentation-goals.md](documentation-goals.md):

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
- **Refs as storage-location indirection** — add a dedicated, Q&A-format user
  page developing refs as "pointers, but safer" (getting refs via `&name` /
  `&(item …)` / `(ref …)`, `deref` / `*name`, bind vs store, and by-ref
  formals). Move the by-ref example out of the functions tutorial
  (`guide/03-functions-deep.md`) into this page, add corpus records from the
  example (obtain/pass refs plus a value-instead-of-ref error variation), and
  add unit tests pinning the example cases. Design:
  [refs-storage-indirection.md](refs-storage-indirection.md).

---

## To design (broader)

From [FEATURES.md](FEATURES.md) and related docs:

- Performance optimizations (general)

---

## Recorded proposals (evaluate / implement)

The following items are captured from design discussion. Each has a short
design note and is open for evaluation and possible implementation.

### Seq-values kwargs (in-scope formals apply)

Specify bytecode and runtime surfaces for kwargs-capable seq-values /
in-scope formals apply; implement and test standalone, then consume from
runtime match and from `define-values` / `setq-values` kwargs paths.

Ref: [language/seq-values-kwargs-formals-apply.md](language/seq-values-kwargs-formals-apply.md).

### Runtime `match` form, `match/case` layout, and `MATCH_CASE` lowering

Reader layout, compile lowering per arm, opcode applier (analogous to
`SWITCH_EQ`), constant pools with extern names from local env, and
syntax-rules-style namespace mapping for committed arms; depends on seq-values
kwargs foundation where arm binding requires it.

Ref: [language/match-case-form.md](language/match-case-form.md).

### `RETURN` frame-only; `return-shift` for delim continuations

Split ordinary return from delimited-continuation rebound: `RETURN`
handles frame boundary only; introduce explicit `return-shift` (name TBD) for
the delim case; migrate any bundled behavior off `RETURN`.

Ref: [interpreter/return-return-shift.md](interpreter/return-return-shift.md).

### Multi-dispatch methods (matchers on formals)

Method tables dispatched by type/value/shape matchers over formals,
structured like `define-special` but feeding **runtime sequence values** into
the matcher arms instead of source forms.

Ref: [language/multi-dispatch-methods.md](language/multi-dispatch-methods.md).

### Example dispatch call timing harness

Add timed examples (Python, async Python, Koskari, demand Koskari
callees; Python and Koskari callers); triple timestamps with baseline
subtraction; ~1000 iterations per cell; aggregate averages.

Ref: [examples/dispatch-call-timing.md](examples/dispatch-call-timing.md).

---

## Concurrent async (`async` form)

Add bootstrap macro **`compile/async`** and small runtime helpers for
concurrent fan-out/join on the current asyncio loop. Fixed job lists expand to
**`gather`** of **`(create-task (defer …))`**; streaming **`(async (each …))`**
and **`limit:`** variants expand to runtime **`async-each`** (iterator-driven
spawn with optional in-flight cap). Cooperative concurrency only — not multicore
parallelism.

Implement in order: **`gather`** builtin, **`with-limit`** / **`async-each`**,
**`_async.ki`** macro, tests, user docs.

Ref: [api/async-form.md](api/async-form.md); `koskari/asyncio.py`;
[defer-coroutine.md](api/defer-coroutine.md).

---

## Bootstrap and reader surface

- **Reader macro dollars example** — bootstrap **`set-macro-character`** /
  **`read-atom`** helpers, port **`examples/reader-macro/dollars.ki`**, reader
  tests, user recipe. Ref:
  [examples/reader-macro-dollars.md](examples/reader-macro-dollars.md).
- **`labels` / `flet`** — bootstrap macros for local function binding (mutual
  recursion vs **`lambda`** shadowing). Ref:
  [language/labels-and-flet.md](language/labels-and-flet.md).

### Ref surface (notes from the refs guide)

- **`(set! (deref r) v)` — optimize value-level ref store** — now has a
  temporary working definition: `set!` on a deref dispatches to the ref's
  `store` method (validated with tests), restoring read/write symmetry with
  `compile/deref`. Open optimization: a dedicated opcode that validates `r` is
  a `Ref` type rather than duck-typing on a `.store` method, and avoids the
  method-call overhead.

(The proposed `*foo` deref reader sugar is a separate open idea — see
[ideas.md](ideas.md). It has been removed from user-facing docs pending a
decision.)

---

## Corpus

### Corpus generation agent

Grow `docs/corpus/records/` to hundreds–thousands of entries covering every
builtin special and macro, via an autonomous Claude Code agent rather than core
dev time. The agent runs in its own git worktree (own gitignored `.venv` from a
one-time `make venv` plus `uv pip install jsonschema PyYAML`; commits freely;
human opens a PR per batch), keeping the oracle drift-free and avoiding
contention with main-tree `tox`/`make test`. Tooling
(`tools/corpus/quick.py` — `verify` oracle, `insert` validate+append,
`coverage`) and the generation-task `docs/corpus/CLAUDE.md` are **implemented**;
`quick.py` is a thin wrapper over the existing `tests/corpus` runner and
`tools/corpus/load.py` serializer, run via `uv run` (the one sanctioned non-tox
path). Remaining work is the supervised worktree run itself. Mandate per
special/macro: basic, variant, block/layout, and invalid+corrective entries;
priority on patterns common in real code (`define-class`, `for-each`,
`define-syntax`, `try/except`, `let`, async). The PR's `make test`
(`phase_corpus`) is the authoritative backstop.

Ref: [corpus-generation-agent.md](corpus-generation-agent.md);
[corpus-schema-philosophy.md](corpus-schema-philosophy.md).

---

## Reference

- **Implemented specials:** [SPECIALS.md](SPECIALS.md)
- **Feature catalog and status:** [FEATURES.md](FEATURES.md)
- **Self-hosted compiler roadmap:** [self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md)
- **Ideas (uncommitted):** [ideas.md](ideas.md)
- **Variable resolution model (proposed):** [language/RESOLUTION-MODEL.md](language/RESOLUTION-MODEL.md)
