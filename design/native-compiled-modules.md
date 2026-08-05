# Native Compiled Modules: AOT Lowering of `.kio` to Shared Objects

## Status

Direction record. Not scheduled; deliberately kept off the TODO. This
captures the design conversation so the ideas and their rationale survive
until an MVP is attempted.

## Summary

Offer a fully native compiled form for Koskari modules: a platform shared
object (`.so` / `.dylib`) that Python imports as an extension module, whose
loaded structure is ordinary Koskari objects, and whose functions execute as
compiled C rather than bytecode under the dispatch loop.

The lowering is one step past the `.kio` format. Where `.kio` persists `Code`
objects for the evaluator to dispatch, the native form emits each `Code`
object's opcode sequence as straight-line C: the same per-opcode handlers,
sometimes called and eventually inlined, with the fetch/decode/dispatch loop
compiled away. The eval loop remains as the scheduler; compiled functions
enter it through the existing native-frame mechanism (`native_handler` on
`KosDynFrame`) and return `KosOpResult` like any handler.

This is template AOT compilation in the same family as Cython's generated C,
protothreads, and CPython's copy-and-patch JIT. It is also the payoff for two
standing disciplines: opcode "hard parts" extracted into helper functions and
eval-loop event handlers, and all frame state kept heap-resident on the
dynamic chain.

## The compilation model

A compiled function is a C function with the `KosNativeHandlerFn` signature
([koskari/lib/eval.h](../koskari/lib/eval.h), `KosNativeHandlerFn`). Its body
is a jump table over resume points followed by segments of straight-line
code:

```c
switch (KosDynFrame_PC(frame)) {
  case 0:  goto pc_0;
  case 17: goto pc_17;   /* only externally-enterable pcs */
  default: return EVAL_FATAL();  /* asserted contract; see below */
}
pc_0:
  /* inlined cheap ops; helper calls for the rest */
  ...
  KosDynFrame_SET_PC(frame, 17);
  return KosEvalState_Call(...);   /* segment boundary: trampoline */
pc_17:
  /* consume delivered value, next segment */
```

Two decisions are forced by the existing architecture, and both are load
bearing:

**Bytecode pc remains the universal resume coordinate.** The dynchain
machinery addresses resumption exclusively by pc: `handler_pc` on TRY and
FINALLY nodes, `pop_pc` on prompts, `resume_pc` on cc nodes, and the
`saved_pc` / `saved_acting_pc` pair in the delimited-continuation INPLACE
surgery ([koskari/lib/dynchain.h](../koskari/lib/dynchain.h)). Compiled code
keeps the same pc numbering; the entry switch maps pc to label. Consequences:

- Continuation clone, restitch, the raise walk, and frozen GEN resume work
  unchanged on native frames.
- Interpreted and compiled frames are interchangeable; position tables and
  backtraces survive; a missing or ABI-stale `.so` falls back to
  interpreting the same `Code`.
- Internal branches compile to plain `goto`; the switch only carries pcs
  that can legally arrive from outside.

**Calls stay trampolined.** The loop remains the scheduler. A call is: set
pc, install the callee via `KosCallable_Call`, return the result to the
loop. This is what keeps ambient async, capture-through-callee, and
multi-shot resumption working with zero new mechanism. Direct C-to-C calls
are impossible anyway while any callee can suspend or be captured;
"provably non-suspending callee" specialization is explicitly a later tier,
and an escape-analysis problem, not a flag.

### The jump table

The externally-enterable pc set is statically enumerable at emit time,
because it is exactly the set the runtime machinery is allowed to inject:

- pc 0 (fresh install);
- the pc after every call / await / yield site (delivered-value
  resumption -- `KosNativeHandlerFn` normalizes delivery to one owned
  `PyObject *`, so each resume label begins by consuming the delivered
  value or NULL);
- every `handler_pc` emitted into TRY / FINALLY nodes;
- `pop_pc + 1` for prompts, `resume_pc` for cc, wind after-thunk returns.

The default case traps with `EVAL_FATAL()`, and that is a feature: the
interpreter will start dispatching at any pc it is handed, so a
chain-surgery bug producing a bogus resume pc silently executes wrong code.
A native frame entered at an unlisted pc fails loudly. Every resume through
compiled code becomes an asserted contract on the dynchain machinery,
concentrated exactly where the risk is (the INPLACE restitch path).

v0 widens the table: while branch opcodes are emitted as handler calls,
their `KOS_RES_DISPATCH` results re-enter the table at arbitrary branch
targets, so v0's table also carries every branch target (still static, read
off the `Code` object). Inlining control flow collapses the table back to
the externally-enterable set.

### Invariants

**No C-local state lives across a segment boundary.** Frames are already
heap-only, so the only per-invocation C state is what the emitter chooses to
cache. Segments must be cleanly re-enterable: a cloned frame (multi-shot
continuation) re-enters the same case label, so no C statics and no one-shot
setup outside the frame. Handlers already live under this rule because the
interpreter re-executes them on resumption.

**The heap stack stays canonical wherever capture or unwind can occur.**
Nearly every opcode can raise, so a spill-before-throwing-op discipline
would erode stack caching to nothing. The planned middle ground is a
write-through stack cache: pushes write both the C local and the heap
stack; reads come from locals. Capture and unwind always see a correct
stack; the win is eliminated reads, pointer chasing, and pop bookkeeping.
Full elision inside provably non-throwing segments is a later optimization.

**`acting_pc` stays honest.** The raise walk and extent lookups
(`kos_try_handler_for`, position tables, backtraces) key off `acting_pc`.
Generated code updates it before any op that can raise or capture, matching
the interpreter prologue. This is the second coordinate of the ABI contract,
next to "pc is the resume coordinate."

## What already exists

Verified against the tree; the two hard prerequisites are built and
exercised, not hypothetical:

- **The native-frame ABI is complete.** `KosDynFrame` carries a
  `native_handler` slot beside `pc` / `acting_pc`
  ([koskari/lib/dynchain.h](../koskari/lib/dynchain.h)). The loop invokes it
  at every entry point the design needs: fresh iteration
  (`eval_step_native_iteration`), return delivery from a callee
  (`eval_parent_return_deliver_impl`), and revive-with-value after
  suspension ([koskari/lib/eval.c](../koskari/lib/eval.c)). Frame clone
  copies the handler pointer; snapshots record it. `apply_native_handler`
  in [koskari/lib/utils.c](../koskari/lib/utils.c) is a live in-tree
  consumer proving the pattern round-trips.
- **All opcodes are standalone uniform handlers.** One file per opcode in
  [koskari/lib/opcode/](../koskari/lib/opcode/), `KOS_OP_HANDLER(NAME)`
  taking `(es, insn)` and returning `KosOpResult`. The v0 emitter is
  therefore generic over the whole opcode set from day one; there is no
  per-opcode porting backlog.
- **`tools/opimpl.py`** already owns a machine-readable enumeration of the
  opcode set (it generates `handlers.incl` from `opcodes.incl`). The AOT
  emitter becomes a second consumer of the same source of truth, so the
  compiled form cannot drift semantically from the interpreter.
- **`KosCallableTypeObject.tp_kos_install`**
  ([koskari/lib/callable.h](../koskari/lib/callable.h)) is the hook a
  compiled function uses to install itself into the eval state.

## Toolchain

Emit C; compile with the platform toolchain (macOS and Linux only; no
Windows). The driver is thin and ours:

- Flags from `sysconfig` (`include` path, `EXT_SUFFIX` for output naming so
  the importer's finder logic is trivial); `-O2 -fPIC -fvisibility=hidden`
  with only `PyInit_*` default-visible.
- Linux: `cc -shared`. macOS: `cc -bundle -undefined dynamic_lookup`
  (Python symbols resolve at import; no libpython link), honoring
  `MACOSX_DEPLOYMENT_TARGET`; host arch only unless distributing.
- Compile cache keyed on (kio content hash, koskari ABI version, Python ABI
  tag, compiler id). One translation unit per module; feels like `.pyc`
  generation, slower the first time.

**No link-time dependency on koskari: capsule API table.** The runtime is
itself an extension loaded RTLD_LOCAL; linking generated modules against it
means shipping a real shared `libkoskari` with rpath / install_name games,
or flat-namespace hacks. Instead, the NumPy pattern: the runtime exports a
versioned struct of function pointers (the 192 opcode handlers plus the
helpers generated code touches) via `PyCapsule`; generated modules include
koskari headers for the inline/macro layer and fetch the table at init,
failing cleanly on ABI-version mismatch. After this decision the compile
step needs exactly three inputs -- a C compiler, Python headers, koskari
headers -- which is what makes a minimal driver viable instead of a build
system.

Rejected alternatives, for the record:

- `setuptools` / `build_ext` programmatically: slow, noisy,
  distutils-compat baggage; we would use 5% of it, and its edge-case
  coverage matters least on exactly the two platforms we claim.
- `llvmlite` (emit IR directly): forfeits the premise. The semantic anchor
  is that opimpl.py's C and the koskari headers are the single source of
  truth; IR means reimplementing every inlined op in a second language.
  Also geared to JIT, not standalone `.so` production.
- `tcc`: compiles fast, optimizes barely; register allocation across
  inlined ops is where the win past dispatch elimination lives.
- `libgccjit`: macOS-hostile.
- The `ziglang` wheel (`zig cc`) is the optional hermetic back pocket:
  wrong default (~50 MB dependency), right answer later for cross-building
  prebuilt-`.so` wheels from one CI machine. The driver takes the compiler
  command as a parameter so the slot exists.

An ABI design lever to protect early: if generated code's *direct* CPython
API surface is kept abi3-clean (version-specific machinery such as
INSTANCE_VALUE already lives behind koskari helpers, which are per-version
builds anyway), consumer `.so`s can be abi3 wheels -- one per platform/arch
instead of one per Python version. Not a v1 guarantee; do not let an inline
header casually break it.

## Workflows

### Koskari's own bootstrap (first, and the proving ground)

No ABI-across-packages problem exists here: generated code and the runtime
it calls are built in the same tree, same CI job, same wheel; the capsule
version cannot mismatch. The build gains a classic staged-bootstrap step:

1. `build_ext` builds `koskari/lib` in place, as today.
2. A post-build hook runs the freshly built runtime + Python compiler over
   `koskari/bootstrap/*.ki`. The chicken-and-egg resolves the normal way:
   interpret to compile (as `bootstrap/__init__.py` does now), then the
   compiled artifacts serve every subsequent run.
3. The emitter lowers each `Code` to `.c`; the driver compiles one `.so`
   per bootstrap module, placed beside the `.ki`.
4. Bootstrap load order becomes `.so` > `.kio` > `.ki`, with `.ki` sources
   still shipped: debug builds and safety-net runs interpret, and the
   fallback is free.

Distribution cost is zero: koskari's wheel is already platform-specific
because of the C extension, and the bootstrap `.so`s ride the same
cibuildwheel matrix. Use the capsule here anyway (rather than linking
generated code into the build directly) so bootstrap exercises exactly the
pipeline consumers get.

### Consumer project building a wheel (later; gated on measured wins)

Modeled on mypyc / Cython, opt-in:

- `[build-system] requires = ["setuptools", "koskari"]` plus a build hook
  koskari provides, discovering `.ki` modules and compiling
  `.ki` -> `.kio` -> `.c` -> `.so` with `EXT_SUFFIX` naming.
- The wheel tag flips to platform-specific; the wheel ships `.ki` and/or
  `.kio` alongside the `.so`.
- At import, the importer prefers `.so`, checks the capsule ABI version,
  and on mismatch falls back to the shipped `.kio` / `.ki`. This is the
  load-bearing decision: a koskari upgrade degrades installed wheels to
  interpreted, it never breaks them. The wheel's `koskari>=X,<Y` pin is a
  performance contract, not a correctness one.
- Consumers who skip the hook keep pure `py3-none-any` wheels.

## Honest payoff analysis

What v0/v1 genuinely buys: dispatch/decode elimination, operand
immediates, write-through stack caching, C-compiler register allocation
across inlined ops, mmap'd shared code pages instead of heap bytecode
(multi-process deployments), near-zero module load cost. On
interpreter-bound straight-line code (longops / churn territory),
plausibly 1.3-2x.

What it does not touch: the cost the churn work identified as dominant on
the method-call path -- frame + namespace allocation per call -- and
PyObject boxing generally. Koskari code is call-heavy, segments are short,
and every call still round-trips the trampoline. Realistic code likely
sees single-digit to ~20%; CPython's copy-and-patch JIT landing at ~5-10%
initially is the sobering comparable. If the motivation were only startup
time, plain `.kio` caching is the cheap 80%.

Where the true payoff is: substrate. Template AOT is the first rung of the
ladder every serious tiered system climbs. Once code is C with a real
optimizer behind it, segment-level specialization, guard-elided fast
paths, and eventually provably-non-suspending direct calls become emitter
features rather than interpreter surgery. The bootstrap workflow
specifically is nearly free payoff: first-party, no ABI exposure, ships in
an already-binary wheel, and speeds the builtins every program runs
through.

Sequencing rule: build the pipeline for the bootstrap workflow only,
benchmark with the churn suite against the predictions above, and let the
numbers decide whether the consumer workflow -- where all the ongoing cost
lives (ABI windows, wheel matrices, build-hook support burden) -- earns
its keep.

## MVP scope

Low-polish, subroutine-threaded, demonstration-grade:

- **No inlining.** Every instruction is a call to its existing
  `KOS_OP_HANDLER` through the capsule table, plus result-kind plumbing:
  NORMAL falls through; DISPATCH re-enters the jump table on the updated
  pc; everything else stores pc and returns to the loop.
- **Companion-`.so` attach, no module wrapper.** The importer loads the
  `.kio` exactly as today, dlopens a companion `.so` exposing an
  ordinal -> function-pointer table, and maps it onto each `KosCode` (one
  new field). Frame push sets `NATIVE_HANDLER` when the code has a native
  body. Consts, formals, and positions keep coming from the `Code` object;
  execution is the only thing redirected.
- **Success criteria:** a bootstrap function runs native; a coroutine
  awaits inside a native frame; a call/cc captured inside a native frame
  resumes twice (multi-shot through clone); a slice of the corpus and
  phases 6/8 agree between interpreted and native; churn suite produces
  the baseline number.

Work items, in order:

1. **Loop-duties audit** (the one real unknown). Between opcodes the
   dispatch loop does more than fetch: transfer-plan drain,
   pending-exception poll, GIL poll, trace/safetynet wraps, `acting_pc`
   maintenance. Write the ABI contract note: which duties generated code
   must replicate between handler calls (GIL poll at back-edges,
   `acting_pc`, pending checks) and which are deliberately skipped
   (native frames bypass `KOS_EVAL_SAFETYNET` and VERBOSE_DEBUG; debug
   means interpret). This is where MVP scope could grow.
2. **Capsule handler table** generated from `opcodes.incl` via opimpl.py,
   plus the `KosCode` native-fn field and the frame-push hookup.
3. **Emitter v0 + driver.** Generic over the opcode set; jump table with
   branch targets included; sysconfig + cc driver with the compile cache.
4. **Differential testing and the long tail:** continuations, wind,
   generator resume through native frames; the delim INPLACE worked
   example (shift body captured inside a compiled frame, resumed twice)
   as the first design-doc scenario to hand-verify.

Estimated at 6-8 focused sessions to the success criteria. The estimate is
short because the expensive 80% -- uniform handlers and the event-driven
loop -- was paid for in advance, deliberately.

## Open questions

- Exact per-iteration duty list from the audit, and whether any opcode
  still depends on loop-local state not reachable from `(es, insn)`.
- Whether the pc-as-label-space choice fully survives the delim INPLACE
  chain surgery; the worked example above is the test.
- Capsule table granularity: handlers only, or handlers plus a curated
  helper set (stack ops that are real symbols, `KosEvalState_Call*`,
  raise-path entry points).
- abi3 cleanliness of the inline header layer used by generated code.
- Whether bootstrap modules eventually merge into a single multi-init
  `.so` (marginal; per-module keeps uniformity with consumers).

## Related

- [interpreter/kio-format.md](interpreter/kio-format.md) -- the format this
  lowers one step past.
- [self-hosted-compiler-bootstrap.md](self-hosted-compiler-bootstrap.md) --
  the staged-bootstrap pattern the build step reuses.
- [OPCODES.md](OPCODES.md), `tools/opimpl.py`, `koskari/lib/opcodes.incl` --
  the single source of truth the emitter consumes.
