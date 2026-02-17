# Koskari Domain (Ambient Dynamic Context)

This document describes the design and implementation plan for **Domain** (ambient dynamic context), **ParamRef** (canonical parameter references), and **PARAM** dynamic extent (parameter bindings on the dyn chain). It specifies TLS plumbing, opcodes (`PARAM`, `PUSH_PARAM`, `POP_PARAM`), compiler lowering, hard invariants, and step-by-step implementation. Not yet implemented.

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

1. **TLS plumbing and static API**: Per-thread TLS stack(s) for current context (and/or current domain); `kos_ctx_current()` and/or `kos_domain_current()` callable from native Python object methods.
2. **KosDomain runtime object**: Minimal v1 — only what is required for parameter binding resolution and future expansion.
3. **ParamRef type**: Canonical identity per parameter key (interning); Ref-compatible API; resolves current value from active execution ambient state (via TLS).
4. **PARAM dynamic extent**: New dyn node kind `KOS_DYN_PARAM`; correct unwind behavior; included in continuation capture/apply.
5. **Bytecode/opcode additions**: Implement `PARAM`, `PUSH_PARAM`, `POP_PARAM` (or equivalent minimal pair); update opcode tables/docs and assembler/disassembler paths.
6. **Compiler lowering (minimum)**: `(use-param X)` expands to binding `X` to `(PARAM "X")`; `parameterize` compiles as LET-like plus `PUSH_PARAM`/`POP_PARAM` around body.
7. **Tests**: Nested parameterize restore; exception/unwind restore; Koskari→Python→Koskari inheritance; async interleave on one OS thread; continuation capture/apply reinstates parameter bindings (multi-shot).

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

### D2 — Where bindings live (recommended)

Make the **dyn chain the truth**:

- `KOS_DYN_PARAM` stores bindings as a compact vector of `(ParamRef*, value)` pairs (or parallel arrays).
- `ParamRef.get()` resolves by walking the active dyn chain.

This avoids dict/overlay mutation and makes continuation behavior automatic because the dyn chain is captured.

Optional later optimization: caching keyed by `(ctx->chain_version, ParamRef*)`.

### D3 — Domain role in v1

Keep `KosDomain` minimal:

- Exists to support future ambient features.
- Provides an identity/lifecycle anchor.
- May hold per-context ParamRef interning table (if you choose module interning, Domain can still be present).

Do **not** require Domain overlay dicts for parameter bindings in v1.

---

## Conceptual Model

### Terminology

- **Context**: A heap-resident Koskari evaluator state (frames + dynamic chain) driven by `koskari_eval()` until a boundary event.
- **Token**: An identity/safety handle used for continuation ownership checks.
- **Domain**: The ambient dynamic context object associated with a Context (and optionally shared with other Contexts). In v1, minimal: identity/lifecycle anchor; parameter bindings live on the dyn chain, not in the Domain.
- **ParamRef**: A canonical Ref-compatible object representing a parameter; one instance per key within the interning domain (e.g. per module). Resolution walks the dyn chain for `KOS_DYN_PARAM` bindings.
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

### KosDomain (v1 minimal)

For v1, Domain is a minimal shell:

- Identity/lifecycle anchor associated with a Context (`ctx->domain`).
- Optional version counter for future use.
- No overlay dicts for parameter bindings in v1; bindings live on the dyn chain (D2, D3).

Typed hot slots (cancel token, capabilities, I/O defaults) and generic key/value overlays are deferred to a later iteration.

### KOS_DYN_PARAM (dyn chain node)

Parameter bindings are stored on the **unified dynamic chain** as a new node kind:

- **Node kind**: `KOS_DYN_PARAM`.
- **Storage**: `count`; array of `(ParamRef*, value)` pairs (or parallel arrays: `ParamRef** params`, `PyObject** values`).
- **Lookup**: `kos_dyn_param_lookup(KosDynNode* node, ParamRef* p, PyObject** out)` returns whether this node binds the given ParamRef and writes the value.

Resolution: `ParamRef.get()` walks `ctx->dyn_top` downward, finds the first `KOS_DYN_PARAM` that binds this ParamRef, and returns that value; else returns the ParamRef’s default.

---

## Lifecycle and Boundary Rules

### TLS Plumbing (I1, I2)

Per OS thread:

- **`tls.ctx_stack`**: stack of `KosContext*` (stack discipline).

**Static API** (callable from native Python object methods):

- `KosContext* kos_ctx_current(void)` — returns top of stack or NULL.
- Domain can be derived as `kos_ctx_current() ? kos_ctx_current()->domain : NULL`; optionally also expose `kos_domain_current()`.

**Evaluator integration**:

- On entry to `koskari_eval(ctx)`: push `ctx` onto TLS stack.
- On every exit path (RETURN, RAISE, YIELD, AWAIT): pop in a `finally`/C cleanup path so TLS is restored before returning to Python.

Debug assertions: `kos_ctx_current() != NULL` during bytecode execution.

### Koskari→Python→Koskari

**Default**: When creating a new Context from Python, if TLS ctx stack is non-empty, the new Context **inherits** `domain` from the active context (shared). Otherwise, create a fresh domain.

**Opt-out**: API to force a fresh domain (e.g. `koskari.eval(..., domain="fresh")` or `(with-fresh-domain ...)`).

---

## ParamRef and PARAM Extent

### ParamRef type

- **Representation**: Native Python heap type; subclass of `Ref` or Ref-protocol compatible.
- **Fields**: `key` (string/symbol), `default_value`, (optional) module/owning namespace pointer for interning.
- **Interning**: `ParamRef* paramref_intern(owner, key)` returns the canonical instance for that key in `owner` (e.g. module/namespace for v1).

**Semantics**:

- **`ref_get(ParamRef)`**: `ctx = kos_ctx_current()`; walk `ctx->dyn_top` for `KOS_DYN_PARAM` nodes; find a binding for this ParamRef identity; else return `default_value`.
- **`ref_set!(ParamRef, v)`** (recommended): mutate `default_value` only (does not mutate current dynamic binding). Optional separate API for “set current binding” if needed.

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

- **`(use-param X)`**: Expand to binding local `X` to `(PARAM "X")` (i.e. load ParamRef for key `"X"` and bind in current scope).
- **`(parameterize ((X v) (Y w)) body...)`**:
  - Compile each `(param, value)` pair onto the stack (e.g. evaluate param ref, evaluate value).
  - Emit `PUSH_PARAM n`.
  - Compile body.
  - Emit `POP_PARAM`.
  - Otherwise behave like LET (lexical scope); restore semantics come from `KOS_DYN_PARAM` unwind.

---

## Unwind Integration (I3)

Leaving any block that introduced PARAM bindings must restore previous bindings. Ensure that:

- Exceptions (try/finally, raise) unwind the dyn chain and thus pop `KOS_DYN_PARAM` nodes.
- `AWAIT`/`YIELD` boundary exits do not leave TLS or dyn chain inconsistent; the context’s chain is preserved across suspend/resume.
- Continuation transfer plans splice the captured chain (which includes PARAM nodes) so bindings are reinstated on apply.

---

## Implementation Plan (Step-by-Step)

### Step 1 — TLS stacks and static API

- Implement TLS: `tls.ctx_stack` (stack of `KosContext*`).
- API: `KosContext* kos_ctx_current(void)` returns top or NULL.
- In `koskari_eval(ctx)`: push ctx on entry; pop on every exit path (RETURN/RAISE/YIELD/AWAIT).
- Add debug assertions: `kos_ctx_current() != NULL` during bytecode execution.

### Step 2 — KosDomain object

- Add `ctx->domain` (Python object or C struct with PyObject header). V1: empty shell plus optional version counter.
- Domain lifecycle: when creating a new context from Python, default = inherit domain from active ctx if TLS non-empty; else create fresh domain.

### Step 3 — ParamRef type and interning

- Implement ParamRef (Python heap type; Ref subclass or Ref protocol).
- Fields: key, default_value, (optional) owner for interning.
- `paramref_intern(owner, key)` returns canonical ParamRef for that key in owner.
- `ref_get(ParamRef)`: use `kos_ctx_current()`, walk dyn chain for `KOS_DYN_PARAM`, resolve binding else default.
- `ref_set!(ParamRef, v)`: set default_value (recommended).

### Step 4 — Add PARAM dyn node kind

- Add `KOS_DYN_PARAM` to dyn node kinds.
- Node stores: count; `ParamRef** params` and `PyObject** values` (or array of pairs).
- Lookup helper: `int kos_dyn_param_lookup(KosDynNode* node, ParamRef* p, PyObject** out)`.

### Step 5 — Unwind integration

- Confirm exceptions, AWAIT/YIELD boundary exits, and continuation transfer plans restore/unwind the dyn chain so PARAM extents are correct.

### Step 6 — Opcodes

- Implement `PARAM`, `PUSH_PARAM`, `POP_PARAM` as above.
- Update opcode tables, design docs (e.g. OPCODES.md), assembler/disassembler.

### Step 7 — Compiler lowering and continuations

- Lower `use-param` and `parameterize` as above.
- Ensure continuation capture/apply includes PARAM nodes (dyn chain slice). Add test: capture inside parameterize, exit parameterize, apply continuation and confirm bindings reinstated; test multi-shot.

---

## Testing Matrix (Required)

1. **Nesting restore**: Outer binds X=8, inner binds X=16; verify correct values and restore.
2. **Exception restore**: Parameterize binds X=16 then raises; after catching, X resolves to prior/default.
3. **Cross-language re-entry**: Koskari parameterizes X then calls Python; Python calls back into Koskari; inner sees same X binding by default.
4. **Async interleave**: ctxA binds X then awaits; ctxB runs on same OS thread with different domain/context and sees default/different binding; ctxA resumes and sees its binding intact.
5. **Continuation capture/apply**: Capture inside parameterize; apply later; binding reinstated; multi-shot applies repeatedly.

---

## Completion Criteria

- TLS stack and `kos_ctx_current()` are correct and do not leak across `AWAIT`.
- ParamRef resolves current bindings reliably using only the active context/dyn chain.
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
