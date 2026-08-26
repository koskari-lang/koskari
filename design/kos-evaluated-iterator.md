# KosEvaluated type and the in-eval iterator interface

Status: substrate, call suite, agen iter suite, and site dispatch are
landed; Python-surface iter adapters and KosMap remain.
Scope: `KosEvaluatedTypeObject` unifying the call and iter in-eval
interfaces; migration of `KosPyAsyncGen` and the iteration opcodes onto
the iter suite; `KosMap` as the second iter provider.

## Motivation

The evaluator had a detectable in-eval interface for calls
(`tp_kos_install`) but none for iteration: every site that flattened a
generator into the running eval dispatched on the concrete
`KosPyAsyncGen` type -- the step sites (`ITER_NEXT`, `OBJECT_NEXT`,
`YIELD_FROM`), the acquisition passthrough (`kos_iter_acquire`), and
the dynchain completion bookkeeping, which downcast to reset
`in_progress` and mark `closed`. A new native iterable wanting in-eval
stepping (awaits passing through the consumer's eval, no intermediary
coroutines) had no surface to adopt.

## The extended type (landed)

One extended type object with the interface suites embedded inline,
CPython's `tp_as_*` family design without the pointer hop. Lives in
`evaluated.h/c`; the abstract root `_KosEvaluated` has
`tp_basicsize == sizeof(PyObject)`, so adopting it constrains no
instance layout.

```c
typedef struct KosCallSlots {
  KosInstallFunc ki_install;              /* tuple+dict; may be NULL */
  KosVectorInstallFunc ki_vinstall;       /* vectorcall; may be NULL */
  int ki_wants_frame;                     /* default when fn is NULL */
  KosCallableWantsFrameFunc ki_wants_frame_fn;
  ternaryfunc ki_pycall;                  /* Python-boundary call worker */
  vectorcallfunc ki_pyvectorcall;         /* vector variant; may be NULL */
} KosCallSlots;

typedef struct KosIterSlots {
  KosIterStepFunc ki_step;
  KosIterThrowFunc ki_throw;
  KosIterCloseFunc ki_close;
  KosIterFinishFunc ki_finish;
} KosIterSlots;

typedef struct {
  PyTypeObject type;
  KosCallSlots kos_call;
  KosIterSlots kos_iter;
} KosEvaluatedTypeObject;
```

Membership in a family is by its defining slot, not pointer presence: a
callable carries `ki_install` or `ki_vinstall`; an iter provider
carries `ki_step`. The root's zeroed suites make it neither.

Detection and resolution:

- `KosEvaluated_Check`: subtype test against the root.
- `KosEvaluated_Resolve`: walk `tp_base` past heap types to the first
  static type. A Python-level subclass of a `BASETYPE` adopter is a
  plain `PyHeapTypeObject` without the extended fields; before the
  migration, call dispatch cast `Py_TYPE()` directly and read garbage
  for such subclasses. The loop body never runs for the static types on
  the hot paths.
- `KosCallable_CheckSlots(obj, &slots)` / `KosIter_CheckSlots(obj,
  &slots)`: check-and-resolve in one, storing the suite out-param on
  success. The single resolve serves the whole call sequence:
  `KosEvalState_Call*` thread the resolved suite through
  `do_frame_prep` and the `KosCallable_CallSlots` /
  `KosCallable_CallArgsSlots` dispatch cores, one resolve where the
  pre-interface code did one direct cast and the first migration cut
  did three resolves.

## Call suite (landed)

All ten adopters (Function, Expression, BoundMethod, KosPartial,
Demand, Procedure, both continuations, Values, Apply) embed their suite
inline in the type initializer and chain to the root; the old
`_KosCallable` root and `KosCallableTypeObject` are gone. The
duplicated wants-frame resolvers collapsed onto
`KosCallable_WantsFrame` / `KosCallSlots_WantsFrame`.

`ki_pycall` / `ki_pyvectorcall` are the Python-boundary workers: how
the callable is invoked from outside any running eval, returning
whatever its surface dictates (Coroutine or AsyncGenerator for
async-surface types, a completed value for demand-surface types). Each
type fills them with the worker it already had -- `KosFunction_Call` /
`KosFunction_VectorCall`, `expression_pycall`, `boundmethod_pycall`
(prepend self, recurse through the generic entry), `KosPartial_Call`
(merge, follow inner through the generic entry), the demand adapters
for Demand, `procedure_tp_call`, and so on.

The Python-slot adapters are pure suite dispatch with no per-type
knowledge:

```c
PyObject *Koskari_AsyncCall(callable, args, kwds);
    /* resolve suite -> ki_pycall; PyObject_Call fallback */
PyObject *KosCallAdapt_CallAsync(self, args, kwds);      /* tp_call */
PyObject *KosCallAdapt_VectorAsync(self, args, nargsf, kwnames);
    /* ki_pyvectorcall when present, else tuple+dict -> AsyncCall */
PyObject *KosCallAdapt_CallDemand(self, args, kwds);
    /* Koskari_DemandSync: forced demand surface */
PyObject *KosCallAdapt_VectorDemand(self, args, nargsf, kwnames);
```

Function, Expression, BoundMethod, and KosPartial wire the async pair
into `tp_call` / the per-instance `vectorcall` field
(`PyVectorcall_Call` convention per func.c); Demand wires CallDemand.
Subtype overrides need no special casing -- Demand-wins and Procedure's
result discard fall out of each type's own `ki_pycall`, and heap
subclasses resolve to the right suite.

`Koskari_DemandSync` (`demand_sync_ex`) retains its internal type
switch; re-expressing it over the suite is open work.

## Iter suite (landed for the generator)

```c
typedef enum {
  KOS_ITER_VALUE,       /* *out: the element (new ref) */
  KOS_ITER_EXHAUSTED,   /* *out: return payload (new ref) or NULL */
  KOS_ITER_AWAIT,       /* *out: awaitable (new ref); site parks */
  KOS_ITER_INSTALLED,   /* spliced into consumer eval; deferred delivery */
  KOS_ITER_ERROR,       /* PyErr set; *out untouched */
} KosIterStepResult;

KosIterStepResult ki_step(es, self, send_val /* borrowed */, &out);
KosIterStepResult ki_throw(es, self, throw_typ, throw_exc, &out);
int ki_close(self);        /* demand close, GeneratorExit semantics;
                              callable outside any eval */
void ki_finish(self, exhausted);  /* completion bookkeeping after an
                              INSTALLED delivery */
```

Contract:

- The site arms its own boundary state before calling `ki_step` --
  FOREACH phase, NEXT boundary node, YIELD_FROM node -- and undoes it
  on any non-INSTALLED, non-AWAIT outcome. The provider never touches
  site state.
- `KOS_ITER_VALUE` / `KOS_ITER_EXHAUSTED` complete synchronously; the
  site disarms and proceeds (push, branch to else pc, default, raise
  StopIteration per site rules).
- `KOS_ITER_AWAIT` parks the site on a Python awaitable and promises
  re-entry: on revive the site calls `ki_step(es, self,
  send_val=result)` again -- the provider's own phase state knows what
  the value is. An exception from the parked awaitable re-enters
  through `ki_throw`, so the provider mediates its own conversions (a
  mid-fetch StopAsyncIteration becomes `KOS_ITER_EXHAUSTED` inside the
  map, not at the site's raise walk). The sites already return
  `EVAL_AWAIT` on this result; the resume-side phase discriminator
  (provider-resume vs legacy awaited-value-is-the-element) is NOT yet
  built -- it lands with the first provider that returns AWAIT.
- `KOS_ITER_INSTALLED` means the provider spliced its frames onto the
  consumer context; the site returns `EVAL_NORMAL` and the result is
  delivered later at the site's dyn node by the return/raise walks,
  which call `ki_finish`.
- On any non-INSTALLED outcome the provider has fully undone its own
  partial state; re-entrancy guarding (`in_progress`) is the provider's
  business.

`KosPyAsyncGen` adopts (iter.c): the attach core moved in from eval.c
as `agen_iter_drive` (guards, busy-mark, `agen_attach_gen_flatten`
graft, send-val placeholder swap), `ki_throw` as the exception-primed
variant of the same drive, `ki_close` over `KosPyAsyncGen_Teardown`,
`ki_finish` as the two-line completion. eval.c's three `op_attach_*`
wrappers and shared attach helpers are deleted.

Site dispatch: `ITER_NEXT` and `OBJECT_NEXT` (shared VAR/FAST core) arm
their boundary and map all five step results; `YIELD_FROM`'s drive is
`yield_from_drive_kos_iter` over `ki_step`/`ki_throw` with the kind
renamed `KOS_YF_KOS_ITER` (VALUE/AWAIT are a SystemError placeholder
there until non-frame providers exist); `kos_iter_acquire` passes
providers through; both `dyn_boundary` discriminations, the six
dynchain completion downcasts, the eval.c fatal-abort site, and the
yield-from unwind close-delegate hook all go through `KosIter_Check` /
`ki_finish`. `dyn_walk.c`'s GEN owner walk stays generator-specific:
GEN nodes belong to gen-backed providers; other providers bring their
own node types. No dispatch path names the concrete agen type; the
remaining `KosPyAsyncGen` internals are its own asend/demand-gen
machinery, replaced by the adapters below.

Unifications made during the swap (deliberate, not drift): `started`
is set on any in-eval drive (was YF-only); dead no-chain states report
EXHAUSTED instead of RuntimeError (YF's quiet-pop semantics
generalized); YF attach-failure closure happens at node unwind via
`ki_finish` instead of a direct `closed` write; OBJECT_NEXT pushes its
boundary before the guards and aborts it on non-INSTALLED; an
inflight-send-val leak in the YF drive is fixed.

## Python-surface iter adapters (planned)

The extended type supplies nothing Python-facing. A provider builds its
own `PyAsyncMethods` / method table from exported adapter functions, so
the work of a correct `__anext__` awaitable is written once:

```c
PyObject *KosIterAdapt_Anext(PyObject *self);           /* am_anext */
PyObject *KosIterAdapt_Asend(PyObject *self, PyObject *v);
PyObject *KosIterAdapt_Athrow(PyObject *self, PyObject *args);
PyObject *KosIterAdapt_Aclose(PyObject *self, PyObject *ignored);
PyObject *KosIterAdapt_DemandIter(PyObject *self);      /* tp_iter */
```

Implementation generalizes the two drivers that exist. The async
adapters share one stepper object -- the `KosPyAsyncGenASend` machinery
(iter.c) with its agen-specific calls replaced by suite dispatch, and
the same priming shape (send_value, throw pair, closing flag). The
adapter is a site like any opcode site: it calls `ki_step` /
`ki_throw` directly and renders the results into the Python protocol
instead of stack pushes and PC branches. Its drive:

1. Mid-await resume: forward `send()` into the in-flight awaitable
   iterator (the `coroutine_send` WAITING_PY pattern); StopIteration's
   value becomes `send_val` for re-entry, failure feeds `ki_throw`.
2. `ki_step(es, provider, send_val, &out)`:
   - VALUE: `StopIteration(out)` -- the anext awaitable completes with
     no `koskari_eval` entry. Map's S1xF1 fast path through the async
     surface costs one stepper allocation and zero eval.
   - EXHAUSTED: `StopAsyncIteration` (payload rules), `ki_finish(1)`.
   - AWAIT: hand `out` to the event loop; the re-entry contract works
     identically Python-side (resume is step 1 feeding step 2).
   - INSTALLED: the graft needs a host chain. The stepper owns a
     minimal `KosContext`, allocated lazily on first INSTALLED (never
     for the other results), and drives `koskari_eval(ctx)`.
3. `KosOpResult` translation (only after INSTALLED) -- today's
   `asyncgenasend_iternext` switch de-agen-ified: YIELD ->
   `StopIteration(value)` + `ki_finish(0)`; RETURN ->
   `StopAsyncIteration` payload mapping + `ki_finish(1)`; AWAIT ->
   forward to loop, resume re-drives eval; RAISE -> propagate, with
   the closing-mode mapping for aclose.

This puts a requirement on `KOS_DYN_MAP_STEP`: its DELIVER must elect
like a gen yield -- through the parent site boundary when one is armed
above it, and surfacing as `KOS_RES_YIELD` to the driver when the node
sits at the chain root (the stepper-hosted case, no consumer frame).
The generator already behaves this way; MAP_STEP mirrors it.

`KosIterAdapt_DemandIter` generalizes `KosDemandGenerator` (demand.c):
the same drive with the sync rendering. VALUE returns the element;
AWAIT denies as `KoskariSyncViolation` at the adapter (a non-frame
park has no frame to carry the demand bit); INSTALLED forces
`KOS_FRAME_DEMAND` on `frame_top` post-graft (today's
`demand_gen_step` move) so in-eval suspension denies at the existing
deny sites; YIELD -> value, RETURN -> `StopIteration(payload)`.

`KosPyAsyncGen` then swaps its hand-rolled `am_anext` / `asend` /
`athrow` / `aclose` / `tp_iter` entries for the adapters, and
`KosPyAsyncGenASend` / `KosDemandGenerator` become the shared stepper
types rather than agen-private ones.

## KosMap (planned)

The goal object: `(map f seq)` as a native in-eval iterable. Duck-typed,
no `builtins.map` inheritance. A `KosEvaluatedTypeObject` with the iter
suite filled (call suite zeroed).

Instance state:

```c
typedef struct {
  PyObject_HEAD
  PyObject *func;
  PyObject *source;      /* acquired iterator (kos_iter_acquire) */
  int phase;             /* IDLE / FETCHING / INVOKING / CLOSED */
  int busy;              /* re-entrancy guard, ki_finish clears */
} KosMapObject;
```

`ki_step` is a state machine over two sub-operations, FETCH (pull the
next source element) and INVOKE (apply func). Every source/func
combination resolves to one of three mechanisms:

- direct: both sub-operations complete synchronously; `KOS_ITER_VALUE`,
  evaluator untouched.
- park/re-enter: a Python awaitable suspends (a foreign `__anext__` in
  FETCH, an async-def coroutine result in INVOKE, after the eager-step
  gate). Map records its phase, returns `KOS_ITER_AWAIT`; the awaited
  result re-enters `ki_step` as `send_val` per the contract above and
  the phase field routes it (FETCHING: it is the element, go to
  INVOKE; INVOKING: it is the mapped value, VALUE).
- graft: koskari execution is needed (func has a call suite, or the
  source is a gen-backed provider whose step is INSTALLED). Map pushes
  its `KOS_DYN_MAP_STEP` node, installs the frames above it, returns
  `KOS_ITER_INSTALLED`.

The matrix. Source shapes: S1 sync Python iterator, S2 Python async
iterator, S3 KosIter provider. Func shapes: F1 sync Python callable,
F2 Python callable returning an awaitable (per-result discovery at the
awaitability gate, as in `push_or_await_plain`), F3 KosCallable.

|        | F1 sync py       | F2 async py             | F3 koskari       |
|--------|------------------|-------------------------|------------------|
| S1     | direct           | direct or park (INVOKE) | graft (INVOKE)   |
| S2     | direct or park   | park either phase,      | park (FETCH) +   |
|        | (FETCH)          | sequentially            | graft (INVOKE)   |
| S3     | VALUE substep:   | graft (FETCH) + park    | graft both:      |
|        | direct; gen:     | at MAP_STEP delivery    | gen frames ->    |
|        | graft (FETCH)    |                         | MAP_STEP -> fn   |

S3xF3 is the fully flattened case: source gen frames, MAP_STEP, func
frame on one chain, one eval, awaits pass anywhere, no intermediary
objects. S3 with another KosMap as source composes cell-by-cell: a
VALUE substep needs no graft, AWAIT re-entry chains through both phase
machines.

Fast path (the whole optimization): S1xF1 arms nothing, allocates
nothing, `KOS_ITER_VALUE` per element. `(map str.upper names)`.

`KOS_DYN_MAP_STEP` is a new dynchain node type participating in the
yield/return delivery election, so a grafted source gen's yield lands
on it (nearer on the chain than the site's boundary) rather than on
the consumer's site. Its handlers advance the phase machine: FETCH
delivery starts INVOKE (installing the func frame from within the
delivery, when func is koskari); INVOKE delivery pops the node and
routes the mapped value to the parent site's boundary through the
existing `deliver_yield` machinery plus `ki_finish`. Exception routing
runs the raise walk through the node like any extent node, so a throw
into the map reaches func frames and the source in the expected order.

Edges:

- StopIteration escaping the func frame would masquerade as source
  exhaustion at the site's raise-walk conversion. MAP_STEP's raise
  handler converts StopIteration/StopAsyncIteration escaping the
  INVOKE phase to RuntimeError (the PEP 479 rule generators follow).
- S3xF2: func's coroutine suspending during a MAP_STEP delivery walk
  means the delivery path must escape as `EVAL_AWAIT` mid-walk.
  Implementation-sensitive; the delivery handlers must be able to
  produce a KosOpResult, not only success/failure.
- Source `EXHAUSTED` payload (a gen's return value) forwards as map's
  `EXHAUSTED` payload.
- Sync/demand consumption of every cell: grafts inherit the demand bit
  at install (drive-state, as today), so koskari cells run under sync
  iteration until something genuinely suspends -- then
  `KoskariSyncViolation`, identical to generator behavior.
  Eager-completing S2/F2 steps pass; genuinely suspending ones deny at
  the adapter.

`ki_throw` forwards to the source when the source is a provider
(matching generator delegation semantics), else raises at the map
boundary. `ki_close` closes the source (provider `ki_close`, else
`close()` if present) and marks CLOSED. `ki_finish` clears `busy`, and
exhausted marks CLOSED.

n-ary `(map f a b c)` extends FETCH to a round-robin pull with
shortest-source termination; deferred until the unary machine is in.

## Open questions

- The AWAIT re-entry contract needs the boundary revive path
  (`revive_await` election, `dyn_boundary` discrimination) to
  distinguish value-is-element parks from provider-resume parks;
  verify the phase discriminator covers the YIELD_FROM node the same
  way as FOREACH/NEXT. Lands with the first AWAIT-returning provider.
- The mid-delivery `EVAL_AWAIT` escape for S3xF2 (see Edges): confirm
  the delivery walk call sites can propagate a KosOpResult.
- Re-expressing `demand_sync_ex`'s type switch over the call suite.
