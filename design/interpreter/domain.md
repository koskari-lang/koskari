# Koskari Domain (Ambient Dynamic Context)

This document describes the design and implementation plan for **Domain** (ambient dynamic context), **ParamRef** (canonical parameter references), and **PARAM** dynamic extent (parameter bindings on the dyn chain). It specifies TLS plumbing, opcodes (`PARAM`, `PUSH_PARAM`, `POP_PARAM`), compiler lowering, hard invariants, and step-by-step implementation.

**Amendment**: parameter storage is being redesigned to fix parameter corruption between concurrently-driven contexts: bindings become cells (ordinary Refs) in a per-context dict forked at context creation; `parameterize` installs fresh cells and restores prior cells on unwind; the Domain narrows to homing the base cell dict (pre-defines, Python-side access) plus the TLS discovery machinery. The value-store description in D2/D3 below predates this. See [domain-task-isolation.md](domain-task-isolation.md).

**Implementation status**: Steps 1, 2, 3, 4, 5, and 6 (opcodes) are implemented. TLS and KosDomain live in `domain.c`; the domain-scoped API includes `kos_ctx_current()`, `kos_domain_current()`, `kos_domain_push_scope()`, `kos_domain_pop_scope()`, `kos_domain_push_python_scope()`, `kos_domain_pop_python_scope()`. KosDomain is a context manager (`with Domain():`) so Python can establish a domain before calling Koskari; `kos_domain_current()` consults the ctx stack and the per-thread `python_domain_stack`. KosDomain holds a param value store (PyDict: name -> value). ParamRef (Ref subclass) resolves via `kos_domain_current()` and `kos_domain_get_param(key)`; no interning; fully dynamic value lookup. KOS_DYN_PARAM dyn nodes are implemented in `dynchain.c`: `kos_dyn_push_param`, `kos_dyn_push_param_dict`, `kos_dyn_pop_param`, enter/restore via domain, transfer-plan integration (PARAM_RESTORE on unwind, param enter folded into INSTALL). Opcodes PARAM, PUSH_PARAM, POP_PARAM are implemented. PARAM extent is unwound on RETURN and exception propagation via `kos_dyn_unwind_to_frame()`. Compiler lowering is not yet implemented.

---

## Motivation

Koskari's model makes "ambient context" a first-class requirement:

- **Flattened async**: Multiple Koskari contexts can be resumed on the same OS thread over time; thread-local alone is insufficient without swapping at resume points.
- **Koskari↔Python interop**: Koskari can yield to Python and Python can call back into Koskari; we want a *shared* ambient context across a Koskari→Python→Koskari stack when desired.
- **Continuations**: Multi-shot continuations plus transfer plans mean ambient context must be well-defined under capture/invoke.

A **Domain** is the general solution: it is the "ambient dynamic context object" that follows a computation across suspension/resume and across language boundaries.

---

## Goals

- Provide a generalized container for **dynamic-extent scoped** settings and metadata.
- Support **parameter-scoped variables** (Scheme/Racket-style parameters / CL-style specials via objects).
- Be correct under:
  - `AWAIT` suspend/resume
  - nested Koskari↔Python↔Koskari
  - continuation transfer plans (including multi-shot)
- Avoid new "meta-global Koskari state" beyond small, explicit, **per-thread TLS plumbing** already used for boundary management.

---

## Non-Goals

- Replace lexical namespaces/Refs. Domain is **not** a lexical environment.
- Provide arbitrary cross-thread propagation automatically (that should be explicit policy).

---

## Deliverables

1. **TLS plumbing and static API** (implemented): Per-thread TLS stack(s) for current context (and/or current domain); `kos_ctx_current()` and `kos_domain_current()` callable from native Python object methods. TLS, `KosTlsState`, and accessors live in `domain.c`. The evaluator calls `kos_domain_push_scope(ctx)` on entry and `kos_domain_pop_scope(ctx)` on cleanup. See `koskari/lib/domain.c`, `domain.h`.
2. **KosDomain runtime object** (implemented): Minimal v1 — identity/lifecycle anchor with optional `version` field. Every KosContext owns a `domain` (PyObject* KosDomain). New contexts inherit the active context's domain when `kos_ctx_current() != NULL`; otherwise a fresh KosDomain is created. Exposed as `koskari.lib.Domain`.
3. **ParamRef type** (implemented): Ref subclass; key (name) and default_value; no interning. Resolves via `kos_domain_current()` and `kos_domain_get_param(key)`. Exposed as `koskari.lib.ParamRef`.
4. **PARAM dynamic extent**: New dyn node kind `KOS_DYN_PARAM`; correct unwind behavior; included in continuation capture/apply.
5. **Bytecode/opcode additions**: Implement `PARAM`, `PUSH_PARAM`, `POP_PARAM` (or equivalent minimal pair); update opcode tables/docs and assembler/disassembler paths.
6. **Compiler lowering (minimum)**: `(use-param X)` expands to binding `X` to `(PARAM "X")` (not yet implemented). `compile/parameterize` is implemented: compiles bindings onto the stack, emits PUSH_PARAM (names tuple constant), compiles body, emits POP_PARAM. Consumers read the current parameter via ParamRef(name).deref() until bind-param exists.
7. **Tests**: Nested parameterize restore; exception/unwind restore; Koskari→Python→Koskari inheritance; async interleave on one OS thread; continuation capture/apply reinstates parameter bindings (multi-shot). Compile tests in `tests/compile/specials/test_parameterize.py` use consumer functions that call ParamRef(name).deref() and are invoked inside the parameterize body; they assert the consumer sees the parameterized value and that the parameter is restored after the block exits.

---

## Hard Invariants

### I1 — TLS correctness

While executing Koskari bytecode, TLS must identify the active Koskari context (and thus its domain/dyn chain).

### I2 — No leakage across boundaries

On any exit from `koskari_eval` to Python (including `AWAIT`), TLS must be restored to its previous value (stack pop).

### I3 — PARAM extent is unwindable

Leaving a scope that introduced PARAM bindings must restore previous bindings.

### I4 — Continuations capture PARAM

A captured continuation must include PARAM extents so applying it reinstates the dynamic parameter environment.

---

## Design Decisions

### D1 — Canonical ParamRef per key

There must be a single ParamRef instance per parameter key *within an interning domain*.

**Recommendation for v1: module/root-namespace interning**

- `PARAM "X"` yields the module’s canonical ParamRef for key `"X"`.
- Sharing across modules occurs by importing/binding that ParamRef object.

(Alternative later: Domain-level or process-level interning; do not block v1.)

### D2 — Where bindings live (implemented)

**Domain holds param values** (name -> value dict):

- KosDomain has `param_bindings` (PyDict: name -> value); lazy-init on first access.
- `ParamRef.deref()`: `domain = kos_domain_current()`; `value = kos_domain_get_param(domain, param_ref->key)`; return value or default.
- ParamRef does **not** walk the dyn chain; it asks the active domain for the value.
- A later phase will add `KOS_DYN_PARAM` on the dyn chain; the dyn node handler will call `kos_domain_set_param` on push and restore on pop, so the domain remains the single source of truth.

### D3 — Domain role in v1 (implemented)

- KosDomain holds `param_bindings` (PyDict: name -> value).
- ParamRef resolves via `kos_domain_current()` and `kos_domain_get_param(key)`.
- Interning is not implemented in v1; ParamRef is created via constructor `ParamRef(key, default=None)`.

---

## Conceptual Model

### Terminology

- **Context**: A heap-resident Koskari evaluator state (frames + dynamic chain) driven by `koskari_eval()` until a boundary event.
- **Token**: An identity/safety handle used for continuation ownership checks.
- **Domain**: The ambient dynamic context object associated with a Context (and optionally shared with other Contexts). Holds `param_bindings` (name -> value). ParamRef resolves via `kos_domain_current()` and domain lookup.
- **ParamRef**: A Ref-compatible object representing a parameter; stores key (name) and default_value. Resolution uses `kos_domain_current()` and `kos_domain_get_param(key)`; fully dynamic—no domain pointer stored. Two ParamRefs with the same key share the same domain binding.
- **PARAM extent**: A dynamic extent introduced by `PUSH_PARAM` and removed by `POP_PARAM`; implemented as a `KOS_DYN_PARAM` node on the unified dynamic chain. Unwindable on exceptions and boundary exits; captured and restored as part of continuations.

### What Belongs in a Domain

A Domain is for values that are:

- dynamic-extent scoped (push/pop)
- should persist across `AWAIT` suspend/resume
- should cross Koskari↔Python boundaries *with the computation*

Examples (all optional, but Domain gives you a home for them):

- **Parameters**: print settings (`*print-base*`, `*print-width*`, …)
- **Continuation marks** / dynamic metadata: trace IDs, profiling spans, debug tags
- **Cancellation/deadlines**: cooperative cancel token, timeout/deadline policy
- **I/O defaults**: "current output port" or formatting policies
- **Security/capability context**: authority/capability objects to gate host calls
- **Interop policy**: conversion knobs for Python↔Koskari values

---

## Data Model

### KosDomain (v1 implemented)

- Identity/lifecycle anchor associated with a Context (`ctx->domain`).
- Version counter for future use.
- `param_bindings` (PyDict: name -> value); lazy-init. API: `kos_domain_get_param(domain, key)`, `kos_domain_set_param(domain, key, value)`, `kos_domain_clear_param(domain, key)`.
- KOS_DYN_PARAM is implemented in dynchain.c; its enter/restore handlers call the domain API.

Typed hot slots (cancel token, capabilities, I/O defaults) and generic key/value overlays are deferred to a later iteration.

### KOS_DYN_PARAM (dyn chain node; implemented)

Parameter bindings on the **unified dynamic chain** as a new node kind:

- **Node kind**: `KOS_DYN_PARAM`.
- **Storage**: `bindings` (PyDict key -> value, what we bind; keys from ParamRef); `saved` (PyDict key -> value or SENTINEL_UNSET; filled at enter, used at restore; NULL if not yet entered).
- **Enter**: `kos_dynparam_enter` saves current domain values into `saved`, merges `bindings` onto domain. Exposed as the unified extent helper `kos_dyn_param_wind`.
- **Restore**: `kos_dynparam_restore` restores domain from `saved` (set or clear per key). Exposed as `kos_dyn_param_unwind`.

For the unified wind/unwind contract and the per-kind extent table (PARAM, WIND, NS, REGION, YIELD_FROM), see **extent-wind-unwind.md**.

The dyn node handler calls `kos_domain_set_param` on push (after saving) and restore on pop; the domain remains the single source of truth. ParamRef resolves via `kos_domain_current()` and `kos_domain_get_param(key)`; no dyn chain walk. Transfer plan: PARAM_RESTORE on unwind (current -> LCA); param enter folded into INSTALL (walk installed chain, run enter for each PARAM).

---

## Lifecycle and Boundary Rules

### TLS Plumbing (I1, I2) (implemented)

Implemented in `koskari/lib/domain.c`. Per OS thread:

- **`KosTlsState`**: holds `ctx_stack` and `token_stack` (stack of `KosContext*` and `KosEvaluatorToken*`; stack discipline).

**Domain-scoped API** (callable from native Python object methods):

- `KosContext* kos_ctx_current(void)` — returns top of ctx stack or NULL.
- `PyObject* kos_domain_current(void)` — returns active Domain or NULL (borrowed ref).
- `void kos_domain_push_scope(KosContext *ctx)` — push token and ctx onto TLS, increment live_depth.
- `void kos_domain_pop_scope(KosContext *ctx)` — decrement live_depth, pop ctx and token.

**Evaluator integration**:

- On entry to `koskari_eval(ctx)`: call `kos_domain_push_scope(ctx)`.
- On every exit path (RETURN, RAISE, YIELD, AWAIT): call `kos_domain_pop_scope(ctx)` in the C cleanup path so TLS is restored before returning to Python.

Debug assertions: `kos_ctx_current() != NULL` during bytecode execution.

### Koskari→Python→Koskari

**Default**: When creating a new Context from Python, if TLS ctx stack is non-empty, the new Context **inherits** `domain` from the active context (shared). Otherwise, if `python_domain_stack` is non-empty, the new Context inherits the top domain. Otherwise, create a fresh domain.

**Opt-out**: API to force a fresh domain (e.g. `koskari.eval(..., domain="fresh")` or `(with-fresh-domain ...)`).

### Python-managed domain scope (implemented)

Domain acts as a context manager so Python can establish a domain before calling Koskari and consume or set parameters after it returns:

```python
with Domain():
    ParamRef("x").store(123)
    result = await some_koskari_fn()
    assert ParamRef("x").deref() == 123  # domain persists after koskari returns
```

Per-thread TLS includes `python_domain_stack`; `kos_domain_push_python_scope(domain)` and `kos_domain_pop_python_scope(domain)` push and pop. `kos_domain_current()` returns the top of `ctx_stack` when non-empty, else the top of `python_domain_stack`, else NULL. Domain implements `__enter__` and `__exit__`, which call the push/pop helpers.

---

## ParamRef and PARAM Extent

### ParamRef type (implemented)

- **Representation**: Native Python heap type; subclass of Ref.
- **Fields**: `key` (name, used for domain lookup), `default_value`.
- **No interning in v1**; ParamRef created via `ParamRef(key, default=None)` or `KosParamRef_New(key, default_value)`.

**Semantics** (fully dynamic—domain resolved at deref/store time):

- **`ref_get(ParamRef)`**: `domain = kos_domain_current()`; if NULL, return default or raise; else `value = kos_domain_get_param(domain, param_ref->key)`; return value or default.
- **`ref_set!(ParamRef, v)`**: `domain = kos_domain_current()`; if NULL, raise; else `kos_domain_set_param(domain, param_ref->key, value)`.
- ParamRef never stores a domain pointer; safe to capture in closures that may run on another thread.

### Surface API (Lisp)

```lisp
(define *print-base* (parameter 10))

(ref-get *print-base*)        ; => current value (dynamic, from chain)
(ref-set! *print-base* 12)     ; => sets default (recommended)
(parameterize ((*print-base* 16) (*print-pretty* true))
  (pretty-print 255))
```

### Continuations and PARAM (I4)

Parameter bindings live on the dyn chain (`KOS_DYN_PARAM`). Continuation capture already includes the dyn chain segment; therefore PARAM extents are **automatically** captured and restored on apply. No separate Domain swap is required; the transfer plan’s chain splice restores the parameter environment.

---

## Bytecode and Opcodes

### PARAM \<const\>

- Reads key from constant pool.
- Obtains canonical ParamRef via interning (e.g. from current module/namespace).
- Pushes ParamRef onto stack.

### PUSH_PARAM \<n\>

- Pops `2*n` stack entries: `(param, value)` pairs (or `n` params then `n` values; pick one stack discipline and document it).
- Validates each `param` is a ParamRef.
- Allocates a `KOS_DYN_PARAM` dyn node storing the pairs.
- Pushes it onto the dyn chain.

### POP_PARAM

- Validates dyn top is `KOS_DYN_PARAM`.
- Pops it.

Opcodes must be added to opcode tables, docs, and assembler/disassembler paths.

---

## Compiler Lowering

- **`(use-param X)`**: Expand to binding local `X` to `(PARAM "X")` (i.e. load ParamRef for key `"X"` and bind in current scope). Not yet implemented.
- **`(compile/parameterize [[X v] [Y w]] body...)`** (implemented in `koskari/compile/specials/context.py`):
  - Compile each value expression onto the stack in order (first binding deepest, last at TOS).
  - Emit `PUSH_PARAM` with constant index of names tuple `(str(X), str(Y), ...)`.
  - Compile body.
  - Emit `POP_PARAM`.
  - Restore semantics come from `KOS_DYN_PARAM` unwind. Consumers of parameters (e.g. functions that will use bind-param) read the current value via ParamRef(name).deref(); the body typically calls such consumers. See `tests/compile/specials/test_parameterize.py`.

---

## Unwind Integration (I3) (implemented)

Leaving any block that introduced PARAM bindings must restore previous bindings. Implemented via `kos_dyn_unwind_to_frame()` in `dynchain.c`: before RETURN (detach or top-level event) and before propagating an exception when no local handler is found, the evaluator pops all chain nodes from dyn_top down to the current frame. Each extent pop runs the corresponding **unified extent unwind helper** (e.g. `kos_dyn_param_unwind` for PARAM, `kos_dyn_ns_unwind` for NS); see **extent-wind-unwind.md**. For WIND with an after-hook, the helper may return `KOS_DYN_UNWIND_ASYNC_STARTED`: the evaluator then runs the after-hook (via the hook runner) and resumes unwinding when the hook hits END_WIND.

- **RETURN**: `RETURN.c` pops the value and returns `EVAL_RETURN(val)`; `eval_event_return` runs unwind and delivery. If unwind returns async (wind after-hook started), the return value is stashed and the loop runs the hook; END_WIND resumes unwinding and delivers the value.
- **Exceptions**: `kos_dyn_try_local_raise_body` unwinds toward the frame when no region is found; if a pop returns async, the exception is restored and the loop runs the after-hook, then unwinding resumes from END_WIND.
- `AWAIT`/`YIELD` boundary exits do not leave TLS or dyn chain inconsistent; the context’s chain is preserved across suspend/resume.
- Continuation transfer plans splice the captured chain (which includes PARAM nodes) so bindings are reinstated on apply.

---

## Implementation Plan (Step-by-Step)

### Step 1 — TLS stacks and static API (done)

- Implemented in `domain.c`: `KosTlsState` with `ctx_stack` and `token_stack`.
- API: `kos_ctx_current()`, `kos_domain_current()`, `kos_domain_push_scope(ctx)`, `kos_domain_pop_scope(ctx)`.
- In `koskari_eval(ctx)`: call `kos_domain_push_scope(ctx)` on entry; `kos_domain_pop_scope(ctx)` on every exit path (RETURN/RAISE/YIELD/AWAIT).

### Step 2 — KosDomain object (done)

- Implemented in `domain.c`, `domain.h`: KosDomain type (PyObject_HEAD + uint64_t version). `ctx->domain` in KosContext.
- Domain lifecycle: `KosContext_New()` inherits domain from active ctx when `kos_ctx_current() != NULL`; else from `python_domain_stack` when non-empty; else creates fresh KosDomain. GC: `Py_VISIT` in traverse, `Py_XDECREF` in free.
- Domain is a context manager: `__enter__` and `__exit__` call `kos_domain_push_python_scope` and `kos_domain_pop_python_scope`.

### Step 3 — ParamRef type (done)

- Implemented ParamRef (Ref subclass); key (name) and default_value; no interning.
- KosDomain: `param_bindings` (PyDict: name -> value), `kos_domain_get_param`, `kos_domain_set_param`, `kos_domain_clear_param`.
- ParamRef.deref: `domain = kos_domain_current()`; `kos_domain_get_param(domain, key)` or default.
- ParamRef.store: `domain = kos_domain_current()`; `kos_domain_set_param(domain, key, value)`.

### Step 4 — Add PARAM dyn node kind (done)

- Implemented in `dynchain.c`, `dynchain.h`: `KOS_DYN_PARAM` dyn node kind.
- Node stores: `bindings` (PyDict key -> value), `saved` (PyDict key -> value or SENTINEL_UNSET; filled at enter).
- `kos_dyn_push_param`, `kos_dyn_pop_param`; `kos_dynparam_enter`, `kos_dynparam_restore`.

### Step 5 — Unwind integration (done)

- `kos_dyn_pop_top` handles KOS_DYN_PARAM: restore domain, pop node.
- `kos_dyn_unwind_to_frame(ctx, frame)` pops nodes from dyn_top until dyn_top is the given frame; used by RETURN and exception propagation. Pops use the unified extent unwind helpers; WIND may defer the pop until the after-hook runs (see extent-wind-unwind.md).
- Transfer plan: EXTENT_ACTION steps call `kos_extent_run_wind` / `kos_extent_run_unwind`; INSTALL step runs `kos_extent_run_wind` for each PARAM and DYN_WITH in the installed segment.

### Step 6 — Opcodes

- Implement `PARAM`, `PUSH_PARAM`, `POP_PARAM` as above.
- Update opcode tables, design docs (e.g. OPCODES.md), assembler/disassembler.

### Step 7 — Compiler lowering and continuations

- `compile/parameterize` is implemented (PUSH_PARAM/POP_PARAM; consumers use ParamRef.deref()). `use-param` not yet implemented.
- Ensure continuation capture/apply includes PARAM nodes (dyn chain slice). Add test: capture inside parameterize, exit parameterize, apply continuation and confirm bindings reinstated; test multi-shot.

---

## Testing Matrix (Required)

**Implemented tests**: Native C tests in `tests/native/test_paramref.c` and Python tests in `tests/lib/test_paramref.py` cover ParamRef creation, deref/store/clear outside and inside active domain, shared value for same key, and `kos_dyn_push_param`/`kos_dyn_pop_param` enter/restore (including nested param extent restore). `tests/lib/test_domain.py` covers Domain as context manager. `tests/lib/opcode/test_push_param.py` covers PUSH_PARAM/POP_PARAM opcodes, including unwind-on-return and unwind-on-raise (PARAM extent restored when RETURN or exception exits before POP_PARAM). `tests/compile/specials/test_parameterize.py` exercises the compile/parameterize special form: consumer functions (using ParamRef(name).deref() in place of bind-param) are invoked inside the parameterize body; tests assert the consumer sees the parameterized value, multiple parameters are visible, and the parameter is restored after the block exits.

**Remaining tests** (for opcodes, parameterize):

1. **Nesting restore**: Outer binds X=8, inner binds X=16; verify correct values and restore.
2. **Exception restore**: Parameterize binds X=16 then raises; after catching, X resolves to prior/default.
3. **Cross-language re-entry**: Koskari parameterizes X then calls Python; Python calls back into Koskari; inner sees same X binding by default.
4. **Async interleave**: ctxA binds X then awaits; ctxB runs on same OS thread with different domain/context and sees default/different binding; ctxA resumes and sees its binding intact.
5. **Continuation capture/apply**: Capture inside parameterize; apply later; binding reinstated; multi-shot applies repeatedly.

---

## Completion Criteria

- TLS stack and `kos_ctx_current()` are correct and do not leak across `AWAIT`.
- ParamRef resolves current bindings reliably via `kos_domain_current()` and domain param store.
- `KOS_DYN_PARAM` nodes are unwindable and included in continuation capture/apply.
- `PARAM`, `PUSH_PARAM`, `POP_PARAM` opcodes function and are documented.
- All required tests pass.

---

## References

- [execution-invariants.md](execution-invariants.md): Koskari execution model and boundary semantics
- [callcc.md](callcc.md): Continuation capture and transfer
- [evaluator-token-continuation.md](evaluator-token-continuation.md): Evaluator token and context ownership
- [bytecode.md](bytecode.md): Bytecode format; [OPCODES.md](../OPCODES.md): Opcode catalog (PARAM, PUSH_PARAM, POP_PARAM to be added)
- [language/special-forms.md](../language/special-forms.md): Special forms (parameterize will extend this)
