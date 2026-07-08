# Koskari: Invariants and Clarifications

This document is intended for teaching another system (human or agent) about the current state of Koskari: what is designed, what is implemented, and what invariants and clarifications hold across the codebase and design docs. Use it as a single reference for "how things work" and "what must stay true."

**Scope**: Agent/human teaching master reference — covers execution invariants, type and data invariants, C structures, implementation status, and clarifications in one place. For a focused numbered invariant list, see [interpreter/execution-invariants.md](interpreter/execution-invariants.md). For the full VM API reference, see [interpreter/evaluator.md](interpreter/evaluator.md). For the **`KosEvalState`** facade (opcode-visible API), see
[archive/kos_eval_state_api_migration.md](archive/kos_eval_state_api_migration.md)
(archived migration ledger), [interpreter/eval-state-live-authority.md](interpreter/eval-state-live-authority.md),
and [EVALUATOR.md](EVALUATOR.md) §1.

**Meta**: This document records settled design facts and invariants only. No speculative alternatives are permitted here. Conflicts are recorded explicitly and left unresolved.

---

## 1. Project Identity and Lineage

**Koskari** is a LISP dialect for Python (sibilant-ng). It is the fourth iteration of the Sibilant idea, preceded by:

- **spexy**: String-to-string transformer; parsed s-expression text and emitted Python expression strings. The original proof-of-concept; the earliest reader and s-expression representation ideas trace back here.
- **python-sibilant**: Compiles to CPython bytecode (targets 3.5–3.7); provides reader, types, compiler to `.pyc`.
- **sibilant-ceval**: Custom bytecode and custom evaluator; provides Namespace, Ref, Formals, frame/stack model.

Koskari **abandons** compiling to CPython bytecode. It uses a **custom bytecode format** and a **custom C evaluator** (VM). Design and implementation draw from python-sibilant and sibilant-ceval; migration status is tracked in `design/README.md` and `design/FEATURES.md`.

**Package layout**: The runtime lives under `koskari/` (Python API and C extension in `koskari/lib/`). Design docs live under `design/` (language, api, interpreter). Tests are under `tests/` (Python and `tests/native/` for C).

---

## 2. Execution Model Invariants

These are **commitments**: they must hold for correct behavior. They are elaborated in `design/interpreter/execution-invariants.md`.

- **Native C extension**: Koskari is a bytecode-based evaluator implemented as a native C extension running inside CPython.

- **Flattening (stackless)**: The evaluator is internally flattening (stackless): nested Koskari calls do **not** consume the C call stack. Instead, execution state is represented by heap-allocated linked structures. Dynamic nodes and frames are interleaved into a heap-allocated linked list representing execution history. From CPython's perspective, the Koskari evaluator is opaque and invisible.

- **Entry point**: The evaluator is driven by `koskari_eval()`, which is invoked when a Python caller drives a KosCoroutine (e.g. via `await` or `send`) or a KosPyAsyncGen. KosCoroutines are created by calling a KosExpression or KosFunction (their `tp_call` returns a new KosCoroutine). Calling a KosPyContinuation (undelimited continuation) from Python does not return a coroutine; it raises `KoskariContinuationApply` to perform upwards transfer via exception. There is no direct Python “run” that bypasses the coroutine wrapper. Expressions are built with `Code.expression(parent_ns)`; functions with `Code.function(formals, parent_ns)`. For the full flow, see `design/FEATURES.md` § "Creating and calling functions and expressions" and `design/interpreter/bytecode.md` § "Creating and calling code".

- **Dual perspectives**: Execution can be observed as (1) **Koskari view** (bytecode evaluating, calling Koskari functions) or (2) **Python view** (Python calling into a Koskari callable). Both are projections of a **single evaluator**, not two runtimes.

- **Single evaluator, heap-resident stack**: Koskari owns the **entire execution stack** as heap-resident frame objects. The evaluator runs in **one C call** per drive/resume. It does **not** rely on CPython’s recursive ceval for its own control flow. This allows flattened async (no C-stack growth per await). Evaluation state is stored in a heap-allocated context structure; the context stores modal state and pointers to the current dynamic-head and frame-head.

- **Jump-table dispatch**: The evaluator executes via a jump-table dispatch loop. Each opcode is implemented as a static, inlined C function. Opcode evaluation is invoked with **`KosEvalState *es`** (live mirrors for frame, code, PCs, operand stack, plus a pointer to the heap **`KosContext`**). Handlers return **`KosOpResult`** (`EVAL_NORMAL`, `EVAL_REPROLOGUE`, `EVAL_RETURN`, `EVAL_YIELD`, `EVAL_AWAIT`, `EVAL_RAISE`, …); **`cg_handle_op_result`** dispatches to **`eval_event_*`** helpers. Opcode sources in **`koskari/lib/opcode/*.c`** do **not** use **`KosContext_*`** field macros directly; they use **`KosEvalState_*`** helpers from **`eval.h`**. Dynamic-chain transitions from opcodes use **`KosEvalState_Transition`** (public wrapper around the internal dynchain dispatcher in **`dynchain.c`**).

- **Callable surface classification**: Every Koskari callable has a fixed surface to Python:
  - **Sync function** → returns a value or raises.
  - **Async function** → returns an awaitable (coroutine-like).
  - **Generator function** → returns a generator.
  - **Async generator function** → returns an async generator.
  Python-visible behavior is **entirely** determined by this classification.

- **Internal completion signals**: The VM produces internal boundary events, not raw CPython opcodes:
  - `RETURN(value)` — normal completion
  - `YIELD(value)` — generator yield
  - `AWAIT(awaitable)` — suspension on dependency
  - `RAISE(exception)` — error propagation
  These are consumed by the Koskari driver or wrapper layer.

- **AWAIT is VM suspension, not a yield**: `AWAIT` **always** suspends the whole Koskari VM immediately. No partial unwinding or nested evaluator calls. Control returns to the current boundary (Python coroutine wrapper or sync adapter).

- **Async by default**: Every Koskari function is async by default. A Koskari call creates and initializes a new evaluator context, returns control to CPython immediately, and execution proceeds via the awaitable interface. Upon resumption, evaluation continues within the same Koskari context.

- **Flattened async**: Async suspension is flattened: no recursive C calls per `await`, no rebuilding of interpreter ladders on resume. The evaluator transparently returns control to the CPython event loop whenever it encounters an awaitable during opcode evaluation (including awaitables returned from calling external Python functions or objects). Resume re-enters the evaluator once with the **entire Koskari stack intact**. Await depth affects heap frames only.

- **In-loop flattening (CALL and async gen)**: Koskari-to-Koskari calls and async generator stepping stay inside the **same** `koskari_eval` loop. (1) **CALL**: When the callable is a KosExpression or KosFunction, the evaluator pushes the callee frame onto the dynamic chain and continues dispatch in the same loop; no recursive `koskari_eval` and no Python call. (2) **Async gen (NEXT/ITER_NEXT)**: When advancing a Koskari async generator from within the loop, the evaluator marks the current frame as pending, **attaches** the generator’s frame to the current context’s chain, and continues dispatch; when the gen yields or returns, the evaluator **detaches** that frame and feeds the value (or exhausted/default) back to the awaiting frame, all within the same loop. (3) **YIELD/RETURN**: If the current frame has a **parent** frame (`frame_top->base.parent != NULL`), YIELD and RETURN do **not** produce a boundary event; they detach the current frame, push the value (or handle exhausted/exception) onto the parent, and the loop continues with the parent as `frame_top`. Only when the frame has no parent do YIELD/RETURN set a KosEvent and return from `koskari_eval`. Python-driven async gen (e.g. `__anext__` from Python) still uses a single context and `koskari_eval(agen->ctx, &ev)`; flattening applies when the consumer is already inside a Koskari eval.

- **Python awaitable interop**: Koskari may await **Python** awaitables. They are treated as opaque; Koskari does **not** recurse into CPython’s await machinery. The boundary wrapper steps them iteratively (e.g. `send(None)`).

- **Await probe (eager-step)**: When the VM hits `AWAIT(py_awaitable)`, the runtime gets the await-iterator and does **one speculative step** (`send(None)`) via **`KosEvalState_EagerStepAwaitable`** (same behavior for **`push_or_await`**, **`next_from_python_async_iter`**, and **`YIELD_FROM`** eager paths). If the result is immediate completion (`StopIteration(value)`), no suspension; value is injected back. Otherwise, suspension occurs and the iterator is stored on the context for later resumption (**`KosEvalState_PythonAwaitSetIter`** / clear on completion; **`eval.c`**).

- **Single wrapper, iterative driving**: From Python, each async Koskari call has **one** wrapper. That wrapper resumes the VM, yields Python awaitables when needed, and receives results/exceptions. There is no recursive Python coroutine chain per nested Koskari `await`.

- **Sync adapter policy**: Blocking execution is **explicit**. Sync execution uses a **dedicated event loop** created by Koskari that drives the Koskari awaitable to completion. If no Python loop is running, use a local loop in-thread; if one is running, use a helper thread with its own loop. Sync execution is never implicit.

- **Ownership and re-entrancy**: A Koskari frame/awaitable has **exactly one active driver** at a time. Concurrent or re-entrant resume is an error. Wrappers/runners enforce this.

- **Generators vs async**: Generators are **not** awaitable. Async generators are **not** awaitable. Async **functions** return awaitables. Yield and await are **disjoint** control paths.

- **Auto-await**: The VM auto-awaits only (1) the result of a Python CALL when that result is a coroutine/awaitable, and (2) the value consumed by the AWAIT opcode. All other values (CONST, loads, getattr, subscript, yield-from results, etc.) are pushed with no automatic await. **Defer objects are not eagerly awaited**: a value of type `defer` is never auto-awaited by CALL return or other implicit await sites; it is pushed as a plain value. Only the explicit AWAIT opcode drives a defer. See design/interpreter/evaluator.md § "Pushing results and auto-await" and design/api/defer-coroutine.md.

- **Yield-from**: YIELD_FROM operates on iterables and async-iterables only. Coroutines (Koskari or Python) are rejected at normalization with `TypeError: "coroutine object is not async iterable"`. Delegate results (yielded or final value) are never re-awaited; they are pushed or emitted as plain values. Koskari callables are async generators only if their code contains YIELD or YIELD_FROM; otherwise they are coroutines and invalid as yield-from targets. See [EVALUATOR.md](EVALUATOR.md) §5–8 and [OPCODES.md](OPCODES.md) (YIELD_FROM); archived detail in [archive/yield-from-execution-model.md](archive/yield-from-execution-model.md).

- **Python boundary for continuations**: Captured continuations **must not** include Python/C call stack. The dynamic chain is scoped per evaluator context (one chain per driver). Delimited capture only finds prompts in the current chain. Delimited invocation from Python is **blocked** at `DelimitedContinuation.tp_call` (always raises). Implementation: `kos_validate_capture()` verifies prompt reachability from `dyn_top`.

- **Unified dynamic chain**: All execution context (frames, prompts, winds, regions, namespace scopes, parameter extents) lives on a **single** chain of `KosDynNode` structures. Node kinds: `KOS_DYN_FRAME`, `KOS_DYN_PROMPT`, `KOS_DYN_WIND`, `KOS_DYN_TRY`, `KOS_DYN_FINALLY`, `KOS_DYN_LOOP`, `KOS_DYN_FOREACH`, `KOS_DYN_NS`, `KOS_DYN_YIELD_FROM`, `KOS_DYN_PARAM`, `KOS_DYN_WITH`, `KOS_DYN_SHIFT`, `KOS_DYN_CC`, `KOS_DYN_CC_RAISE` (see [EVALUATOR.md](EVALUATOR.md) §5, [domain.md](interpreter/domain.md)). The macro `KosDynNode_IS_REGION` is a convenience alias that matches TRY, FINALLY, LOOP, and FOREACH. Walking the chain gives full dynamic extent. `KOS_DYN_NS` (KosDynNs) marks namespace scopes from PUSH_NS; stores `saved_ns` for restore on POP_NS. `KOS_DYN_PARAM` (KosDynParam) marks parameterize extents; enter/restore domain bindings on push/pop and during continuation transfer.

- **VM-native wind/unwind**: Wind/unwind hooks are **bytecode blocks** (implemented as null-arity Koskari callables that compile to bytecode). They are invoked via `kos_callable_call` and run as normal frames. When the evaluator unwinds (return or exception), each extent is torn down via **unified extent helpers** (`kos_dyn_*_unwind`); for WIND with an after-thunk, the helper may start the thunk and return async, in which case the evaluator runs the thunk and the RETURN opcode resumes unwinding when it completes. Same helpers are used by transfer plans. See design/interpreter/extent-wind-unwind.md. Thunks execute in their own frames and capture lexical context via closure.

- **Control flow**: Koskari supports local-only jumps restricted to the current block context. Local-only jumps implement continue, break, conditional control flow, and try/except/finally. Non-local jumps across block or dynamic extents are not permitted except via continuations. JUMP locality is validated at Code construction: JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR, ITER_NEXT, and CHECK_* opcodes must target a pc inside the current extent or at its pop_pc (leaving). ITER_NEXT must be inside a PUSH_FOREACH block. PUSH_TRY, PUSH_FINALLY, PUSH_PROMPT, etc. are excluded (their handler targets may be outside the block).

- **Local jumps vs wind/unwind**: Local jumps and wind/unwind are **distinct mechanisms**: local jumps do **not** invoke wind/unwind handlers; continuation transfer **does** invoke them.

- **Continuation semantics**: Koskari implements both **delimited** continuations (reset/shift) and **undelimited** (full) continuations. Delimited continuations capture from the current point up to (but not including) the nearest matching prompt; they are restricted to the same evaluator context and may not cross the CPython C calling-convention boundary. Undelimited capture is implemented (PUSH_CC, CAPTURE_CC, POP_CC); the continuation stores a reference to its owning evaluator token (`token_ref`). Undelimited invocation from bytecode is via the CALL opcode (CALL 0 or CALL 1 with stack [callable, arg_0, ...]). If the continuation’s token matches the current context’s token (or is undelimited with no token_ref), the VM applies it directly; otherwise the VM sets KoskariContinuationApply and returns -1 so the exception propagates to the caller. Full continuations may be invoked within the same evaluator context or into an upwards context; cross-context invocation uses the specialized BaseException KoskariContinuationApply. Continuations are **multi-shot** by default. Invocation runs wind/unwind hooks in the correct order. Python and C frames are **not** embedded in the Koskari dynamic chain; only Koskari frames are re-enterable.

- **Context lifecycle**: Each evaluator context records an explicit lifecycle state (at least: active, sleeping/await/yield, exited). It is an error to apply a continuation whose owning context state does not permit invocation. Attempting to invoke an unusable continuation (delimited or full) raises an exception at the invocation site.

- **Transfer plan atomicity**: Continuation application is done via a **transfer plan**. The plan runs atomically with respect to normal VM execution; the VM does not interleave arbitrary opcode dispatch with mid-transfer state. Exceptions: (1) wind/unwind thunks may suspend on AWAIT — the plan stays armed and the same **`koskari_eval`** loop runs the thunk frame; (2) a nested continuation invoke suspends the outer plan on **`plan_stack`** while the inner plan drains, then the outer is replanned (**implemented**; see [archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md) §5).
- **Transfer in-place migration**: Transfer steps mutate live **`dyn_top`** / **`frame_top`** (unwind, INSTALL, wind, INJECT). There is no scratch dynchain swapped in at the end. Once stepping begins, the evaluator has left the invoke-site topology; after INSTALL the chain is the destination segment. On failure, exception dispatch uses the **manifested** chain (**`try_local`** after abandoning the plan nest), not a transactional rollback to the invoke site (**implemented**; see [archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md) §3–§4). Foreign cc-unwind re-emit is a signal path, not a failed-transfer path. Undelimited continuation invoke does not expect a return value at the invoke site.

- **Introspection is read-only**: The evaluator provides a read-only introspection mechanism (TraceView snapshot; see `design/interpreter/introspection-trace.md`). Introspection does not mutate evaluator state, expose raw C structs, inject execution, or interact with continuations. Snapshot objects remain valid even if the VM continues. Exception trace reporting attaches a TraceView to raised exceptions (e.g. `exc.__koskari_trace__`).

---

## 3. Data and Type Invariants

- **Atoms**: `symbol` and `keyword` are interned; same name ⇒ same object. `gensym()` produces unique symbols (optional prefix). Implemented in `koskari/lib/atom.c`.

- **Pairs**: Cons cells; `nil` is the singleton empty list and is a subtype of pair. Pairs can be proper (terminated by `nil`) or improper. Recursion detection is supported. Implemented in `koskari/lib/pair.c`.

- **Bracket-pairs**: `bracket-pair` (and the `bracket-nil` singleton) is a `pair` subtype the reader produces from `[ ]` source forms. It is value-equal to and structurally interchangeable with an ordinary pair — `[ ]` is **not** a vector or array literal — and exists only to record that the source used brackets. By default the compiler treats it exactly like a pair (a non-empty head-position list compiles to a call either way). The `( )` = call / `[ ]` = data split is a convention; the marker lets an opt-in compiler check act on it (see §10 and the `reader/brackets` / `compiler/call-brackets` pragmas). Implemented in `koskari/lib/pair.c`; gating in `koskari/compile/pragma/bracket.py` and the `compile_pair` chokepoint.

- **Namespace**: Lexical scope container; `NamespaceSpec` describes layout. `ModuleNamespace` is module-level with builtins fallback. `CollectorNamespace` blocks closure capture. Implemented in `koskari/lib/namespace.c`.

- **Refs**: Mutable reference cells. Variants: `Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`. Used for shared state in closures and by-name binding. Implemented in `koskari/lib/ref.c`.

- **Formals**: Parameter specification; `FormalsSpec` is the template; `Formals` instances are created with `apply()` for binding. Implemented in `koskari/lib/formals.c`.

- **Values**: Multi-value return object (positional and keyword). Implemented in `koskari/lib/values.c`.

- **Defer**: Single-shot await-handle; stores fn, args, kwds for application on first explicit await. Not eagerly awaited (CALL return and other implicit await sites push it as a plain value). Implemented in `koskari/lib/defer.c`; see design/api/defer-coroutine.md.

- **Stack**: `Stack` (Python) is block-structured; the **storage** for the VM is `KosStackBlock` (C). Control-flow metadata (block style, labels, enabled) lives in the region nodes (`KOS_DYN_TRY`, `KOS_DYN_FINALLY`, `KOS_DYN_LOOP`, `KOS_DYN_FOREACH`) on the dynamic chain, not on the stack. Implemented in `koskari/lib/stack.c` (storage only).

- **Bytecode**: `Code` holds immutable instructions (4 bytes each: `uint16_t op`, `uint16_t arg`), delta-encoded position table, constant pool, and extent table. The block is immutable after construction; whether it is also OS-sealed is pragma-governed: with `compiler/protect-code` (baseline off) each block is page-aligned and `mprotect(PROT_READ)`-sealed at init, and with `compiler/pack-module` (baseline on) a module's surviving code relocates into a shared, singly-sealed `Arena` at module seal (each packed `Code` holds a strong ref to its arena; the arena frees when the last member dies). Relocation is safe across suspended frames (they hold `{Code, pc}` and re-derive the buffer on resume). `Code` is not GC-tracked (its reachable graph is created-before and immutable; cycles form through `Function`, which is tracked) and is weakref-able. `KOS_CODE_YIELDS` marks code that contains yield (→ async generator). The extent table (tagged-union `KosExtentDesc` array, `top_extent_id[pc]`) is derived from PUSH/POP pairs during construction; it enables O(1) lookup of block membership and handler targets. Exception dispatch uses `kos_try_handler_for()` when the extent table is present. JUMP locality is validated: control-flow jumps (JUMP, JUMP_FALSE, etc.) may not target outside the current block; PUSH_* opcodes whose handler targets may point outside (e.g. PUSH_TRY, PUSH_PROMPT) are excluded from this restriction. Malformed bytecode (unmatched PUSH/POP) is rejected at Code construction. Implemented in `koskari/lib/bytecode.c`; opcodes in `koskari/lib/opcodes.incl`. See `design/interpreter/bytecode.md` (archived spec: `design/archive/bytecode-metadata-tables.md`).

---

## 4. Evaluator and Dynamic Chain (C Structures)

- **KosDynNode**: Single struct for all chain node kinds. Contains `parent`, `kind`, `flags`, `caps`, `node_id` (stable uint32 assigned on alloc, copied on clone for LCA matching across disjoint chains), **`stack`** (optional owned operand-stack extent; `NULL` when this node does not introduce a stack segment), and a union `u` with kind-specific payloads (frame, prompt, delim, cc, wind, try_, finally_, loop, foreach, ns, collect, yield_from, param, with_, shift, cc_raise, gen, next, …). Typedefs (KosDynFrame, KosDynPrompt, etc.) refer to the same struct. **Access node fields only via the macros** in dynchain.h (e.g. KosDynNode_KIND, KosDynNode_NODE_ID, KosDynFrame_PC, KosDynTry_HANDLER_PC, KosDynLoop_BREAK_PC); do not touch `u` or base fields directly outside the header.

- **KosDynFrame** (union variant): Function activation; `code` (borrowed), `pc`, `acting_pc`, `ns` (owned), `native_handler` (optional; when non-NULL, handler is frame evaluator). The operand stack for the active extent is **`KosDynNode.stack`** on the frame node (macro `KosDynFrame_STACK`); it is not stored inside `u.frame`. Code is not owned. Access via KosDynFrame_CODE, KosDynFrame_PC, KosDynFrame_ACTING_PC, KosDynFrame_STACK, KosDynFrame_NS, KosDynFrame_FLAGS, KosDynFrame_NATIVE_HANDLER.

- **KosDynPrompt** (union variant): Delimiter for reset; `prompt_id` for matching (0 = default). `pending_shift_wrapper` when a PUSH_SHIFT plan completed. Access via KosDynPrompt_ID, KosDynPrompt_PENDING_SHIFT_WRAPPER.

- **KosDynDelim** (union variant): Delimited invoke / POP_PROMPT return marker; mode (`KOS_DYN_DELIM_MODE_*`), return chain, PC, frame, and node metadata. See `kos_dyndelim_*` and `KosDynDelim_*` in `dynchain.h`.

- **KosDynWind** (union variant): Dynamic-wind extent; `before_thunk` and `after_thunk` (PyObject* callables or NULL); `entered` marks whether before has run. Thunks are invoked via `kos_callable_call`. Access via KosDynWind_BEFORE_THUNK, KosDynWind_AFTER_THUNK, KosDynWind_ENTERED.

- **KosDynTry / KosDynFinally / KosDynLoop / KosDynForeach** (union variants): Block-local control — formerly a single `KosDynRegion` with `KosRegionStyle`, now four separate kinds with minimal per-kind payloads. TRY and FINALLY carry `handler_pc` and `enabled`. LOOP adds `break_pc`, `continue_pc`, and `result`. FOREACH extends LOOP with `iter`, `exhausted`, and `iter_next_else_pc` for the ITER_NEXT exhausted path. `KosDynNode_IS_REGION` is a macro alias that matches all four. Access via kind-specific macros (e.g. `KosDynTry_HANDLER_PC`, `KosDynLoop_BREAK_PC`, `KosDynForeach_ITER`).

- **KosDynNs** (union variant): Namespace scope from PUSH_NS; holds `saved_ns` (prior frame namespace) for restore on POP_NS. Access via KosDynNs_SAVED_NS.

- **Allocator caches**: dynchain.c maintains a **single freelist for all KosDynNode kinds** (one allocation size); KosContinuation has its own **freelist**; eval.c maintains a **context cache** (KosContext freelist). Caps are tunable (DYN_NODE_MAX_FREE, CONT_MAX_FREE, CONTEXT_MAX_FREE). Same pattern as stack block freelist in stack.c.

- **KosEvalState**: Stack-allocated interpreter slice for one **`koskari_eval`** invocation (and for short-lived helpers that need the same API as the loop). Holds cached **`frame`**, **`code`**, **`pc`**, **`acting_pc`**, operand-stack **`block`**, and **`ctx`**; **`KosEvalState_Init` / `SyncFromContext` / `RefreshDynCaches`** keep mirrors aligned with **`KosContext`**. **`koskari_eval`** builds **`KosEvalState`**, calls **`KosEvalState_EnterDomainScope`** / **`LeaveDomainScope`** around the dispatch loop (TLS token/context stacks in **`domain.c`**). Public opcode- and eval-facing operations are named **`KosEvalState_*`** in **`eval.h`**; implementations are in **`eval.c`**, including suspension tagging, Python await/resume bridge fields, and domain scope wrappers. Continuation application arms plans with **`KosEvalState_ArmTransferPlan`**; draining uses **`KosEvalState_DisarmTransferPlan`** / **`HasActiveTransferPlan`** where appropriate.

- **KosContext**: Top-level VM heap state: `dyn_top`, `frame_top`, **`block`** (points at the innermost operand-stack `KosStackBlock *`; refreshed with the chain), `token`, **`domain`** (PyObject* KosDomain; ambient dynamic context anchor), `pending_return_value` / `pending_return_to_parent`, `plan` / `plan_stack`, `py_await_iter`, `py_resume_inject`/`py_resume_kind`, `inject_mode`/`inject_type`/`inject_value`, `suspension_event`, `flags`. Eval-layer code reads and writes **`suspension_event`** and the Python await/resume bridge through **`eval.c`** (`KosEvalState_SetSuspensionEvent`, `KosEvalState_PythonAwaitIter`, and related symbols). **`KosCoroutine`** in **`coro.c`** builds a temporary **`KosEvalState`** when it must clear or call methods on the stored await iterator. In-loop NEXT / ITER_NEXT flattening uses **`KOS_DYN_NEXT`** and **`KOS_DYN_FOREACH`** state, not context-local “pending agen” mirrors. Delimited return targets live on **`KOS_DYN_DELIM`**. FINALLY `handling_raise`/`handling_result` live on the FINALLY node, not context. Frozen async-gen install checks from **`iter.c`** / **`demand.c`** use **`kos_dyn_gen_frozen_needs_install_es`** before **`kos_dyn_gen_install`**.

- **KosOpResult**: Boundary result from `koskari_eval`: `KOS_RES_RETURN`, `KOS_RES_RAISE`, `KOS_RES_YIELD`, `KOS_RES_AWAIT`; `val` is a new reference owned by the caller when non-NULL. These are produced only when the current frame has **no parent** (top-level return/yield/raise/await); when the frame has a parent, YIELD/RETURN are handled in-loop by detaching and pushing onto the parent.

- **KosContinuation**: Captured segment: `captured_chain`, `resume_frame`, `resume_pc`, `resume_stack`, `prompt_id`, `graft_point_id` (node_id of graft point for LCA fallback when chains disjoint; 0 for capture-cc), `one_shot`, `token_ref` (PyObject * KosEvaluatorToken; ownership marker for evaluator identity). Multi-shot clones on each invoke.

- **KosTransferPlan**: Ordered list of steps: `KOS_XFER_EXTENT_ACTION`, `KOS_XFER_INSTALL`, `KOS_XFER_INJECT`, and related step kinds (`PUSH`, `POP_ONE`, `TRUNCATE_AND_INJECT`). Holds `original_target` (template segment; **clone before disarm** when replanning), `graft_point_id` (LCA fallback when chains are disjoint), `resume_pc`, `resume_frame`, `resume_stack`, `continuation_ref`, `invoke_clone`. Nested invoke: outer plan linked via `suspended` / **`plan_stack`**; after inner completion, **`kos_build_direct_install_plan`** rebuilds the outer plan from the current chain position.


**Ownership**: Context owns the dynamic chain. On continuation capture, chains are cloned for multi-shot. Only Python-visible wrappers are GC-tracked; C structs are traversed via wrapper `tp_traverse`.

---

## 5. Python-Facing Wrappers

- **KosPyContinuation** (cont.c): Wraps undelimited `KosContinuation` (call/cc). Callable from Python: does not return; raises `KoskariContinuationApply` to perform upwards transfer via exception. Multi-shot: each apply clones captured state.

- **KosCoroutine** (coro.c / coro.h): Wraps `KosContext` for the coroutine protocol (`__await__`, `send`, `throw`, `close`). States: NEW, RUNNING, WAITING_PY, YIELDED, DONE. Created by `KosCoroutine_NewFromCode()` (Expression/Function call). Driving the coroutine (send/await) calls `koskari_eval()`. (Continuation invocation from Python raises; it does not create a KosCoroutine.) While **`WAITING_PY`**, **`send` / `throw` / `close`** use **`KosEvalState_Init(&coro_es, coro->ctx)`** and the same **`KosEvalState_Python*`** helpers as **`revive_await`** so behavior stays consistent with in-eval await storage. **Sync entry**: `demand()` drives the coroutine to completion on the demand path (no asyncio); raises if suspension requires a Python awaitable.

- **KosPyAsyncGen** / **KosPyAsyncGenASend** (iter.c / iter.h): Async generator protocol; `__anext__` returns an awaitable that drives until yield or completion. **Sync iteration**: `tp_iter` returns a **DemandGenerator** (`KosDemandGenerator_FromAsyncGen`), not asyncio.

---

## 6. Bytecode and Opcodes

- **Instruction**: 4 bytes = `KosOp { op, arg }` (both uint16_t). The exact bytecode encoding is not finalized; current aim is a fixed-width encoding of (uint16 op, uint16 arg).
- **Position**: Stored in a separate delta-encoded table; only changes are recorded; lookup by instruction index is binary search.
- **Opcodes**: Defined in `opcodes.incl` with `OP(name, style)`. Styles: COMPILE, LABEL, NOARG, COUNT, PAIR_COUNT, STAR, CONST, VAR, FASTVAR, EXTRA. Categories include stack, control flow, jumps, variables (VAR / FASTVAR), attributes, items, calls (including tail calls), block control, delimited continuations (PUSH_PROMPT, POP_PROMPT, PUSH_SHIFT, POP_SHIFT), dynamic wind (PUSH_WIND, POP_WIND), continuation invocation (INVOKE_K), object protocol, iteration, introspection, exceptions. Runtime handlers are **`KOS_OP_HANDLER(name)`** fragments included from **`eval.c`**; each implements **`KosOpResult op_name(KosEvalState *es, KosOp *insn)`** (via **`EVAL_*`** return macros).
- **Implementation status**: See [OPCODES.md](OPCODES.md) §1.6. That document is the authoritative reference for which opcodes are implemented, stubs, or have no runtime.

**OPEN** (not settled): Full opcode set and per-opcode semantics; bytecode word size and encoding details beyond tentative format; full opcode inventory and formal operand conventions; precise opcode-level semantics for local-only jumps; exact taxonomy of continuation-related exceptions.

---

## 7. Continuation and Wind Semantics

- **Capture (shift)**: Find prompt by `prompt_id`, validate prompt reachable in chain, clone chain segment, build `KosContinuation`, abort to prompt (handler runs if present).
- **Invoke**: Clone if multi-shot, build transfer plan (unwind hooks → install segment → before hooks → inject value/throw), run plan. Nested invocation **suspends** the current plan onto **`plan_stack`**, runs the inner plan to completion, then **replans** the outer via **`kos_build_direct_install_plan`** (implemented). Exception during a transfer step: abandon nest and dispatch on manifested chain via **`try_local`** (implemented); cc-unwind sentinel re-emit propagates **`KoskariContinuationApply`** upward without **`try_local`**.
- **Wind order on transfer**: Unwind: run **after** hooks from current toward LCA (inner to outer). Install: splice target chain. Wind: run **before** hooks from LCA toward target (outer to inner), set `entered` on each.

---

## 8. Language and Syntax (Design)

- **Syntax**: S-expressions; lists and improper lists; atoms (symbols, keywords, numbers, strings, etc.). Reader stream types: `BaseSourceStream` (C, hot-path I/O), `SourceStream` (Python, error reporting and pushback), `IndentAwareStream` (indent-bounded whitespace skip for layout blocks). Reader classes: `Reader` (plain s-expressions), `LayoutReader` (indent-aware block syntax; requires `IndentAwareStream`); reader macros (e.g. `'`, `` ` ``, `,`, `,@`). See `design/language/syntax.md`.
- **Special forms**: Compile to bytecode; no runtime form. Core: quote, quasiquote, begin, lambda, function, let, define, setq, delq, refq; control: cond, while, for-each, try, return, break, continue; context: with, await-with; generators/async: yield, yield-from, await, declare-async; attributes, values, import, doc. See `design/language/special-forms.md`.
- **Formals**: Positional, defaults, keyword-only, `*args`, `**kwargs`; syntax uses `*` and `:`. Multi-value binding with `define-values` / `setq-values` and optional `*` rest.

---

## 9. Implementation Status (What Exists Today)

**Fully implemented and tested (C + Python)**:

- Atoms: symbol, keyword, gensym (atom.c).
- Pairs: pair, nil, cons, car, cdr, setcar, setcdr, build_unpack_pair, predicates (pair.c).
- Namespace: Namespace, NamespaceSpec, ModuleNamespace, CollectorNamespace, Module (namespace.c).
- Refs: all six ref types (ref.c).
- Formals: Formals, FormalsSpec (formals.c).
- Values (values.c).
- Stack: block-structured storage (stack.c); region metadata in DynRegion.
- Bytecode: Code, 4-byte instructions, position table, constant pool, pragma-governed sealing and arena packing (bytecode.c, arena.c).
- Parser/reader: BaseSourceStream (parse.c), SourceStream, IndentAwareStream, Reader, LayoutReader (koskari/reader/parser.py).
- Dynamic chain: all DynNode types (TRY, FINALLY, LOOP, FOREACH, WITH, DYN_WITH, SHIFT, and others), push/pop, clone, free, `kos_find_node_for_loop` (`dynchain.c` shell and `dynchain/dyn_*.c`; walks in `dynchain/dyn_walk.c`).
- Continuation: capture (`kos_continuation_capture_segment`), validate, clone, invoke, transfer plan, LCA (`dynchain.c`, `dynchain/xfer_plan.c`, `eval.c`, `eval/eval_chain.c`).
- Wind: push/pop wind, hook runner, begin/hook_active/end_hook (`dynchain/dyn_wind.c`, `eval.c`).
- Evaluator core: **KosEvalState** (eval.h), **KosContext** (with domain field), KosFrame, KosContext_New/free, kos_frame_new/clone/free (eval.c). **eval.c** also implements suspension-event and Python await/resume accessors; **KosEvalState_EnterDomainScope** / **LeaveDomainScope** (forward to **kos_domain_push_scope** / **pop_scope** on **`KosEvalState_CONTEXT(es)`**). Domain: KosDomain type (param value store: name -> value), ParamRef (Ref subclass; resolves via kos_domain_current and kos_domain_get_param), TLS (KosTlsState, ctx_stack, token_stack), kos_ctx_current, kos_domain_current, kos_domain_push_scope, kos_domain_pop_scope (domain.c).
- Drive loop: `koskari_eval()` builds **KosEvalState**, enters domain scope, and runs opcode fetch, dispatch table, and boundary handling. **CALL** (stack [callable, arg_0, ..., arg_{N-1}]): for Continuation/DelimitedContinuation, same-token invoke applies directly, different-token sets KoskariContinuationApply and returns -1; for KosExpression/KosFunction pushes the callee frame and continues in the same loop (flattened); for other callables goes through Python (push_or_await result). **NEXT/ITER_NEXT** for a Koskari async gen attach the gen’s frame to the current context and continue in the same loop until the gen yields or completes (flattened). **YIELD/RETURN** with a parent frame detach and push value onto the parent, then continue in the same loop; only with no parent do they set a boundary event. Opcode implementation status: see [OPCODES.md](OPCODES.md) §1.6.
- Python wrappers: KosPyContinuation (cont.c); KosCoroutine and `KosCoroutine_NewFromCode` (coro.c); KosPyAsyncGen, KosPyAsyncGenASend (iter.c). KosCoroutine drive (send/throw/close) in coro.c; async-gen drive and eager-step await in eval.c / iter.c. Sync: `Coroutine.demand()` (coro.c); `tp_iter` → DemandGenerator (iter.c / demand.c).
- GC: traverse helpers for context, frame, stack block, chain, continuation, transfer plan, hook runner (eval.c, dynchain.c).
- Operators: reducing arithmetic, bitwise, membership, build-str; apply, reapply, identity, constantly, _pass; build_tuple, build_list, build_set, build_dict (operator.c, lib.c).

**Not yet implemented**:

- Remaining opcode handlers: see [OPCODES.md](OPCODES.md) §1.6 for stub opcodes.
- Compiler: `koskari.compile` contains a substantially complete compiler (Compiler, ByteCodeFragment, specials package covering begin, bytecode, call, conditionals, continuations, context/with/parameterize, exceptions/try, items, lambda, let, loops, object protocol, pairs/quote/quasiquote, slots/attrs, variables, imports). The full end-to-end pipeline (macro expansion, optimization passes) is not yet complete. Module loading integration is implemented; see `design/api/import-system.md`.
- Traceback integration with Koskari frames.

---

## 10. Clarifications and Common Pitfalls

- **call/cc vs reset/shift**: Design supports both **undelimited** (full) call/cc and **delimited** reset/shift. Delimited capture and invocation **cannot** cross the Python boundary (chain is per-context; `DelimitedContinuation.tp_call` blocks invocation from Python); full continuations **may** cross the CPython boundary in the upwards direction only, implemented by raising a specialized BaseException. See §2 continuation semantics.

- **All code is async**: By design, Koskari code is async by default. The `KOS_CODE_YIELDS` flag distinguishes “returns coroutine” vs “returns async generator,” not sync vs async.

- **`[ ]` is not a vector literal**: A common mistake is to read `[ ]` as "a literal list/array" distinct from `( )`. It is not. Both delimiters read into `pair` chains and **compile the same way by default** — a non-empty list in head position is a *call*. `[a 1]` is a call to `a`, just like `(a 1)`. Brackets carry only an origin marker (`bracket-pair`), and the `( )` = call / `[ ]` = data distinction is a **convention** that is inert unless the `compiler/call-brackets` pragma is set to `warn` / `error`. So `(let [[a 1]] …)` works because `let` consumes its binding list as data, not because `[ ]` means something different to the reader or evaluator.

- **Frame vs KosDynFrame**: `KosFrame` is a typedef for `KosDynFrame`; they are the same. The “frame” is the topmost FRAME node on the dynamic chain; `ctx->frame_top` caches it, and **`KosEvalState_FRAME(es)`** mirrors it while **`RefreshDynCaches`** / **`SyncFromContext`** are used correctly.

- **KosEvalState vs KosContext**: **`KosContext`** is the persistent heap object; **`KosEvalState`** is the per-invocation working view. Opcodes and most **`eval.c`** helpers take **`es`**; **`dynchain`** entry points that mutate the chain take **`KosEvalState *es`** first and reach the context with **`KosEvalState_CONTEXT(es)`**. Do not add new **`KosContext_*`** field touches in **`opcode/*.c`**; extend **`eval.h`** / **`eval.c`** instead.

- **Stack “block”**: In C, the evaluator uses `KosStackBlock` (linked blocks of value arrays). The Python `Stack` type is the block-structured value stack API; control-flow metadata is in the region nodes on the dynamic chain (TRY, FINALLY, LOOP, FOREACH), not in the stack.

- **Borrowed vs owned**: In C, “borrowed” means the frame/code does not take ownership (no DECREF); “owned” or “stolen” means the receiver is responsible for DECREF or free. Docstrings in dynchain.h and eval.h specify per field.

- **Tox**: This repo uses **tox** for environments and tests. Run Python via `tox -e <env>` (e.g. `tox -e py312`), not raw `python` or `python3`, per project rules.

---

## 11. Where to Look in the Repo

| Topic              | Design doc(s)                    | Implementation                          |
|-------------------|-----------------------------------|-----------------------------------------|
| Execution model   | interpreter/execution-invariants.md, EVALUATOR.md §1, archive/kos_eval_state_api_migration.md | koskari/lib/eval.c, eval.h |
| Domain (TLS, KosDomain) | interpreter/domain.md      | koskari/lib/domain.c, domain.h          |
| Coroutine / run-code | (this doc, evaluator.md)       | koskari/lib/coro.c, coro.h              |
| Async generator   | (this doc, evaluator.md)         | koskari/lib/iter.c, iter.h              |
| Demand path       | (this doc)                       | koskari/lib/demand.c, demand.h; `as_coroutine` in koskari/types.py |
| Dynamic chain     | interpreter/evaluator.md          | koskari/lib/dynchain.c, dynchain.h, dynchain/dyn_*.c, dynchain/dyn_walk.c |
| Continuations     | interpreter/callcc.md, archive/interrupted-transfer-plans.md | dynchain.c, dynchain/xfer_plan.c, eval.c, eval/eval_chain.c |
| Bytecode          | interpreter/bytecode.md, archive/bytecode-metadata-tables.md | koskari/lib/bytecode.c, bytecode.h, opcodes.incl |
| Types             | api/types.md                     | koskari/lib/*.c, koskari/types.py       |
| Builtins/API      | api/builtins.md, api/runtime.md   | koskari/lib/lib.c, operator.c           |
| Language / Reader | language/*.md, SYNTAX.md, LAYOUTS.md | koskari/reader/parser.py, koskari/lib/parse.c |
| Compiler          | SPECIALS.md, OPCODES.md, interpreter/README.md (Compiler) | koskari/compile/                  |
| Import system     | api/import-system.md             | koskari/importer/                 |
| Feature list      | FEATURES.md, README.md           | —                                       |

---

## 12. Non-Goals (Explicit Exclusions)

- No implicit event loop creation on normal calls.
- No reliance on CPython frame recursion for Koskari semantics.
- No unification of yield and await signals.
- No Python-callable wind/unwind hooks (VM bytecode only).
- No compiling to CPython bytecode (custom bytecode only).

---

*This file is the single “invariants and clarifications” reference for teaching another system about Koskari’s current design and implementation. For full detail, use the linked design docs and source files.*
