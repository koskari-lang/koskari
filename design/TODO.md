# Koskari design TODO

Centralized list of open design and implementation work. See the referenced
design docs for detail.

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

### Fused binary compare opcodes

Single-op variants of the binary comparison of a variable against another
variable or a constant: `FAST_COMPARE_CONST`, `FAST_COMPARE_FAST`, and the
VAR variations (`VAR_COMPARE_CONST`, `VAR_COMPARE_VAR`). Today these sites
emit loads plus stack-reducing `OBJECT_COMPARE`; the fused forms would sit
beside the ternary bounds opcodes (`OBJECT_COMPARE_VAR` /
`OBJECT_COMPARE_FAST`) and push the boolean result.

Ref: [archive/compare-unification.md](archive/compare-unification.md).

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

### Macro-expansion cycle checks (investigate)

A self-producing expansion spins the compiler: `full_compile`'s rewrite
loop has no cycle or depth guard, and operator-position lowering added a
second rewrite-until-settled loop (`Compiler.lower_head` restarts via the
same path). Investigate detection: a depth cap, a seen-form check, or a
per-turn expansion budget, and what the diagnostic should name (the
cycling syntax, the forms in the loop). Applies to both loops at once if
the guard lives in `full_compile`.

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

### Dict literal compression (review)

A theoretical `compile/#dict` form could compress `(#dict ...)` calls
-- template-copy for fully-constant literals, `BUILD_KWDS_NAMES` for
constant-key runs -- once the head-identification gate (resolution
proof vs pragma) is settled. Backed off from the removed special
(56d77346) in favor of the uniform call lowering; the methods, the
gate options, and the equivalence obligations are recorded for review.

Ref: [language/dict-literal-compression.md](language/dict-literal-compression.md).

### All-constant comparison chains fold to a literal bool

Comparison specials (`compile/<`, `compile/==`, etc.) never fold, unlike
the arithmetic/bitwise family's leading-constant fold. When every operand
in a comparison chain compiles to a single `CONST`, evaluate the chain at
compile time and emit `CONST True`/`CONST False` instead of `OBJECT_COMPARE`:
`(< 4 8)` should compile straight to `CONST True`.

Ref: [SPECIALS.md](SPECIALS.md) (§Comparison interface);
`koskari/compile/specials/object.py`.

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

### Fused item in-place opcodes (six-opcode review)

Replace the `DUP` choreography in `compile/inplace-item` with fused
read-modify-write item opcodes mirroring the attr in-place family:
stack-object `ITEM_INPLACE` / `CONST_ITEM_INPLACE`, variable-based
`VAR_ITEM_INPLACE` / `VAR_CONST_ITEM_INPLACE`, and their fast
derivations `FAST_ITEM_INPLACE` / `FAST_CONST_ITEM_INPLACE`. Review the
six proposed opcodes -- encodings, the read-at-op-time ordering shift,
naming vs the `*_INPLACE_CONST` suffix convention, and whether the
stack-object tier carries its dispatch-table weight -- before any
implementation.

Ref: [interpreter/item-inplace-opcodes.md](interpreter/item-inplace-opcodes.md).

### GEN yield-from passthru

A `yieldfrom` field on the GEN dynnode: re-entering a delegating generator
dispatches straight to the delegate without thawing the frame, which is
reconstructed only when the delegate raises or ends.  Staged foreign-first;
the flattened Koskari delegate carries the hard questions (chain topology,
lazy reconstruction cascade, extent-inert guard).  Undecided — the proposal
doc weighs pros/cons and per-delegate-kind complexity.

Ref: [interpreter/gen-yieldfrom-passthru.md](interpreter/gen-yieldfrom-passthru.md).

### Seq-values kwargs (in-scope formals apply)

Specify bytecode and runtime surfaces for kwargs-capable seq-values /
in-scope formals apply; implement and test standalone, then consume from
runtime match and from `define-values` / `setq-values` kwargs paths.

Ref: [language/seq-values-kwargs-formals-apply.md](language/seq-values-kwargs-formals-apply.md).

### `define-matcher` parameterized matcher templates

Named, parameterized matcher construction from the native units,
module-scoped via `matcher/NAME` namespace resolution. The type and value
association semantics are deliberately open for deliberation.

Ref: [language/define-matcher.md](language/define-matcher.md).

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

### Formal default modes (`value:` / `copy:` / `new:`)

Generalize a defaulted formal from *a value to share* into *an operand plus an
operation applied at each application that omits the argument*: `value:`
(share), `copy:` (duplicate), `new:` (call). By-ref formals default to `copy`.
**Review and implement for feel**, with the explicit intent of easing the
transposer use-case.

Ref: [language/formal-default-modes.md](language/formal-default-modes.md).

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
(`tools/corpus/quick.ki` — `verify` oracle, `insert` validate+append,
`coverage`) and the generation-task `docs/corpus/CLAUDE.md` are **implemented**;
`quick` is a thin wrapper over the existing `tests/corpus` runner and
`tools/corpus/load.ki` serializer, run via `uv run` (the one sanctioned non-tox
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

---

## Reference

- **Implemented specials:** [SPECIALS.md](SPECIALS.md)
- **Feature catalog and status:** [FEATURES.md](FEATURES.md)
- **Self-hosted compiler roadmap:** [self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md)
- **Ideas (uncommitted):** [ideas.md](ideas.md)
- **Variable resolution model (proposed):** [language/RESOLUTION-MODEL.md](language/RESOLUTION-MODEL.md)
