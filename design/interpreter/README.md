# Interpreter Architecture

This directory documents the interpreter implementation, including bytecode format, compiler design, and evaluation model.

## Compiler

The Koskari compiler lives in `koskari/compile/`. Implemented special forms, the macro
pipeline (reader macros, `define-syntax`, **`compile/syntax-*`** /
**`compile/macro-*`**), and compiler optimizations are documented in
[SPECIALS.md](../SPECIALS.md). For the historical python-sibilant (CPython bytecode)
pipeline and sibilant-ceval migration notes, see
[archive/compiler-python-sibilant-historical.md](../archive/compiler-python-sibilant-historical.md).

The former one-page stub [archive/compiler.md](../archive/compiler.md) is archived
(superseded by this section and SPECIALS.md).

## Contents

- **bytecode.md**: Bytecode format and opcodes (implemented in `koskari/lib/bytecode.c`): the 32-byte instruction record and the resolved extras row -- design record [archive/resolved-operands.md](../archive/resolved-operands.md); extent/location tables — archived spec [archive/bytecode-metadata-tables.md](../archive/bytecode-metadata-tables.md)
- **lib-lifecycle.md**: The `koskari.lib` module lifecycle: process statics, a re-runnable init, no teardown from the module slots, an explicit clear for a host of its own
- **electors.md**: Electors — handlers elected once for a site's operands (cache slot) or an extent's fixed fact (dyn node) and tail-called thereafter; built on YIELD_FROM (node-style) and on OBJECT_OPERAND and the in-place families (cache-style, int bodies); float and string bodies, the compare family, FOREACH, and the return arm remain
- **[archive/bytecode-finalization.md](../archive/bytecode-finalization.md)** (archived): Final form phase design — collect/size/allocate/emit pipeline, identity-based cpool dedup, unified block with extra table; implemented in `bytecode.c`
- **evaluator.md**: Evaluation model and drive loop (eval.c); KosCoroutine and run-code-as-coroutine (func.c)
- **[archive/shortcut-jump-threading.md](../archive/shortcut-jump-threading.md)** (archived): a processor pass rewriting a `JUMP_AND`/`JUMP_OR` that lands on a conditional jump into that jump, so each `and`/`or` clause in test position fuses to its `CHECK_*` form; implemented in `processor.py`, with the negate flag on the compare checks and the feeder-fused branch opcodes
- **[EVALUATOR.md](../EVALUATOR.md)**: Evaluator operation diagrams, threaded dispatch and the trampoline, opcode signaling, exceptions, transfer goals, tail calls, call installs, CPS resumes, and play-by-play scenarios
- **[archive/threaded-dispatch.md](../archive/threaded-dispatch.md)** (archived): the musttail-threaded dispatch design, implemented; **[archive/cps-consumer-api.md](../archive/cps-consumer-api.md)** (archived): the continuation-passing Call/Next entries, implemented; **stencil-compilation.md** builds on both
- **[archive/iteration-yield-boundaries.md](../archive/iteration-yield-boundaries.md)** (archived): YIELD vs YIELD_FROM vs PUSH_FOREACH/ITER_NEXT — implementation map, underlying calls, consolidation proposals
- **[archive/opcode-eval-helpers-inventory.md](../archive/opcode-eval-helpers-inventory.md)** (archived): Static helpers in `eval.c` and opcode `.c` files — opcode cross-reference, redundancies, suggested public names for native/runtime API
- **execution-invariants.md**: Koskari execution model invariants and design commitments
- **generator-extent-freeze.md**: extents across a generator yield -- PARAM restores its cells at freeze and re-enters them at install; whether WIND and WITH need a leave and re-enter walk without unlinking is open
- **yield-from-close.md**: closing a delegate through yield-from -- GeneratorExit takes the raise walk, which cannot await an async or Koskari delegate's close; whether it should route through the resume like every other throw is open
- **pair-const-opcodes.md**: PROPER_CONST, BRACKET_PROPER_CONST, CONS_CONST and BRACKET_CONS_CONST, pair builders fed from a pool tuple so quoted and quasiquoted lists render down over their constant elements in one row (idea)
- **module-dict.md**: a Koskari module's `__dict__` must be a dict for exec, eval, dataclasses and typing; the in-place snapshot was tried and set aside; a read-through dict subclass with purged writes, or dict-backed ItemRef bindings accelerated by dict versioning, are the directions to investigate (open)
- **opcode-sizing.md**: per-handler code size joined with per-opcode timing (`make opsizing`, `design/perf/opsizing.tsv`); how to read the table and how to measure a change
- **demand-evalex-phase1.md** / **demand-evalex-phase2.md** (archived under [archive/](../archive/)): demand in-eval denial and call flattening — see [EVALUATOR.md](../EVALUATOR.md) §7 and the archived [archive/demand-wrapper.md](../archive/demand-wrapper.md)
- **python-boundary.md**: the Python-to-Koskari call boundary -- per-call ledger of the sync and async entries, the callee as the root frame, the root-frame cache for the general shape (implemented), the minted domain replaced by a per-thread default domain (implemented), `KosBoundMethod` vectorcall (implemented), removing `KosEvalState` in favour of `KosContext` alone (implemented), one thread-record resolution per boundary call (implemented), the exception-state swap only when something is held (implemented), the async entry building its frame and context at first send and releasing both at completion, with a context freelist (implemented), and the frame cache serving every call shape (implemented)
- **[archive/introspection-trace.md](../archive/introspection-trace.md)** (archived): the TraceView snapshot proposal, never built; the live views are `Context.snapshot()` and `__koskari_dynchain__`
- **callcc.md**: Call/cc implementation design (current PUSH delimited invoke)
- **[archive/delimited-invoke-model.md](../archive/delimited-invoke-model.md)**: two-path delimited invoke (in-place Path A / bounded PUSH Path B) — implemented and archived; see also [archive/exception-patchup.md](../archive/exception-patchup.md) for the raise-path model
- **[archive/kospartial.md](../archive/kospartial.md)**: partial application — implemented as the standalone native `KosPartial` (duck-typed, not an `isinstance` match); archived
- **[archive/kosproperty.md](../archive/kosproperty.md)**: KosCallable-aware **`property`** — auto **`Demand`** wrap on accessors; implemented and archived
- **[archive/evaluator-token-continuation.md](../archive/evaluator-token-continuation.md)** (archived): Evaluator token + continuation application model (KosEvaluatorToken, KoskariContinuationApply)
- **[archive/gil-cooperation.md](../archive/gil-cooperation.md)** (archived): GIL cooperation design — domain-TLS counter + timer, maybe_cooperate/ensure_gil, candidate call sites, and cost–benefit of options
- **kio-format.md**: `.kio` compiled cache format — binary file layout, softened/hardened serialization levels, constant pool policy, `TaggedTuple`, cache invalidation, and loader algorithm
- **kio-setuptools-build.md**: `.kio` precompilation during `bdist_wheel` (sdist `.ki` only; wheel adds `.kio` beside `.ki`), loader preference, and why not `__pycache__`
- **scratch-slots.md**: a pool of scratch tokens a fragment hands out for a special's extent and reuses across non-overlapping uses (tier 1), their own tuple on the spec so they leave the locals set (tier 2), and a scratch node with its own instructions as an optimization to measure (tier 3); the match temporaries as the first user (design)
- **[archive/matcher-bytecode.md](../archive/matcher-bytecode.md)** (archived): the matcher lowered to evaluator instructions -- one lowering from the frozen tuple to a standalone `Function`, a module's lambda or an inline fragment, pull and peek instructions over fast-slot cursors, failure edges in place of the change log, the fused instruction shapes with their VAR and FAST twins, the matcher compiler's three lifetimes and the `Syntax` dispatcher, and the stages that replaced the engine (implemented)
- **[archive/matcher-callable.md](../archive/matcher-callable.md)** (archived): the matcher as the `Function` its lowering builds -- no Matcher type, a gensym formal, compile at build, each `Syntax` arm tried through its own function by a shared first-match function, the frozen tuple an internal handoff (implemented)
- **[archive/matcher-core.md](../archive/matcher-core.md)** (archived): the matcher core redesign -- every node against a cursor, no peek mode or width shortcuts, reservation by a bounded cursor, captures held in variably sized marks on the operand stack, the instruction set and per-node lowering, a differential harness against master, and a phased implementation plan (implemented)

Completed design drafts (historical) live under **[archive/](../archive/)** — e.g.
`dynamic-node-unified-api.md` (partial dynnode migration spec), `compiler.md` (stub),
`dynnode-event-model.md`, `yield-from-execution-model.md`,
`yield-from-revamped-operational-model.md`, `eval-loop-mode-refactor.md`, `eval-result-unification.md`,
`demand-evalex-phase1.md`, `demand-evalex-phase2.md`,
`interrupted-transfer-plans.md`, `bytecode-metadata-tables.md`,
`domain.md` and `domain-task-isolation.md` (Domain / per-context parameter refs),
`node-owned-operand-stack.md`, `kos_eval_state_api_migration.md`,
`kosevent-elimination.md`.

## Implementation Status

| Component | Status | Files |
|-----------|--------|-------|
| Bytecode storage | Implemented | `bytecode.c`, `bytecode.h`, `opcodes.incl` |
| Opcode definitions | Implemented | `opcodes.incl` |
| Evaluator core | Implemented | `eval.c`, `eval.h` |
| KosCoroutine / run-code-as-coroutine | Implemented | `func.c`, `func.h` |
| Opcode dispatch | Implemented | `eval.c` (per [OPCODES.md](../OPCODES.md) §1.6) |
| Continuation support | Implemented | `eval.c`, `eval/eval_chain.c`, `dynchain.c`, `dynchain/xfer_plan.c` |
| Compiler | Substantially implemented — see [SPECIALS.md](../SPECIALS.md) | `koskari/compile/compiler.py`, `koskari/compile/specials/` |
| `.kio` cache format | Design only | `design/interpreter/kio-format.md` |
