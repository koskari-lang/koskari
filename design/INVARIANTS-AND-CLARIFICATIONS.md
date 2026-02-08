# Koskari: Invariants and Clarifications

This document is intended for teaching another system (human or agent) about the current state of Koskari: what is designed, what is implemented, and what invariants and clarifications hold across the codebase and design docs. Use it as a single reference for "how things work" and "what must stay true."

**Meta**: This document records settled design facts and invariants only. No speculative alternatives are permitted here. Conflicts are recorded explicitly and left unresolved.

---

## 1. Project Identity and Lineage

**Koskari** is a LISP dialect for Python (sibilant-ng). It is the successor to:

- **python-sibilant**: Compiles to CPython bytecode (targets 3.5–3.7); provides reader, types, compiler to `.pyc`.
- **sibilant-ceval**: Custom bytecode and custom evaluator; provides Namespace, Ref, Formals, frame/stack model.

Koskari **abandons** compiling to CPython bytecode. It uses a **custom bytecode format** and a **custom C evaluator** (VM). Design and implementation draw from both predecessors; migration status is tracked in `design/README.md` and `design/FEATURES.md`.

**Package layout**: The runtime lives under `koskari/` (Python API and C extension in `koskari/lib/`). Design docs live under `design/` (language, api, interpreter). Tests are under `tests/` (Python and `tests/native/` for C).

---

## 2. Execution Model Invariants

These are **commitments**: they must hold for correct behavior. They are elaborated in `design/interpreter/execution-invariants.md`.

- **Native C extension**: Koskari is a bytecode-based evaluator implemented as a native C extension running inside CPython.

- **Flattening (stackless)**: The evaluator is internally flattening (stackless): nested Koskari calls do **not** consume the C call stack. Instead, execution state is represented by heap-allocated linked structures. Dynamic nodes and frames are interleaved into a heap-allocated linked list representing execution history. From CPython's perspective, the Koskari evaluator is opaque and invisible.

- **Entry point**: Entry into the evaluator occurs via the `tp_call` (call implementation) of the Koskari function CPython type.

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

- **Python awaitable interop**: Koskari may await **Python** awaitables. They are treated as opaque; Koskari does **not** recurse into CPython’s await machinery. The boundary wrapper steps them iteratively (e.g. `send(None)`).

- **Await probe (eager-step)**: When the VM hits `AWAIT(py_awaitable)`, the runtime gets the await-iterator and does **one speculative step** (`send(None)`). If the result is immediate completion (`StopIteration(value)`), no suspension; value is injected back. Otherwise, suspension occurs and the iterator is stored for later resumption.

- **Single wrapper, iterative driving**: From Python, each async Koskari call has **one** wrapper. That wrapper resumes the VM, yields Python awaitables when needed, and receives results/exceptions. There is no recursive Python coroutine chain per nested Koskari `await`.

- **Sync adapter policy**: Blocking execution is **explicit**. Sync execution uses a **dedicated event loop** created by Koskari that drives the Koskari awaitable to completion. If no Python loop is running, use a local loop in-thread; if one is running, use a helper thread with its own loop. Sync execution is never implicit.

- **Ownership and re-entrancy**: A Koskari frame/awaitable has **exactly one active driver** at a time. Concurrent or re-entrant resume is an error. Wrappers/runners enforce this.

- **Generators vs async**: Generators are **not** awaitable. Async generators are **not** awaitable. Async **functions** return awaitables. Yield and await are **disjoint** control paths.

- **Python boundary for continuations**: Captured continuations **must not** include Python/C call stack. The dynamic chain marks Python boundaries with `KOS_DYN_PYCALL`. `shift` capture is **rejected** if any such node exists between current position and the target prompt. Implementation: `kos_validate_capture()` walks from `dyn_top` to the prompt.

- **Unified dynamic chain**: All execution context (frames, prompts, winds, regions) lives on a **single** chain of `KosDynNode` structures. Node kinds: `KOS_DYN_FRAME`, `KOS_DYN_PROMPT`, `KOS_DYN_WIND`, `KOS_DYN_REGION`, `KOS_DYN_PYCALL`. Walking the chain gives full dynamic extent.

- **VM-native wind/unwind**: Wind/unwind hooks are **bytecode blocks** (implemented as null-arity Koskari callables that compile to bytecode). They run in the VM via `KosHookRunner` with **isolated** operand stacks and end with the `END_WIND` sentinel opcode. They execute in their own frames and capture lexical context via closure; they cannot see or change the enclosing frame’s stack.

- **Control flow**: Koskari supports local-only jumps restricted to the current block context. Local-only jumps implement continue, break, conditional control flow, and try/except/finally. Non-local jumps across block or dynamic extents are not permitted except via continuations.

- **Local jumps vs wind/unwind**: Local jumps and wind/unwind are **distinct mechanisms**: local jumps do **not** invoke wind/unwind handlers; continuation transfer **does** invoke them.

- **Continuation semantics**: Koskari implements both **delimited** continuations (reset/shift) and **reusable full** continuations. Delimited continuations capture from the current point up to (but not including) the nearest matching prompt; they are restricted to the same evaluator context and may not cross the CPython C calling-convention boundary. Full continuations may be invoked within the same evaluator context or into an upwards context; full invocation may cross the CPython boundary only in the upwards direction (via a specialized BaseException). Continuations capture a reference to their owning evaluator context. They are **multi-shot** by default. Invocation runs wind/unwind hooks in the correct order. Python and C frames are **not** embedded in the Koskari dynamic chain; only Koskari frames are re-enterable.

- **Context lifecycle**: Each evaluator context records an explicit lifecycle state (at least: active, sleeping/await/yield, exited). It is an error to apply a continuation whose owning context state does not permit invocation. Attempting to invoke an unusable continuation (delimited or full) raises an exception at the invocation site.

- **Transfer plan atomicity**: Continuation application is done via a **transfer plan**. The plan runs atomically with respect to normal VM execution; the VM does not interleave opcode dispatch with mid-transfer state. Exception: hooks may suspend on AWAIT, in which case the transfer plan is suspended with the context.

- **Chain version**: The context keeps a `chain_version` counter, incremented on every chain push/pop. Transfer plans record the version at creation. Nested transfers that change the chain can be detected by version mismatch for safe resumption.

---

## 3. Data and Type Invariants

- **Atoms**: `symbol` and `keyword` are interned; same name ⇒ same object. `gensym()` produces unique symbols (optional prefix). Implemented in `koskari/lib/atom.c`.

- **Pairs**: Cons cells; `nil` is the singleton empty list and is a subtype of pair. Pairs can be proper (terminated by `nil`) or improper. Recursion detection is supported. Implemented in `koskari/lib/pair.c`.

- **Namespace**: Lexical scope container; `NamespaceSpec` describes layout. `ModuleNamespace` is module-level with builtins fallback. `CollectorNamespace` blocks closure capture. Implemented in `koskari/lib/namespace.c`.

- **Refs**: Mutable reference cells. Variants: `Ref`, `AttributeRef`, `ItemRef`, `DynamicRef`, `ConstantRef`, `MemoizedRef`. Used for shared state in closures and by-name binding. Implemented in `koskari/lib/ref.c`.

- **Formals**: Parameter specification; `FormalsSpec` is the template; `Formals` instances are created with `apply()` for binding. Implemented in `koskari/lib/formals.c`.

- **Values**: Multi-value return object (positional and keyword). Implemented in `koskari/lib/values.c`.

- **Stack**: `Stack` (Python) is block-structured; the **storage** for the VM is `KosStackBlock` (C). Control-flow metadata (block style, labels, enabled) lives in **`KosDynRegion`** nodes on the dynamic chain, not on the stack. Implemented in `koskari/lib/stack.c` (storage only).

- **Bytecode**: `Code` holds immutable instructions (4 bytes each: `uint16_t op`, `uint16_t arg`), delta-encoded position table, and constant pool. After init, code and pool are made read-only with `mprotect()`. `KOS_CODE_YIELDS` marks code that contains yield (→ async generator). Implemented in `koskari/lib/bytecode.c`; opcodes in `koskari/lib/opcodes.incl`.

---

## 4. Evaluator and Dynamic Chain (C Structures)

- **KosDynNode**: Base for all chain nodes; first member must be `parent`, `kind`, `flags`. All node types embed this first.

- **KosDynFrame**: Function activation; holds `code` (borrowed), `pc`, `stack` (owned), `ns` (owned). Frame owns stack and ns; code is not owned.

- **KosDynPrompt**: Delimiter for reset; `prompt_id` for matching (0 = default). Optional `handler_code` / `handler_pc` for the handler run after shift aborts.

- **KosDynWind**: Dynamic-wind extent; `before_code`/`before_pc`, `after_code`/`after_pc`; `entered` marks whether before has run. Hooks are bytecode blocks, not Python.

- **KosDynRegion**: Block-local control; `KosRegionStyle` (TRY, FINALLY, LOOP, WITH, FOREACH, AWAIT_WITH, AWAIT_FOREACH, NS, BEGIN). `a_label`, `b_label` are jump targets; `enabled` for exception handling.

- **KosContext**: Top-level VM state: `dyn_top`, `frame_top`, `globals_frame`, `globals_cache_version`, `hook_runner`, `plan` / `plan_stack`, `chain_version`, `invocation_segment_top`, `py_await_iter`, `pending_throw`/`pending_exc`, `handling_raise`/`handling_result`, `flags`.

- **KosEvent**: Boundary event from `kos_drive`: `KOS_EV_RETURN`, `KOS_EV_RAISE`, `KOS_EV_YIELD`, `KOS_EV_AWAIT`; `payload` is a new reference owned by the caller.

- **KosContinuation**: Captured segment: `captured_chain`, `resume_frame`, `resume_pc`, `resume_stack`, `prompt_id`, `one_shot`. Multi-shot clones on each invoke.

- **KosTransferPlan**: Ordered list of steps: `KOS_XFER_RUN_HOOK`, `KOS_XFER_INSTALL`, `KOS_XFER_INJECT`. Tracks `chain_version` and `original_target` for replanning after nested transfers. For invocation: `resume_pc`, `resume_frame`, `resume_stack`, `continuation_ref`.

- **KosHookRunner**: Runs one hook block: `code`, `pc`, `scratch` stack, `active` flag.

**Ownership**: Context owns the dynamic chain. On continuation capture, chains are cloned for multi-shot. Only Python-visible wrappers are GC-tracked; C structs are traversed via wrapper `tp_traverse`.

---

## 5. Python-Facing Wrappers

- **KosPyContinuation**: Wraps `KosContinuation`. Callable: calling it returns a `KosPyCoroutine` that resumes with the argument. Multi-shot: each call clones captured state.

- **KosPyCoroutine**: Wraps `KosContext` for the coroutine protocol (`__await__`, `send`, `throw`, `close`). States: NEW, RUNNING, WAITING_PY, DONE.

- **KosPyAsyncGen** / **KosPyAsyncGenASend**: Async generator protocol; `__anext__` returns an awaitable that drives until yield or completion.

---

## 6. Bytecode and Opcodes

- **Instruction**: 4 bytes = `KosOp { op, arg }` (both uint16_t). The exact bytecode encoding is not finalized; current aim is a fixed-width encoding of (uint16 op, uint16 arg).
- **Position**: Stored in a separate delta-encoded table; only changes are recorded; lookup by instruction index is binary search.
- **Opcodes**: Defined in `opcodes.incl` with `OP(name, style)`. Styles: COMPILE, LABEL, NOARG, COUNT, PAIR_COUNT, STAR, CONST, VAR, FASTVAR. Categories include stack, control flow, jumps, variables (VAR / FASTVAR), attributes, items, calls (including tail/recur), block control, delimited continuations (PUSH_PROMPT, SET_PROMPT_HANDLER, POP_PROMPT, SHIFT), dynamic wind (PUSH_WIND, POP_WIND, END_WIND), continuation invocation (INVOKE_K), region control, object protocol, iteration, introspection, exceptions.

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
- Evaluator core: KosContext, KosFrame, kos_context_new/free, kos_frame_new/clone/free, kos_koscall, kos_tail_koscall (eval.c).
- Drive loop: `kos_drive()` is implemented with opcode fetch, dispatch table, and boundary handling. Many opcodes are implemented (stack, constants, exceptions, try/finally, delimited continuations, object protocol); many others are still stubs that raise "unimplemented opcode" at runtime.
- Python wrappers: KosPyContinuation, KosPyCoroutine, KosPyAsyncGen, KosPyAsyncGenASend; coroutine/async-gen drive logic and eager-step await (eval.c).
- GC: traverse helpers for context, frame, stack block, chain, continuation, transfer plan, hook runner (eval.c, dynchain.c).
- Operators: reducing arithmetic, bitwise, membership, build-str; apply, reapply, identity, constantly, _pass; build_tuple, build_list, build_set, build_dict (operator.c, lib.c).

**Not yet implemented**:

- Remaining opcode handlers (variables, attributes, items, calls, blocks, loops, etc. — currently stubs).
- Compiler (parse → expand → compile → bytecode).
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
| Dynamic chain     | interpreter/evaluator.md          | koskari/lib/dynchain.c, dynchain.h      |
| Continuations     | interpreter/callcc.md             | dynchain.c (capture/invoke), eval.c     |
| Bytecode          | interpreter/bytecode.md           | koskari/lib/bytecode.c, bytecode.h, opcodes.incl |
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
