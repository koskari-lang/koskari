# Koskari design TODO

Centralized list of open design and implementation work. See the referenced
design docs for detail.

---

## Evaluator performance

### Opcode caching: further families (investigative)

New cache kinds on the per-site cache framework. Each is its own
investigation, gated by a real workload that shows the win:

- **`DICT_HINT` field mode** — the reserved field mode: a keys-index hint for
  attributes on a *materialized* instance `__dict__` (today they resolve to
  GENERIC).
- **Jump / exception-type caches** — speculative; evaluate only if a workload
  demands them.

Ref: [archive/attr-lookup-cache.md](archive/attr-lookup-cache.md)
(§Phase 5).

### Pair opcodes fed from the constant pool (investigate)

A quoted list compiles to one CONST per element and a CONS or PROPER;
the pool holds tuples but not pairs. PROPER_CONST and
BRACKET_PROPER_CONST would build the list from a pool tuple in one
row, and CONS_CONST and BRACKET_CONS_CONST would join stacked values
with a constant run and tail without pushing the constants, so a
template's quotes and quasiquotes render down over their constant
elements.

Ref: [interpreter/pair-const-opcodes.md](interpreter/pair-const-opcodes.md).

### Electors: remaining homes

A `KosThreadFn` chosen once for a site's operands (a cache slot) or an
extent's fixed fact (a dyn node field) and tail-called on every later
execution. Open: node-style on the FOREACH step and NEXT node, then the
frame's return arm.

Ref: [interpreter/electors.md](interpreter/electors.md).

### Reads that call and reads that wait

A ref deref that answers "call this" or an awaitable instead of
running a nested evaluator, so a read can raise, suspend and be
captured like a call. A schema-derived row resume finishes the
multi-slot and fused rows; RETURN and YIELD loads tail into the
getter. Raw and plain-ref reads keep their code. The same row resume
carries UNPACK and the star call over a Koskari generator, collecting
it element by element through the NEXT node instead of materialising
it on the demand path (section 7).

Ref: [interpreter/resumable-reads.md](interpreter/resumable-reads.md).

### Immortal, immutable code objects (proposal, 3.12 floor)

Durable function code becomes one sealed unit: a `KosCode` holding its
own block, reserved from its chunk, immortal (PEP 683), a non-GC leaf,
with the per-site cache slots moved to a writable home. Closes the
writable header through which the sealed block is reached. Blocked on
raising the Python floor to 3.12.

Ref: [interpreter/immortal-code.md](interpreter/immortal-code.md).

### Dispatch tweaks to investigate

Every record now carries its own pc, so the `acting_pc` store in
`KOS_THREAD_AT` may be redundant (readers could take `insn->pc`); a
call install could return the callee's first record instead of
re-fetching through `es->code`; and the fused POP peephole could read
`insn[1]` rather than walk the code. Each is under 1% on the churn
kernels; measure before taking any.

Ref: [archive/resolved-operands.md](archive/resolved-operands.md).

### Result destinations (design discussion)

An operation whose result goes straight to a store row allocates twice
while the destination's old value dies. The user wants an encoding for
"stored in this manner" (a destination operand or an elector body
variant), not one class of fused opcodes; 42 sites on the churn kernels.

Ref: [interpreter/result-destinations.md](interpreter/result-destinations.md).

### Trailing defaults fast path (design discussion)

A function with defaults only at the tail takes the general formals
machine on every call and never reuses its cached frame; 18 cycles per
call over the simple path, every call in the tree kernel. A min..max
arity classification with a default vector; default modes decide it.

Ref: [interpreter/trailing-defaults.md](interpreter/trailing-defaults.md).

### Call-path follow-ups (measured, not taken)

Tail calls to a different function, the installable arity field, frame
bookkeeping, hot/cold handler splits, dispatch and stack indirection,
node and function layouts, and the KosOp.fn elector quickening
decision. Each with its measurement and gate.

Ref: [interpreter/call-path-followups.md](interpreter/call-path-followups.md).

### Node push and pop by shape (investigate)

After the region pushes stopped walking the extent table, a loop entry
still costs 9.6 ns over a `when` body in push and pop machinery whose
branches are all decided by facts fixed at construction. Idea: two arms
(frame, region) inside the existing push and pop, no per-kind push. The
constructor and clone step that was withdrawn is spelled out there.

Ref: [interpreter/push-pop-shapes.md](interpreter/push-pop-shapes.md).

### Namespace size survey (investigate)

Size classes were declined. Survey function namespace slot counts over
the opgrams input set before considering a smaller KOS_NS_MIN_SLOTS.

Ref: [interpreter/call-path-followups.md](interpreter/call-path-followups.md)
(Namespace size survey).

---

## Native build optimization

### Profile-guided optimization (PGO) build

Evaluate the PGO procedure and stand it up for release wheels. Under
threaded dispatch every opcode handler is its own function reached by tail
call, so a profile's value is in laying out the hot handlers and helpers
together and in the inlining decisions inside them.
Design: [interpreter/pgo-build.md](interpreter/pgo-build.md).

Two pieces of work:

- **Evaluate the procedure.** Prototype `make build-pgo` (three-pass:
  instrument → run harness → optimize) and measure the delta vs `-O2` on
  `churn --all`, machine-normalized. Then wire the per-cell three-pass
  build into cibuildwheel (clang/llvm-profdata on every platform, since
  the extension builds with clang), profile regenerated per
  `os × Python-version` cell.

- **Synthesize the profiling run set.** Author the harness as a representative
  mix, weighted toward the universal hot machinery (dispatch, `CALL_*`, loop,
  branch, namespace access): a subset of the phased test suite for coverage,
  `churn --all` repeated for hot-path weight, and a set of example Koskari
  programs for realistic distributions. The harness is the durable,
  version-controlled asset; the profile data is a per-target build intermediate.

---

## Compiler

### Extent verification and dead-region sweep

Add the TAIL-under-non-discardable-extent check to the finalize walk
(`derive_extents_fill`), which already verifies extent balance and
cannot be mocked out; settle per-kind CLEAR semantics first. The
Python and C rulesets stay deliberately distinct: C is the legality
rule. Dead-region sweep and label GC stay last, if at all.

Ref: [extent-verification.md](extent-verification.md).

### Values pack as the multi-value carrier (design)

`return` and `yield` pop a count into a tuple, `break` and `continue`
carry one value, and a keyword among the values rewrites the source to
a call of `values` by name, which a binding of that name shadows. A
`compile/values` special and a `BUILD_VALUES` opcode put one pack at
every delivery and every receiver.

Ref: [language/values-pack.md](language/values-pack.md).

### Scratch slots for compiler temporaries (implement tiers 1 and 2)

A pool of tokens a fragment's context manager hands out for the
extent of a special's emitted code, declared once as locals and
reused by non-overlapping uses, then their own tuple on the spec so
they leave the locals set; a scratch node with its own instructions
is an optimization to measure after. First user: the match form's
cursor, mark and items temporaries.

Ref: [interpreter/scratch-slots.md](interpreter/scratch-slots.md).

### Externs slot in NamespaceSpec (investigate)

A fourth name list on `NamespaceSpec`: names a frame reads from a
fully qualified module attribute rather than a scope, so a template's
free name can resolve in the macro's own module. Bound at frame
creation or lazily through a cached read opcode; the qualifier comes
from the expanding transformer. Frame-level definitions stay on the
use-site rule.

Ref: [namespace-externs.md](namespace-externs.md).

### Scope declaration positions (implement)

The resolution keeps a position only for the first write to a name.
Record one for the first request, define and define-global of each
name too, so a duplicate or conflicting scope declaration (a define
and a define-global of one name in a frame) raises at the line of the
earlier form and names the later one, instead of the bare message.

Ref: `koskari/compile/pragma/resolution.py` (`on_declare`,
`on_explicit_global`, `_write_positions`),
[language/RESOLUTION-MODEL.md](language/RESOLUTION-MODEL.md).

### Resolver candidate spellings (investigate)

`Resolver.choose` rebuilds a marked atom's prefix gensyms and their
spellings on every call, and `resolve` calls it per bucket entry and
per variable operand. Profile a macro-heavy compile; if it shows, keep
the spellings per atom for the life of the resolver.

Ref: `koskari/hygiene.py` (`candidates`, `Resolver.choose`).

### Dict literal compression (review)

A theoretical `compile/#dict` form could compress `(#dict ...)` calls
-- template-copy for fully-constant literals, `BUILD_KWDS_NAMES` for
constant-key runs -- once the head-identification gate (resolution
proof vs pragma) is settled. Backed off from the removed special
(56d77346) in favor of the uniform call lowering; the methods, the
gate options, and the equivalence obligations are recorded for review.

Ref: [language/dict-literal-compression.md](language/dict-literal-compression.md).

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

## Reader

### Multi-character reader macro triggers (investigate)

Reader macros key on one character, so `#'`, `` #` ``, and the `#`
collection openers are branches inside the `#` handler, repeated in the
layout block predicate and the lexer. Investigate a registration keyed
by a trigger string, dispatched longest first under the first
character, that the layout reader and lexer read as data.

Ref: [reader-macro-triggers.md](reader-macro-triggers.md).

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

### Module `__dict__` as a dict (investigate)

The Koskari Module in `sys.modules` exposes a `ModuleNamespace` as its
`__dict__`, which `exec` refuses, so `@dataclass`, `typing` and
`inspect` fail on imported `.ki` modules. Options: a dict subclass that
reads through the namespace and purges anything written into it on the
next read, or dict-backed ItemRef bindings made invisible by dict
version or watcher caching.

Ref: [interpreter/module-dict.md](interpreter/module-dict.md).

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
- **`reference/vm/`** — contract lives in one large `index.md`; ~125 of 198
  `opcodes/*.md` pages are thin stubs. Split maintainer-facing pages from the
  spec (standalone bytecode-format, disassembly-and-debugging,
  peephole-and-lowering) are not separate; per-opcode public entries need depth.
- **`project/vm-and-opcodes.md`** — contributor opcode checklist (add to
  `opcodes.incl`, regen handlers, update VM reference) not written.
- **`patterns/recipes/`** — expand as usage surfaces gaps.
- **`exercises/`** — optional exercise track from the meta spec not started.
- **Maintenance process** — per-page stability labels not applied outside corpus
  records; tutorial/guide examples are not corpus-linked or verified; no CI guard
  for opcode-doc drift vs `koskari/lib/opcodes.incl`.
- **`changelog.md`** — single 0.1.0 entry; needs release-driven updates.
  **`faq.md`** is thin in places (e.g. free-threading answer incomplete).

---

## To design (broader)

### C stack at the Koskari-to-CPython call boundary (investigate)

A Koskari call out to CPython that re-enters Koskari costs 1,424 bytes
of C stack per level, 1,104 of them in Koskari's own frames. Attempt
tail-call threading from the call site into the CPython call, or a
CPS-style rebound to the trampoline that then tail-call threads, so the
handler and call-path frames are gone before CPython runs.
Ref: [interpreter/demand-flattening.md](interpreter/demand-flattening.md) (step 6).

### Context snapshot after completion (investigate)

A boundary drive's root is now a code-less frame with no namespace, so
a retained `Context` reports `namespace` as None once the drive ends.
Investigate a snapshot API that preserves a finished context's state
(namespace, last frames) for later inspection.
Ref: [interpreter/demand-flattening.md](interpreter/demand-flattening.md).

### Extents across a generator yield (investigate)

A yield moves the span above the GEN off the chain intact, so a
`dynamic-wind` or `with` inside a generator holds its state while the
consumer runs and does not re-establish it at the resume. PARAM restores
its cells in `kos_dyn_extent_freeze` and re-enters them in
`kos_dyn_extent_install`; decide whether other kinds need a leave and
re-enter walk without unlinking.
Ref: [interpreter/generator-extent-freeze.md](interpreter/generator-extent-freeze.md).

### Closing a delegate through `yield-from` (investigate)

A throw into a generator suspended in `yield-from` reaches the
delegate; GeneratorExit does not. It raises at the frame and the raise
walk closes a sync generator but only marks a KosIter finished and
drops a Python async generator, so their cleanup runs at collection,
not at the relay's aclose. Decide whether GeneratorExit should route
through the resume so the drive can await the delegate's close.
Ref: [interpreter/yield-from-close.md](interpreter/yield-from-close.md).

### Traceback creation: frames between the catch points (investigate)

A raise records the whole dynchain on the exception, and every faux
frame injected for it carries that whole record as `__koskari_trace__`,
so a CLI traceback prints the chain once per frame and shows the
launchpad. The record must hold only the frames between the raise
point and the catch point, and a frame must carry only its own entry.

Ref: [EVALUATOR.md](EVALUATOR.md) (§4.3 Traceback Injection).

---

## Recorded proposals (evaluate / implement)

The following items are captured from design discussion. Each has a short
design note and is open for evaluation and possible implementation.

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

### Formal default modes (`value:` / `copy:` / `new:`)

Generalize a defaulted formal from *a value to share* into *an operand plus an
operation applied at each application that omits the argument*: `value:`
(share), `copy:` (duplicate), `new:` (call). By-ref formals default to `copy`.
**Review and implement for feel**, with the explicit intent of easing the
transposer use-case.

Ref: [language/formal-default-modes.md](language/formal-default-modes.md).

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

### Dataclass builder in Koskari (proposed)

`@dataclass` over a `class` applies the stdlib decorator, which
generates the methods by `exec` against the module's `__dict__` and so
leans on the module namespace relaying as a dict. A `dataclass` builtin
in the bootstrap would collect the annotated fields, set
`__dataclass_fields__` and `__dataclass_params__` for the stdlib
helpers, and compile `__init__`, `__repr__`, `__eq__` and the rest as
Koskari functions.

Ref: [language/dataclass-builder.md](language/dataclass-builder.md).

- **Reader macro helpers** — bootstrap **`set-macro-character`** /
  **`read-atom`** helpers for `examples/reader-macro/dollars.ki`, reader
  tests, user recipe. Ref:
  [examples/reader-macro-dollars.md](examples/reader-macro-dollars.md).

### Ref surface (notes from the refs guide)

- **`(set! (deref r) v)` — dedicated ref-store opcode** — `set!` on a deref
  dispatches to the ref's `store` method today. A dedicated opcode would
  validate `r` is a `Ref` type rather than duck-typing on `.store`, and avoid
  the method-call overhead.

(The proposed `*foo` deref reader sugar is a separate open idea — see
[ideas.md](ideas.md). It has been removed from user-facing docs pending a
decision.)

---

## Corpus

### Corpus generation agent

Run the supervised worktree generation pass: grow `docs/corpus/records/`
to hundreds–thousands of entries covering every builtin special and macro
via an autonomous Claude Code agent in its own git worktree, one PR per
batch. Mandate per special/macro: basic, variant, block/layout, and
invalid+corrective entries; priority on patterns common in real code
(`define-class`, `for-each`, `define-syntax`, `try/except`, `let`, async).
The PR's `make test` (`phase_corpus`) is the authoritative backstop.

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
