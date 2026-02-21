# Koskari: Invariants and Clarifications

This document is intended for teaching another system (human or agent) about the current state of Koskari: what is designed, what is implemented, and what invariants and clarifications hold across the codebase and design docs. Use it as a single reference for "how things work" and "what must stay true."

**Scope**: Agent/human teaching master reference — covers execution invariants, type and data invariants, C structures, implementation status, and clarifications in one place. For a focused numbered invariant list, see [interpreter/execution-invariants.md](interpreter/execution-invariants.md). For the full VM API reference, see [interpreter/evaluator.md](interpreter/evaluator.md).

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

- **Entry point**: The evaluator is driven by `koskari_eval()`, which is invoked when a Python caller drives a KosCoroutine (e.g. via `await` or `send`) or a KosPyAsyncGen. KosCoroutines are created by calling a KosExpression or KosFunction (their `tp_call` returns a new KosCoroutine) or by calling a KosPyContinuation (which also returns a KosCoroutine). There is no direct Python “run” that bypasses the coroutine wrapper. Expressions are built with `Code.expression(parent_ns)`; functions with `Code.function(formals, parent_ns)`. For the full flow, see `design/FEATURES.md` § "Creating and calling functions and expressions" and `design/interpreter/bytecode.md` § "Creating and calling code".

- **Dual perspectives**: Execution can be observed as (1) **Koskari view** (bytecode evaluating, calling Koskari functions) or (2) **Python view** (Python calling into a Koskari callable). Both are projections of a **single evaluator**, not two runtimes.

- **Single evaluator, heap-resident stack**: Koskari owns the **entire execution stack** as heap-resident frame objects. The evaluator runs in **one C call** per drive/resume. It does **not** rely on CPython’s recursive ceval for its own control flow. This allows flattened async (no C-stack growth per await). Evaluation state is stored in a heap-allocated context structure; the context stores modal state and pointers to the current dynamic-head and frame-head.

- **Jump-table dispatch**: The evaluator executes via a jump-table dispatch loop. Each opcode is implemented as a static, inlined C function. Opcode evaluation is invoked with the active context and returns a (mode, value) pair, which the evaluator loop interprets to determine the next control action.

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

- **Await probe (eager-step)**: When the VM hits `AWAIT(py_awaitable)`, the runtime gets the await-iterator and does **one speculative step** (`send(None)`). If the result is immediate completion (`StopIteration(value)`), no suspension; value is injected back. Otherwise, suspension occurs and the iterator is stored for later resumption.

- **Single wrapper, iterative driving**: From Python, each async Koskari call has **one** wrapper. That wrapper resumes the VM, yields Python awaitables when needed, and receives results/exceptions. There is no recursive Python coroutine chain per nested Koskari `await`.

- **Sync adapter policy**: Blocking execution is **explicit**. Sync execution uses a **dedicated event loop** created by Koskari that drives the Koskari awaitable to completion. If no Python loop is running, use a local loop in-thread; if one is running, use a helper thread with its own loop. Sync execution is never implicit.

- **Ownership and re-entrancy**: A Koskari frame/awaitable has **exactly one active driver** at a time. Concurrent or re-entrant resume is an error. Wrappers/runners enforce this.

- **Generators vs async**: Generators are **not** awaitable. Async generators are **not** awaitable. Async **functions** return awaitables. Yield and await are **disjoint** control paths.

- **Auto-await**: The VM auto-awaits only (1) the result of a Python CALL when that result is a coroutine/awaitable, and (2) the value consumed by the AWAIT opcode. All other values (CONST, loads, getattr, subscript, yield-from results, etc.) are pushed with no automatic await. See design/interpreter/evaluator.md § "Pushing results and auto-await".

- **Yield-from**: YIELD_FROM operates on iterables and async-iterables only. Coroutines (Koskari or Python) are rejected at normalization with `TypeError: "coroutine object is not async iterable"`. Delegate results (yielded or final value) are never re-awaited; they are pushed or emitted as plain values. Koskari callables are async generators only if their code contains YIELD or YIELD_FROM; otherwise they are coroutines and invalid as yield-from targets. See design/interpreter/yield-from-execution-model.md.

- **Python boundary for continuations**: Captured continuations **must not** include Python/C call stack. The dynamic chain marks Python boundaries with `KOS_DYN_PYCALL`. `shift` capture is **rejected** if any such node exists between current position and the target prompt. Implementation: `kos_validate_capture()` walks from `dyn_top` to the prompt.

- **Unified dynamic chain**: All execution context (frames, prompts, winds, regions, namespace scopes, parameter extents) lives on a **single** chain of `KosDynNode` structures. Node kinds: `KOS_DYN_FRAME`, `KOS_DYN_PROMPT`, `KOS_DYN_WIND`, `KOS_DYN_REGION`, `KOS_DYN_NS`, `KOS_DYN_PYCALL`, `KOS_DYN_YIELD_FROM`, `KOS_DYN_PARAM` (see design/interpreter/yield-from-execution-model.md, design/interpreter/domain.md). Walking the chain gives full dynamic extent. `KOS_DYN_NS` (KosDynNs) marks namespace scopes from PUSH_NS; stores `saved_ns` for restore on POP_NS. `KOS_DYN_PARAM` (KosDynParam) marks parameterize extents; enter/restore domain bindings on push/pop and during continuation transfer.

- **VM-native wind/unwind**: Wind/unwind hooks are **bytecode blocks** (implemented as null-arity Koskari callables that compile to bytecode). They run in the VM via `KosHookRunner` and end with the `END_WIND` sentinel opcode. When the evaluator unwinds (return or exception), each extent is torn down via **unified extent helpers** (`kos_dyn_*_unwind`); for WIND with an after-hook, the helper may start the hook and return async, in which case the evaluator runs the hook then resumes unwinding from END_WIND (unwind-in-progress state). Same helpers are used by transfer plans. See design/interpreter/extent-wind-unwind.md. Hook blocks execute in their own frames and capture lexical context via closure; they cannot see or change the enclosing frame’s stack.

- **Control flow**: Koskari supports local-only jumps restricted to the current block context. Local-only jumps implement continue, break, conditional control flow, and try/except/finally. Non-local jumps across block or dynamic extents are not permitted except via continuations. JUMP locality is validated at Code construction: JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR, ITER_NEXT, and CHECK_* opcodes must target a pc inside the current extent or at its pop_pc (leaving). ITER_NEXT must be inside a PUSH_FOREACH block. PUSH_TRY, PUSH_FINALLY, SET_PROMPT_HANDLER, etc. are excluded (their handler targets may be outside the block).

- **Local jumps vs wind/unwind**: Local jumps and wind/unwind are **distinct mechanisms**: local jumps do **not** invoke wind/unwind handlers; continuation transfer **does** invoke them.

- **Continuation semantics**: Koskari implements both **delimited** continuations (reset/shift) and **undelimited** (full) continuations. Delimited continuations capture from the current point up to (but not including) the nearest matching prompt; they are restricted to the same evaluator context and may not cross the CPython C calling-convention boundary. Undelimited capture is implemented (PUSH_CC, CAPTURE_CC, POP_CC); the continuation stores a reference to its owning evaluator token (`token_ref`). Undelimited invocation from bytecode is via the CALL opcode (CALL 0 or CALL 1 with stack [callable, arg_0, ...]). If the continuation’s token matches the current context’s token (or is undelimited with no token_ref), the VM applies it directly; otherwise the VM sets KoskariContinuationApply and returns -1 so the exception propagates to the caller. Full continuations may be invoked within the same evaluator context or into an upwards context; cross-context invocation uses the specialized BaseException KoskariContinuationApply. Continuations are **multi-shot** by default. Invocation runs wind/unwind hooks in the correct order. Python and C frames are **not** embedded in the Koskari dynamic chain; only Koskari frames are re-enterable.

- **Context lifecycle**: Each evaluator context records an explicit lifecycle state (at least: active, sleeping/await/yield, exited). It is an error to apply a continuation whose owning context state does not permit invocation. Attempting to invoke an unusable continuation (delimited or full) raises an exception at the invocation site.

- **Transfer plan atomicity**: Continuation application is done via a **transfer plan**. The plan runs atomically with respect to normal VM execution; the VM does not interleave opcode dispatch with mid-transfer state. Exception: hooks may suspend on AWAIT, in which case the transfer plan is suspended with the context.

- **Chain version**: The context keeps a `chain_version` counter, incremented on every chain push/pop. Transfer plans record the version at creation. Nested transfers that change the chain can be detected by version mismatch for safe resumption.

- **Introspection is read-only**: The evaluator provides a read-only introspection mechanism (TraceView snapshot; see `design/interpreter/introspection-trace.md`). Introspection does not mutate evaluator state, expose raw C structs, inject execution, or interact with continuations. Snapshot objects remain valid even if the VM continues. Exception trace reporting attaches a TraceView to raised exceptions (e.g. `exc.__koskari_trace__`).

---

## 3. Data and Type Invariants

- **Atoms**: `symbol` and `keyword` are interned; same name ⇒ same object. `gensym()` produces unique symbols (optional prefix). Implemented in `koskari/lib/atom.c`.

- **Pairs**: Cons cells; `nil` is the singleton empty list and is a subtype of pair. Pairs can be proper (terminated by `nil`) or improper. Recursion detection is supported. Implemented in `koskari/lib/pair.c`.

- **Namespace**: Lexical scope container; `NamespaceSpec` describes layout. `ModuleNamespace` is module-level with builtins fallback. `CollectorNamespace` blocks closure capture. Implemented in `koskari/lib/namespace.c`.

- **Refs**: Mutable reference cells. Variants: `Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`. Used for shared state in closures and by-name binding. Implemented in `koskari/lib/ref.c`.

- **Formals**: Parameter specification; `FormalsSpec` is the template; `Formals` instances are created with `apply()` for binding. Implemented in `koskari/lib/formals.c`.

- **Values**: Multi-value return object (positional and keyword). Implemented in `koskari/lib/values.c`.

- **Stack**: `Stack` (Python) is block-structured; the **storage** for the VM is `KosStackBlock` (C). Control-flow metadata (block style, labels, enabled) lives in **`KosDynRegion`** nodes on the dynamic chain, not on the stack. Implemented in `koskari/lib/stack.c` (storage only).

- **Bytecode**: `Code` holds immutable instructions (4 bytes each: `uint16_t op`, `uint16_t arg`), delta-encoded position table, constant pool, and extent table. After init, code, pool, and extent table are made read-only with `mprotect()`. `KOS_CODE_YIELDS` marks code that contains yield (→ async generator). The extent table (tagged-union `KosExtentDesc` array, `top_extent_id[pc]`) is derived from PUSH/POP pairs during construction; it enables O(1) lookup of block membership and handler targets. Exception dispatch uses `kos_try_handler_for()` when the extent table is present. JUMP locality is validated: control-flow jumps (JUMP, JUMP_FALSE, etc.) may not target outside the current block; PUSH/SET_PROMPT_HANDLER handler targets may point outside. Malformed bytecode (unmatched PUSH/POP) is rejected at Code construction. Implemented in `koskari/lib/bytecode.c`; opcodes in `koskari/lib/opcodes.incl`. See `design/interpreter/bytecode-metadata-tables.md`.

---

## 4. Evaluator and Dynamic Chain (C Structures)

- **KosDynNode**: Single struct for all chain node kinds. Contains `parent`, `kind`, `flags`, `node_id` (stable uint32 assigned on alloc, copied on clone for LCA matching across disjoint chains) and a union `u` with kind-specific payloads (frame, prompt, cc, wind, region, ns, pycall). Typedefs (KosDynFrame, KosDynPrompt, etc.) refer to the same struct. **Access node fields only via the macros** in dynchain.h (e.g. KosDynNode_KIND, KosDynNode_NODE_ID, KosDynFrame_PC, KosDynRegion_STYLE); do not touch `u` or base fields directly outside the header.

- **KosDynFrame** (union variant): Function activation; `code` (borrowed), `pc`, `stack` (owned), `ns` (owned). Frame owns stack and ns; code is not owned. Access via KosDynFrame_CODE, KosDynFrame_PC, KosDynFrame_STACK, KosDynFrame_NS, KosDynFrame_FLAGS.

- **KosDynPrompt** (union variant): Delimiter for reset; `prompt_id` for matching (0 = default). Optional `handler_code`/`handler_pc` for the handler run after shift aborts. Access via KosDynPrompt_ID, KosDynPrompt_HANDLER_CODE, KosDynPrompt_HANDLER_PC.

- **KosDynWind** (union variant): Dynamic-wind extent; before/after code and pc; `entered` marks whether before has run. Hooks are bytecode blocks, not Python. Access via KosDynWind_BEFORE_CODE, KosDynWind_BEFORE_PC, KosDynWind_AFTER_CODE, KosDynWind_AFTER_PC, KosDynWind_ENTERED.

- **KosDynRegion** (union variant): Block-local control; `KosRegionStyle` (TRY, FINALLY, LOOP, WITH, FOREACH, etc.). `break_pc`, `continue_pc` are jump targets; `enabled` for exception handling. FOREACH regions have `iter_next_else_pc` for ITER_NEXT exhausted path. Access via KosDynRegion_STYLE, KosDynRegion_BREAK_PC, KosDynRegion_CONTINUE_PC, KosDynRegion_ENABLED, KosDynRegion_ITER_NEXT_ELSE_PC, and related macros.

- **KosDynNs** (union variant): Namespace scope from PUSH_NS; holds `saved_ns` (prior frame namespace) for restore on POP_NS. Access via KosDynNs_SAVED_NS.

- **Allocator caches**: dynchain.c maintains a **single freelist for all KosDynNode kinds** (one allocation size); KosContinuation has its own **freelist**; eval.c maintains a **context cache** (KosContext freelist). Caps are tunable (DYN_NODE_MAX_FREE, CONT_MAX_FREE, CONTEXT_MAX_FREE). Same pattern as stack block freelist in stack.c.

- **KosContext**: Top-level VM state: `dyn_top`, `frame_top`, `globals_frame`, `globals_cache_version`, `token`, **`domain`** (PyObject* KosDomain; ambient dynamic context anchor), `hook_runner`, `plan` / `plan_stack`, `chain_version`, `invocation_segment_top`, `py_await_iter`, `py_resume_inject`/`py_resume_kind`, `inject_mode`/`inject_value`, **pending async gen** (`pending_agen`, `pending_agen_default`, `pending_agen_n` for in-loop NEXT/ITER_NEXT; ITER_NEXT exhausted path reads region and else_pc from FOREACH region via kos_find_region_for_loop), `flags`. FINALLY `handling_raise`/`handling_result` live on `KosDynRegion`, not context.

- **KosEvent**: Boundary event from `koskari_eval`: `KOS_EV_RETURN`, `KOS_EV_RAISE`, `KOS_EV_YIELD`, `KOS_EV_AWAIT`; `payload` is a new reference owned by the caller. These are produced only when the current frame has **no parent** (top-level return/yield/raise/await); when the frame has a parent, YIELD/RETURN are handled in-loop by detaching and pushing onto the parent.

- **KosContinuation**: Captured segment: `captured_chain`, `resume_frame`, `resume_pc`, `resume_stack`, `prompt_id`, `graft_point_id` (node_id of graft point for LCA fallback when chains disjoint; 0 for capture-cc), `one_shot`, `token_ref` (PyObject * KosEvaluatorToken; ownership marker for evaluator identity). Multi-shot clones on each invoke.

- **KosTransferPlan**: Ordered list of steps: `KOS_XFER_EXTENT_ACTION`, `KOS_XFER_INSTALL`, `KOS_XFER_INJECT`. Tracks `chain_version` and `original_target` for replanning after nested transfers. For invocation: `resume_pc`, `resume_frame`, `resume_stack`, `continuation_ref`.

- **KosHookRunner**: Runs one hook block: `code`, `pc`, `scratch` stack, `active` flag.

**Ownership**: Context owns the dynamic chain. On continuation capture, chains are cloned for multi-shot. Only Python-visible wrappers are GC-tracked; C structs are traversed via wrapper `tp_traverse`.

---

## 5. Python-Facing Wrappers

- **KosPyContinuation** (eval.c): Wraps `KosContinuation`. Callable: calling it returns a `KosCoroutine` that resumes with the argument. Multi-shot: each call clones captured state.

- **KosCoroutine** (func.c): Wraps `KosContext` for the coroutine protocol (`__await__`, `send`, `throw`, `close`). States: NEW, RUNNING, WAITING_PY, DONE. Created by `kos_run_code_as_coroutine()` (Expression/Function call) or `KosCoroutine_New()` (Continuation call). Driving the coroutine (send/await) calls `koskari_eval()`. **Sync adapter**: `run()` runs the coroutine via `asyncio.run(self)`; for tests and sync scripts only; must not be used when an event loop is already running (deadlock risk).

- **KosPyAsyncGen** / **KosPyAsyncGenASend** (eval.c): Async generator protocol; `__anext__` returns an awaitable that drives until yield or completion. **Sync adapter**: `iter()` returns a synchronous iterator that drives the gen via `asyncio.run()` on each `__anext__`; for tests and sync scripts only; must not be used when an event loop is already running (deadlock risk).

---

## 6. Bytecode and Opcodes

- **Instruction**: 4 bytes = `KosOp { op, arg }` (both uint16_t). The exact bytecode encoding is not finalized; current aim is a fixed-width encoding of (uint16 op, uint16 arg).
- **Position**: Stored in a separate delta-encoded table; only changes are recorded; lookup by instruction index is binary search.
- **Opcodes**: Defined in `opcodes.incl` with `OP(name, style)`. Styles: COMPILE, LABEL, NOARG, COUNT, PAIR_COUNT, STAR, CONST, VAR, FASTVAR. Categories include stack, control flow, jumps, variables (VAR / FASTVAR), attributes, items, calls (including tail/recur), block control, delimited continuations (PUSH_PROMPT, SET_PROMPT_HANDLER, POP_PROMPT, SHIFT), dynamic wind (PUSH_WIND, POP_WIND, END_WIND), continuation invocation (INVOKE_K), region control, object protocol, iteration, introspection, exceptions.
- **Implementation status**: Each opcode has a handler in `eval.c`; handlers are either fully implemented, stubs (`op_unimplemented_stub`), or a dedicated "no runtime implementation" handler (THEEND: hardcoded error, not a stub). The list of **fully implemented**, **no runtime implementation** (THEEND), and **unimplemented (stub)** opcodes is documented in `design/FEATURES.md` (§ "Opcodes: implementation status") and summarized in `design/interpreter/bytecode.md` (§ "Opcode implementation status"). There are no partially implemented opcodes.

**OPEN** (not settled): Full opcode set and per-opcode semantics; bytecode word size and encoding details beyond tentative format; full opcode inventory and formal operand conventions; precise opcode-level semantics for local-only jumps; exact taxonomy of continuation-related exceptions.

---

## 7. Continuation and Wind Semantics

- **Capture (shift)**: Find prompt by `prompt_id`, validate no PYCALL between current and prompt, clone chain segment, build `KosContinuation`, abort to prompt (handler runs if present).
- **Invoke**: Clone if multi-shot, build transfer plan (after hooks → install segment → before hooks → inject value/throw), run plan. Nested invocation suspends current plan and pushes it onto `plan_stack`.
- **Wind order on transfer**: Unwind: run **after** hooks from current toward LCA (inner to outer). Install: splice target chain. Wind: run **before** hooks from LCA toward target (outer to inner), set `entered` on each.

---

## 8. Language and Syntax (Design)

- **Syntax**: S-expressions; lists and improper lists; atoms (symbols, keywords, numbers, strings, etc.). Reader: `Reader`, `SourceStream`; reader macros (e.g. `'`, `` ` ``, `,`, `,@`). See `design/language/syntax.md`.
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
- Bytecode: Code, 4-byte instructions, position table, constant pool, read-only protection (bytecode.c).
- Parser/reader: BaseSourceStream, SourceStream, Reader (parse.c, parse.py).
- Dynamic chain: all DynNode types, push/pop, clone, free, kos_find_region_for_raise (dynchain.c).
- Continuation: capture, validate, clone, invoke, transfer plan, LCA (dynchain.c, eval.c).
- Wind: push/pop wind, hook runner, begin/hook_active/end_hook (dynchain.c, eval.c).
- Evaluator core: KosContext (with domain field), KosFrame, kos_context_new/free, kos_frame_new/clone/free, kos_koscall, kos_tail_koscall (eval.c). Domain: KosDomain type (param value store: name -> value), ParamRef (Ref subclass; resolves via kos_domain_current and kos_domain_get_param), TLS (KosTlsState, ctx_stack, token_stack), kos_ctx_current, kos_domain_current, kos_domain_push_scope, kos_domain_pop_scope (domain.c).
- Drive loop: `koskari_eval()` is implemented with opcode fetch, dispatch table, and boundary handling. **CALL** (stack [callable, arg_0, ..., arg_{N-1}]): for Continuation/DelimitedContinuation, same-token invoke applies directly, different-token sets KoskariContinuationApply and returns -1; for KosExpression/KosFunction pushes the callee frame and continues in the same loop (flattened); for other callables goes through Python (push_or_await result). **NEXT/ITER_NEXT** for a Koskari async gen attach the gen’s frame to the current context and continue in the same loop until the gen yields or completes (flattened). **YIELD/RETURN** with a parent frame detach and push value onto the parent, then continue in the same loop; only with no parent do they set a boundary event. Fully implemented opcodes include stack, constants, RETURN/AWAIT, YIELD, YIELD_FROM, CALL (as above), variables (VAR, VAR_BIND, VAR_CLEAR, VAR_REF, VAR_STORE), fast locals (FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE), jumps (JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR), check jumps (CHECK_EQ, CHECK_NE, CHECK_LT, CHECK_LE, CHECK_GT, CHECK_GE, CHECK_IN, CHECK_NOT_IN, CHECK_IS, CHECK_IS_NOT, CHECK_IS_INSTANCE, CHECK_IS_SUBCLASS), attributes and items (ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE; ITEM, ITEM_CLEAR, ITEM_REF, ITEM_STORE; CONST_ITEM, CONST_ITEM_CLEAR, CONST_ITEM_REF, CONST_ITEM_STORE), iteration (ITER_NEXT, NEXT; transparent async, Koskari async gen in-loop attach/detach), namespace scope (PUSH_NS, POP_NS), exceptions and try/finally, delimited continuations (PUSH_PROMPT, SET_PROMPT_HANDLER, POP_PROMPT, SHIFT), undelimited continuations (PUSH_CC, CAPTURE_CC, POP_CC; invoke via CALL), INVOKE_K and INVOKE_K_VAR, and the full object-protocol set (OBJECT_*). All other opcodes are stubs; see `design/FEATURES.md` § "Opcodes: implementation status".
- Python wrappers: KosPyContinuation, KosPyAsyncGen, KosPyAsyncGenASend (eval.c); KosCoroutine and `kos_run_code_as_coroutine` (func.c). KosCoroutine drive (send/throw/close) in func.c; async-gen drive and eager-step await in eval.c.
- GC: traverse helpers for context, frame, stack block, chain, continuation, transfer plan, hook runner (eval.c, dynchain.c).
- Operators: reducing arithmetic, bitwise, membership, build-str; apply, reapply, identity, constantly, _pass; build_tuple, build_list, build_set, build_dict (operator.c, lib.c).

**Not yet implemented**:

- Remaining opcode handlers: argument building (except BUILD_FUNCTION), CALL_NAMES/CALL_STAR/CALL_ATTR*/RECUR*/TAIL_*, block control (with/begin/class, wind, region). THEEND has no runtime implementation (hardcoded error). See `design/FEATURES.md` § "Opcodes: implementation status".
- Compiler: A partial implementation exists in `koskari.compile` (Compiler, ByteCodeFragment, specials: compile/bytecode, compile/begin, compile/call, lambda). Full parse → expand → compile → bytecode pipeline is not complete (macro expansion, remaining special forms, optimization passes).
- Module creation/loading and import system integration.
- Most special forms (only types, parsing, and runtime primitives exist).
- Traceback integration with Koskari frames.

---

## 10. Clarifications and Common Pitfalls

- **call/cc vs reset/shift**: Design supports both **undelimited** (full) call/cc and **delimited** reset/shift. Delimited capture **cannot** cross Python boundaries (PYCALL); full continuations **may** cross the CPython boundary in the upwards direction only, implemented by raising a specialized BaseException. See §2 continuation semantics.

- **All code is async**: By design, Koskari code is async by default. The `KOS_CODE_YIELDS` flag distinguishes “returns coroutine” vs “returns async generator,” not sync vs async.

- **Frame vs KosDynFrame**: `KosFrame` is a typedef for `KosDynFrame`; they are the same. The “frame” is the topmost FRAME node on the dynamic chain; `ctx->frame_top` caches it.

- **Stack “block”**: In C, the evaluator uses `KosStackBlock` (linked blocks of value arrays). The Python `Stack` type is the block-structured value stack API; control-flow metadata is in `KosDynRegion`, not in the stack.

- **Borrowed vs owned**: In C, “borrowed” means the frame/code does not take ownership (no DECREF); “owned” or “stolen” means the receiver is responsible for DECREF or free. Docstrings in dynchain.h and eval.h specify per field.

- **Tox**: This repo uses **tox** for environments and tests. Run Python via `tox -e <env>` (e.g. `tox -e py312`), not raw `python` or `python3`, per project rules.

---

## 11. Where to Look in the Repo

| Topic              | Design doc(s)                    | Implementation                          |
|-------------------|-----------------------------------|-----------------------------------------|
| Execution model   | interpreter/execution-invariants.md| koskari/lib/eval.c, eval.h              |
| Domain (TLS, KosDomain) | interpreter/domain.md      | koskari/lib/domain.c, domain.h          |
| Coroutine / run-code | (this doc, evaluator.md)         | koskari/lib/func.c, func.h              |
| Dynamic chain     | interpreter/evaluator.md          | koskari/lib/dynchain.c, dynchain.h      |
| Continuations     | interpreter/callcc.md             | dynchain.c (capture/invoke), eval.c     |
| Bytecode          | interpreter/bytecode.md, bytecode-metadata-tables.md | koskari/lib/bytecode.c, bytecode.h, opcodes.incl |
| Types             | api/types.md                     | koskari/lib/*.c, koskari/types.py       |
| Builtins/API      | api/builtins.md, api/runtime.md   | koskari/lib/lib.c, operator.c           |
| Language          | language/*.md                    | koskari/parse.py, koskari/lib/parse.c   |
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
