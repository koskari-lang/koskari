# Koskari design TODO

Centralized list of open design and implementation work. See the referenced
design docs for detail.

---

## Evaluator architecture

- **Generic boundary dynnode dispatch** — evolve `kos_dynnode_on_control_returned`
  or add `kos_dynnode_on_boundary_event` so NEXT / FOREACH / YIELD_FROM
  consumers route through one capacity-driven dispatcher instead of scattered
  calls in `dyn_next.c`, `dyn_yield_from.c`, and `eval_parent_return_deliver`.
  Ref: [interpreter/boundary-control-reemit.md](interpreter/boundary-control-reemit.md) §5.2.

---

## Continuations / transfer plans

- **Async nested await during transfer** — when a wind/unwind thunk under an
  active or suspended transfer returns `KOS_XFER_SUSPENDED_AWAIT`, `plan_stack`
  must remain coherent across `KOS_RES_AWAIT` freeze/thaw, including when
  `plan_stack` depth > 0. Verify prologue ordering, abandon paths after resume,
  and `dyn_wind` on_chain / EXTENT_ACTION guards. Tests: async before/after thunk
  during undelimited nested interrupt (`let/cc` + `dyn-wind` + async Koskari
  await). Archived spec: [archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md) §6.7.
- **Full delimited PUSH replan** — deferred; §6.6 forbid remains until then.

Ref: [interpreter/callcc.md](interpreter/callcc.md),
[archive/delimited-invoke-model.md](archive/delimited-invoke-model.md),
[archive/exception-patchup.md](archive/exception-patchup.md),
[archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md).

---

## Evaluator performance

### Opcode caching: further families (investigative)

The per-site cache is **built and active**: the framework (tagged-union slots,
widened `KosOp`, trailing slot array, the two pragmas) plus two families --
the method cache (`KOS_CACHE_METHOD`, CALL_ATTR*) and the attribute-storage
field cache (`KOS_CACHE_FIELD`, FAST/VAR_ATTR(_STORE); SLOT on all versions,
INSTANCE_VALUE on 3.11-3.14). What remains is not building caching but adding
new **kinds** on the same framework. Each is its own investigation, gated by a
real workload that shows the win:

- **`DICT_HINT` field mode** — the reserved field mode: a keys-index hint for
  attributes on a *materialized* instance `__dict__` (today they resolve to
  GENERIC).
- **Jump / exception-type caches** — speculative; evaluate only if a workload
  demands them.

Ref: [interpreter/attr-lookup-cache.md](interpreter/attr-lookup-cache.md)
(§Phase 5).

### Dispatch hot-path microopt (investigated 2026-07-12 -- both blocked)

The computed-goto dispatch loop (`eval.c`, `koskari_eval_dispatch_computed_goto`)
already does per-handler tail-dispatch, and the coroutine / continuation /
async checks are already off the NORMAL path (they ride non-NORMAL
`KosOpResult` kinds through `cg_prologue` / `cg_poll`). Three per-opcode
round-trips were examined; **all three are load-bearing, not redundant** -- the
stackless call-flattening design makes the "obvious" caches unsound. Recorded
so this is not re-attempted blind:

- **Hoisting the code array base / length across NORMAL steps -- UNSOUND.**
  The premise "`es->code` is invariant across NORMAL steps" is false *by
  design*: a plain Koskari-to-Koskari call is flattened -- `callable_promote_normal`
  (`callable.c:56`) returns `EVAL_NORMAL()` after entering the callee's frame
  (new `es->code`, new `es->pc`), so dispatch continues into the callee without
  re-entering `cg_prologue`. The per-opcode `es->code` reload in
  `KOS_EVAL_PREPARE_NEXT_INSN` exists precisely to pick up that new frame.
  Caching the base across NORMAL steps SIGSEGVs on the first flattened call.
  A correct version needs a distinct frame-entry result kind threaded through
  the `KosEvalState_Call*` ABI (many consumers: `demand.c`, `object.c`,
  `values.c`, `dynchain.c`) -- a call-ABI change, not a microopt.
- **Dropping the per-opcode PC bounds check -- UNSAFE.** Non-terminal code
  objects genuinely exist and run: the opcode-unit-test harness
  (`tests/lib/opcode` `build_code`) constructs single-op snippets with no
  terminal `RETURN` to exercise one opcode in isolation. The check is what
  stops dispatch from running off the end there, so it cannot be removed (and
  enforcing terminal-RETURN at `KosCode_New` rejects those tests at
  construction).
- **Register-caching `es->pc` (the `next_instr` win) -- blocked** by the
  stackless design: suspension (AWAIT / YIELD / CAPTURE_CC / shift) freezes
  `es->pc` / `es->acting_pc` to the heap frame, and JUMP-family handlers rewrite
  `es->pc` while returning NORMAL, so pc must stay memory-authoritative.

Net: the dispatch loop's per-opcode state reloads are all required by the
flattening / suspension model. A real win here means reworking how frame-entry
is signaled (a typed `ENTER_FRAME` result that refreshes cached loop state
without the full `cg_prologue` drain), which is a design project. PGO (below)
is the lower-risk lever on dispatch-layout cost.

---

## Profiling / instrumentation

- Optional opcode-level histograms (freelist tracing, `KOS_TRACE_EVENTS`, and
  `KOS_TRACE_TIMING` already exist).

Ref: [api/tracing.md](api/tracing.md).

---

## Native build optimization

### Profile-guided optimization (PGO) build

Evaluate the PGO procedure and stand it up for release wheels. The computed-goto
interpreter is a near-ideal PGO target, and a profile should dissolve the
CALL_*_ATTR dispatch-layout tax the per-site cache currently pays (handlers
grow, the shared dispatch function bloats, `-O2` mis-lays-out the hot blocks).
Design: [interpreter/pgo-build.md](interpreter/pgo-build.md).

Two pieces of work:

- **Evaluate the procedure.** Prototype `make build-pgo` (three-pass:
  instrument → run harness → optimize with `-DKOS_PGO` un-forcing
  `always_inline`) and measure the delta vs `-O2 + always_inline` on
  `churn --all`, machine-normalized. Confirm PGO recovers (ideally erases) the
  all-sites cache tax and that un-forcing inline under a profile beats forcing
  it. Then wire the per-cell three-pass build into cibuildwheel (GCC/gcov on
  Linux, Clang/llvm-profdata on macOS), profile regenerated per
  `os × Python-version` cell.

- **Synthesize the profiling run set.** Author the harness as a representative
  mix, weighted toward the universal hot machinery (dispatch, `CALL_*`, loop,
  branch, namespace access): a subset of the phased test suite for coverage,
  `churn --all` repeated for hot-path weight, and a set of example Koskari
  programs for realistic distributions. The harness is the durable,
  version-controlled asset; the profile data is a per-target build intermediate.

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

Promotion is only valid for non-yielding functions, and yield-ness is fully
known before the pass runs: scan for `YIELD` / `YIELD_FROM` first and skip
promotion entirely for yielding streams — never convert then un-convert.
`demote_tail_calls_if_yields` stays in the pipeline as a safety net only,
for raw `TAIL_CALL*` embedded via bytecode specialty syntax in a generator.

Ref: [language/tail-call-bytecode-pass.md](language/tail-call-bytecode-pass.md).

### Const-run squashing for `#list` and `#tuple`

Partition collection literal elements into compile-time constant runs vs dynamic
expressions; emit tuple constants from the pool plus `BUILD_LIST_EXTEND` (new
opcode) for lists, and `CONST` / `COMBINE_ARGS` for tuples. `compile/#tuple`
and `compile/#list` today use naive per-element compile + `BUILD_ARGS` /
`BUILD_LIST`.

Ref: [language/const-collection-squashing.md](language/const-collection-squashing.md).

### `assert` syntax + runtime function

Add `assert` in two forms sharing one gate, `assert/enabled` (a new `assert/*`
pragma feature, baseline `True`, seedable at launch via the CLI pragma). The
**syntax** `(assert EXPR MSG?)` is a `define-special` that reads compile-time
`assert/enabled`: enabled → emit a falsey-check that raises `AssertionError`;
disabled → expand to `(begin)`, so `EXPR` is neither compiled nor evaluated
(Python `-O` semantics). The **runtime function** `assert` (bootstrap callable)
consults the session-level launch value at call time: raises on a falsey
condition when enabled, returns without raising when disabled. Pragma changes
the level forward-only and module-scoped. Open: graded level vs. bare bool,
message form.

Ref: [language/assert.md](language/assert.md); [PRAGMA.md](PRAGMA.md);
[language/pragma-cli.md](language/pragma-cli.md).

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

### CLI pragma

Let the CLI seed a module's pragma from outside the source, for the module the
entry point loads (a script's main module, or the REPL module). Resolve a
`ModulePragma` from command-line flags (`--pragma KEY=VALUE`, repeatable)
and/or a settings file, and pass it as the **active source** to
`build_module_load_facets` (which grows an optional `source_pragma`). The seed
is a session default in the precedence chain (below the module's own
`(pragma …)`); it applies only to the entry and REPL modules, never to imported
modules, so nothing leaks into distributed libraries. Scope out flag spelling,
`.ki` vs. structured settings-file format, and file discovery/precedence.

Ref: [language/pragma-cli.md](language/pragma-cli.md); [PRAGMA.md](PRAGMA.md).

### Package pragma

Let a package declare a default pragma its sub-packages and sub-modules inherit
(formerly "meta-pragma"). Add a `__package_pragma__` slot (a `ModulePragma`
template, no watchers) and a `(package-pragma [ns/key value …] …)` surface macro
that writes both `__package_pragma__` and the local `__pragma__`. Augment the
loader so `build_module_load_facets` resolves the parent package's
`__package_pragma__` as the module's default `__pragma__` (fresh watcher set —
watchers never cross modules) and stores both, so sub-sub-packages keep pulling
the value down; a sub-package that declares its own refines the default for its
subtree. Rides Python's parent-before-child import order — no header scan.
Reuses `ModulePragma.rebuild` wholesale.

Ref: [language/pragma-package.md](language/pragma-package.md); [PRAGMA.md](PRAGMA.md).

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

## Tooling

### `koskari.lint` advisory validator ("flake8 for Koskari")

New `koskari-extras` `koskari.lint` module plus a `koskari-lint` CLI: a
parallel-to-flake8 tool (not reusing flake8) that never runs user code. A reader
+ validator pair mirrors the reader + compile pair, replacing compile-and-eval
with a non-evaluating validate pass (`source -> reader -> validate -> structured
findings`). **Mandate:** the analysis is a pure data-driven reporter -- engines
emit flat structured findings (code, span, message), and the CLI alone reads
config to select/suppress and format. Two engines share the frontend: a form /
semantic engine (form tree + real resolution + compile-time matcher builders,
reused not reimplemented; validates call sites against the exact matcher for
known transformer heads) and a lexical / layout engine (lexer/layout stream for
indentation, spacing, docstrings). Undeclared/higher-order heads report as
unchecked (no arity guess); reader-macro or computed-macro regions mark opaque.
Config is pragma-independent but pragma-aware (observe, never share; strength
lives in config defaults, not in the analysis). Example checks: recommend/warn
on bracketing, keyword-in-call from layout misuse, `(when (not …))` -> `unless`,
docstrings, shadowing, unused bindings. Open: whether `@special` registration
should require a `validate` sibling of `compile` to prevent silent coverage
drift.

Ref: [koskari-lint.md](koskari-lint.md).

### Baked-in corpus MCP on AWS Lambda

Migrate the corpus MCP server off live Hugging Face Dataset Viewer queries onto
a dataset **baked into the image**, run as a public service on **AWS Lambda** at
`mcp.koskari.org`: replace the `viewer-*` fetch layer with in-memory filtering
over the exported parquet tables, and package via the Lambda Web Adapter
behind an API Gateway custom domain with a GitHub OIDC deploy workflow.

Ref: [mcp-baked-in-lambda.md](mcp-baked-in-lambda.md);
[archive/corpus-mcp-server.md](archive/corpus-mcp-server.md).

---

## Reference

- **Implemented specials:** [SPECIALS.md](SPECIALS.md)
- **Feature catalog and status:** [FEATURES.md](FEATURES.md)
- **Self-hosted compiler roadmap:** [self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md)
- **Ideas (uncommitted):** [ideas.md](ideas.md)
- **Variable resolution model (proposed):** [language/RESOLUTION-MODEL.md](language/RESOLUTION-MODEL.md)
