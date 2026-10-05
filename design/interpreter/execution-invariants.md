# Koskari Execution Model — Invariants & Decisions

This document records the agreed execution semantics, invariants, and boundary
policies for Koskari, intended for IDE/agent ingestion. These are *design
commitments*, not exploratory notes.

**Scope**: Numbered invariant reference focused on execution semantics and boundary decisions. For the combined master reference (invariants + type/C-struct details + implementation status), see [../INVARIANTS-AND-CLARIFICATIONS.md](../INVARIANTS-AND-CLARIFICATIONS.md). For the full VM API reference, see [evaluator.md](evaluator.md).

---

## 1. Dual Execution Perspectives (Invariant)

Koskari execution is observed from **two distinct perspectives**:

1. **Koskari View** — when Koskari bytecode evaluates and calls other Koskari
   functions.
2. **Python View** — when Python code calls into a Koskari callable.

These perspectives are projections over a *single evaluator*, not separate
runtimes.

---

## 2. Single Evaluator, Heap-Resident Stack (Invariant)

* Koskari is a bytecode-based evaluator implemented as a native C extension
  running inside CPython.
* Koskari owns its **entire execution stack** as heap-resident frame objects.
* The evaluator is internally **flattening (stackless)**: nested Koskari calls
  do not consume the C call stack; execution state is represented by
  heap-allocated linked structures. Dynamic nodes and frames are interleaved
  into a heap-allocated linked list representing execution history.
* The evaluator runs inside **one C call** per drive/resume.
* Koskari never relies on CPython's recursive `ceval` re-entry for its own
  control flow.
* The evaluator is driven by `koskari_eval()`, which is invoked when a
  KosCoroutine or KosPyAsyncGen is driven (e.g. via Python `await` or `send`).
  KosCoroutines are created by calling a KosExpression or KosFunction (each
  returns a new KosCoroutine). Calling a KosPyContinuation from Python raises
  `KoskariContinuationApply` for upwards transfer; it does not return a
  KosCoroutine.

This enables flattening of async suspension without C-stack growth.

---

## 3. Callable Surface Classification (Invariant)

Every Koskari callable has an explicit surface classification determining what
Python sees when it is called:

* **Sync Function** → returns a value or raises
* **Async Function** → returns an awaitable (coroutine-like)
* **Generator Function** → returns a generator
* **Async Generator Function** → returns an async generator

Python-visible behavior is *entirely determined* by this classification.

---

## 4. Internal Completion Signals (Invariant)

The evaluator produces **internal completion signals** that do not directly
correspond to CPython opcodes:

* `RETURN(value)` — computation completed
* `YIELD(value)` — generator yield
* `AWAIT(awaitable)` — suspension awaiting dependency
* `RAISE(exception)` — error propagation

These signals are consumed by the Koskari driver or wrapper layer.

When the current frame has a **parent** (e.g. callee of a Koskari CALL or
attached async gen frame), YIELD and RETURN do **not** produce a boundary
signal; they are handled in-loop by detaching the frame and pushing the
value onto the parent, and the dispatch loop continues with the parent.
Only the top-level frame (no parent) produces a boundary event on
yield/return/raise/await.

---

## 5. AWAIT Is a VM Suspension, Not a Yield (Invariant)

* `AWAIT` **always suspends the entire Koskari VM immediately**.
* No partial unwinding or nested evaluator calls occur.
* Control returns directly to the current boundary:
  * a Python coroutine wrapper, or
  * a sync adapter runner.

This is a deliberate divergence from CPython's `YIELD_FROM` ladder.

---

## 5a. Defer Objects Are Not Eagerly Awaited (Invariant)

* Values of type **defer** (single-shot await-handle) are **never** auto-awaited by the VM.
* CALL return paths and other implicit await sites push a defer value as a **plain value**; they do not drive it.
* Only the **explicit AWAIT opcode** drives a defer (invokes its target and delegates to any resulting awaitable).
* This allows defer to be passed around as a handle and awaited exactly when the programmer chooses. See design/api/defer-coroutine.md.

---

## 6. Flattened Async Semantics (Invariant)

* Koskari async suspension is **flattened**:
  * no recursive C calls per `await`
  * no rebuilding of interpreter ladders on resume
* Resume re-enters the evaluator once, with the **entire Koskari stack intact**.

Await nesting depth affects heap frames only, never the C stack.

---

## 7. Python Awaitable Interop (Invariant)

* Koskari may await **Python awaitables** (coroutines, futures, etc.).
* These are treated as opaque state machines.
* Koskari does **not** recurse into CPython's await machinery.

Instead, Python awaitables are *iteratively stepped* by the boundary wrapper.

---

## 8. Await Probe Optimization (Decision → Invariant)

When Koskari encounters `AWAIT(py_awaitable)`:

1. The runtime obtains the await-iterator (`__await__`).
2. It performs **one speculative step** (`send(None)`).

Outcomes:

* **Immediate completion** (`StopIteration(value)`):
  * No suspension occurs.
  * `value` is injected back into the Koskari VM.

* **Yielded value**:
  * Suspension occurs.
  * The yielded object is passed to the Python event loop.
  * The await-iterator is stored for later resumption.

This preserves Python semantics while avoiding unnecessary event-loop hops.

---

## 9. Single Wrapper, Iterative Driving (Invariant)

From Python's perspective:

* Each async Koskari call produces **one wrapper object**.
* That wrapper:
  * resumes the Koskari VM
  * yields Python awaitables outward when needed
  * receives results/exceptions back

There is no recursive Python coroutine chain per nested Koskari `await`.

---

## 10. Sync Adapter Policy (Invariant)

* Blocking execution is **explicitly opt-in**.
* Sync execution is performed by a **dedicated event loop** created by Koskari.
* This loop drives the Koskari awaitable to completion.

Rules:

* If no Python event loop is running: use a local loop in-thread.
* If a loop is already running: use a helper thread with its own loop.
* Sync execution never happens implicitly.

---

## 11. Ownership and Re-entrancy (Invariant)

* A Koskari frame/awaitable has **exactly one active driver** at a time.
* Concurrent or re-entrant resume attempts are errors.
* Wrapper or runner objects enforce this invariant.

---

## 12. Generator vs Async Generator Separation (Invariant)

* Generators are **not awaitable**.
* Async generators are **not awaitable**.
* Async functions return awaitables.

Yielding and awaiting are disjoint control paths internally and at the surface.

---

## 12a. Control Flow (Invariant)

* Koskari supports **local-only jumps** restricted to the current block context.
* Local-only jumps implement continue, break, conditional control flow, and
  try/except/finally.
* **Non-local jumps** across block or dynamic extents are not permitted except
  via continuations.

---

## 13. Error and Cancellation Propagation (Decision)

* Python exceptions raised inside Koskari propagate outward normally.
* Cancellation (including Ctrl-C in sync mode) injects a cancellation exception
  into the Koskari VM.
* Python exception chaining (`__cause__`, `__context__`) is preserved across
  boundaries.

---

## 14. Python Boundary Constraint (Invariant)

* **Delimited continuations**: Captured continuations (shift) **MUST NOT include
  Python/C call stack segments**. Delimited capture may not cross the CPython C
  calling-convention boundary.
* **Full continuations**: May cross the CPython boundary only in the *upwards*
  direction (from callee toward caller). Crossing is implemented by raising a
  specialized `BaseException`, forcing a single return through intervening
  Python/C frames until the owning Koskari evaluator loop is reached.
* The dynamic chain does **not** track Python boundaries with nodes. One chain
  per evaluator context (driver). Delimited capture only finds prompts in the
  current chain. Delimited invocation from Python is **blocked** at
  `DelimitedContinuation.tp_call` (always raises).
* Python and C frames are **not** embedded in the Koskari dynamic chain; only
  Koskari frames are re-enterable.

**Rationale**: Python call frames cannot be cloned or resumed. Including them
would create non-resumable continuations.

**Implementation**: `kos_validate_capture()` verifies prompt reachability from
`dyn_top` on the chain. Context/token identity and type-level blocking enforce
the boundary constraint.

---

## 15. Unified Dynamic Chain (Invariant)

* All execution context (frames, prompts, winds, regions) is tracked in a
  **single unified dynamic chain**.
* The chain is a linked list of `KosDynNode` structures with typed payloads.
* Walking the chain provides complete dynamic extent information.
* All mutations to the chain go through `kos_dyn_push`, the single unlink
  site `kos_dyn_unlink_top` (behind the `kos_dyn_pop_*` entry points and the
  unwind walks), or a transfer goal's landing. No direct chain manipulation
  is permitted.

**Node types**:
* `KOS_DYN_FRAME` — function activation record
* `KOS_DYN_PROMPT` — reset/shift delimiter; owns a COW stack block for the delimited region; BARRIER + HANDLES_EXCEPTIONS
* `KOS_DYN_SHIFT` — shift block marker; simple, no stack block or handlers
* `KOS_DYN_WIND` — dynamic-wind extent (before/after hooks)
* `KOS_DYN_TRY` — try block (handler_pc, enabled; owns stack block)
* `KOS_DYN_FINALLY` — finally block (handler_pc, enabled, handling_raise; owns stack block)
* `KOS_DYN_LOOP` — while loop (break_pc, continue_pc, enabled; owns stack block)
* `KOS_DYN_FOREACH` — for-each loop (extends LOOP with iter, exhausted, iter_next_else_pc; owns stack block)
  (These four are the region kinds; each has only its own `KosDynNode_IS_*` test.)
* `KOS_DYN_NS` — namespace scope (PUSH_NS/POP_NS)
* `KOS_DYN_CC` — call/cc extent (PUSH_CC/POP_CC)
* `KOS_DYN_YIELD_FROM` — yield-from delegation state (see [EVALUATOR.md](../EVALUATOR.md) §5; [archive/yield-from-execution-model.md](../archive/yield-from-execution-model.md))
* `KOS_DYN_PARAM` — parameterize extent (PUSH_PARAM/POP_PARAM); binds/restores dynamic param
* `KOS_DYN_WITH` — with context manager extent (PUSH_WITH/POP_WITH, PUSH_DYN_WITH/POP_DYN_WITH for the multi-shot shot); CATCH, CONTROL_HOOK, HOLDS_OBLIGATION

**Capability bits** govern behavior without kind checks in generic handlers:
* `KOS_DYN_CAP_CATCH` — node may swallow exception (WITH)
* `KOS_DYN_CAP_BARRIER` — normal pop refused (PROMPT)
* `KOS_DYN_CAP_HANDLES_EXCEPTIONS` — node has `unwind_on_exception` handler invoked during exception unwind when pop is refused (PROMPT, TRY, FINALLY)
* `KOS_DYN_CAP_CONTROL_HOOK` — node answers the extent contract (enter, leave, settle, abort): WIND, WITH
* `KOS_DYN_CAP_HOLDS_OBLIGATION` — node carries an obligation slot: WIND, WITH
* `KOS_DYN_CAP_NEEDS_INSTALL_WIND` — node re-enters when a transfer lands it (PARAM, multi-shot WITH)
* `KOS_DYN_CAP_OWNS_NS` — node introduced its own namespace, which the collector reaches through it (code FRAME, NS, COLLECT)
* `KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP` — preserve exception across pop (YIELD_FROM)

---

## 16. VM-Native Wind/Unwind Thunks (Invariant)

* Wind/unwind thunks are **null-arity Koskari callables** (bytecode blocks)
  invoked via `kos_callable_call`, not Python callables.
* Thunks run as normal frames in the eval loop; when a thunk returns into
  its WIND, `kos_dyn_settle_top` advances the node, and the walk or transfer
  goal that was parked on it continues.
* Thunks capture lexical context via closure.
* **Local jumps and wind/unwind are distinct**: local jumps do *not* invoke
  wind/unwind handlers; continuation transfer *does* invoke them.

**Rationale**: Using the normal call path allows thunks to participate in
continuation capture and avoids special hook machinery.

---

## 17. Continuation Semantics (Invariant)

* Koskari implements both **delimited continuations** (reset/shift) and
  **reusable full continuations** (call/cc).
* Delimited continuations are restricted to the same evaluator context.
* Continuations capture a reference to their owning evaluator context.
* Each evaluator context records an explicit lifecycle state (at least: active,
  sleeping/await/yield, exited). It is an error to apply a continuation whose
  owning context state does not permit invocation. Attempting to invoke an
  unusable continuation (delimited or full) raises an exception at the
  invocation site.
* Delimited continuations capture from the current point up to (but not
  including) the nearest matching prompt.
* Captured continuations are **multi-shot** by default.
* Continuation invocation executes proper wind/unwind hooks in correct order.

---

## 18. Transfer Goal Re-derivation (Invariant)

* Continuation application is a **transfer goal**: a record of where control
  is going (seam, target, payload), never of what to do.
* The driver derives its next step from the live chain on every pass, so a
  chain that moved while the goal was parked is its input, never a hazard.
* A goal parked on a node (its thunk or `__exit__` is running, or it awaits)
  acts again only when `dyn_top` is where it was when the node fired.
* Landing is total: splice, resume pc, and the freeing of outer goals on an
  undelimited landing happen in one synchronous step.
* A failed step frees every goal and raises through the chain as it stands.

---

## 19. Introspection Is Read-Only (Invariant)

* The evaluator exposes only read-only views: `Context.snapshot()` pictures
  a context's chain, stacks, and namespaces as plain Python data, and
  exception decoration attaches `__koskari_dynchain__`, a list of frame
  dicts, to a raised exception (`decorate_exception_with_dynchain`, eval.c).
* Introspection does not mutate evaluator state, expose raw C structs, inject
  execution, or interact with continuations.
* A snapshot is plain data and stays valid however the VM continues.
* The TraceView snapshot object once proposed for this was never built; see
  [../archive/introspection-trace.md](../archive/introspection-trace.md).

---

## 20. Code Blocks Move Only at Quiescence (Invariant)

* A code object's unified block (instructions, positions, pool, extras
  rows, extents) may be relocated once, by `pack_into`, at a quiescent
  point: the module body has completed and no frame of that code is
  mid-dispatch.
* The instruction record and the extras row carry pointers into the
  block (a jump's target, an EXTRA's row, a row's LABEL target); the
  fill pass re-derives them after the move.
* Nothing that outlives the quiescent point may hold a pointer into a
  block. Frames, continuations, defers, and parked frames hold the code
  object and an integer pc; a handler holds its own record only for its
  own execution, and a resume's state never names a record.
* The instruction base is therefore never cached on a frame; the
  handler's own record is the base for its straight-line step, and a
  fetch after a frame change or a stored pc goes through `es->code`.

---

## 20a. Frame Resumes (Invariant)

* A frame's resume (`resume_kind`, a static `KosResumeKind`, and
  `resume_state`) receives the frame's next delivery instead of an
  instruction. A step resume (map, filter) lives on a code-less frame
  with the step object as its state. A node resume lives on a bytecode
  frame whose extent node owns an operation in flight (yield-from), with
  NULL state: the node is `dyn_top` above the frame.
* The kind carries the lifecycle. A resume with no delivery coming is
  cleared through the kind's `clear` (a provider step releases its
  record's busy mark and pending value); a continuation clone copies the
  state through the kind's `clone` or refuses by the kind's name (a
  yield-from phase shares its state, a provider step refuses). A node
  resume is delivered its value unsettled and is cleared by its node's
  pop and unwind.
* A bytecode frame's delivery is always the pending slot, never its
  operand stack. The revive, the generator install, and the flattened
  drive route a sent value to the slot when the frame is waiting, and
  the delivery step pops a stack only on a code-less frame.
* Nothing threads into a frame that is waiting on a resume: an installer
  asks `kos_frame_threads` and bounces through the prologue instead.

---

## 21. Non-Goals / Explicit Exclusions

* No implicit event loop creation on normal calls.
* No reliance on CPython frame recursion for Koskari semantics.
* No unification of `yield` and `await` signals.
* No Python-callable wind/unwind hooks.

---

## 22. Summary Invariant

**Koskari owns control flow.**
Python observes execution only through explicit, protocol-correct projections
(value, generator, async generator, awaitable), while Koskari internally
executes with a single flattened evaluator, a unified dynamic chain, and
delimited continuation semantics.

---

## Related Documentation

* [evaluator.md](evaluator.md) — Evaluation model and VM structures
* [../archive/introspection-trace.md](../archive/introspection-trace.md) (archived) — the TraceView proposal, never built
* [callcc.md](callcc.md) — Delimited continuation implementation
* [../archive/evaluator-restructure.md](../archive/evaluator-restructure.md) — Design document for
  the DynNode chain restructure (implemented; archived)
* [../language/async.md](../language/async.md) — Async and coroutine language
  features
* [../language/generators.md](../language/generators.md) — Generator language
  features
* [../language/control-flow.md](../language/control-flow.md) — Control flow
  including reset/shift and dynamic-wind
