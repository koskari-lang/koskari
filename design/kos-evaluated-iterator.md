# KosEvaluated type and the in-eval iterator interface

Status: draft
Scope: new `KosEvaluatedTypeObject` unifying the call and iter in-eval
interfaces; migration of `KosPyAsyncGen` and the iteration opcodes onto
the iter suite; `KosMap` as the second iter provider.

## Motivation

The evaluator has a detectable in-eval interface for calls
(`KosCallableTypeObject.tp_kos_install`) but none for iteration. Every
site that flattens a generator into the running eval dispatches on the
concrete type instead:

- `opcode/ITER_NEXT.c:71`, `opcode/OBJECT_NEXT.c:48`,
  `opcode/YIELD_FROM.c:33` -- step dispatch
- `eval/eval_utils.c:1243` (`kos_iter_acquire`) -- iterator acquisition
  passthrough
- `dynchain/dyn_foreach.c:140,158`, `dynchain/dyn_next.c:92,115`,
  `dynchain/dyn_walk.c:538,622`, `dynchain/dyn_boundary.c:145,164` --
  completion bookkeeping (`in_progress` reset, `closed` marking),
  downcasting to `KosPyAsyncGen`

Any new native iterable that wants in-eval stepping (awaits passing
through the consumer's eval, no intermediary coroutines) has no surface
to adopt. The generator's flattening privilege is hardcoded to its type.

## The extended type

One extended type object, suite pointers per interface family, mirroring
CPython's `tp_as_*` design. Lives in a new unit `evaluated.h/c`.

```c
typedef struct {
  KosInstallFunc ki_install;              /* tuple+dict; may be NULL */
  KosVectorInstallFunc ki_vinstall;       /* vectorcall; may be NULL */
  int ki_wants_frame;                     /* default when fn is NULL */
  KosCallableWantsFrameFunc ki_wants_frame_fn;
} KosCallSlots;

typedef struct {
  KosIterStepFunc ki_step;
  KosIterThrowFunc ki_throw;
  KosIterCloseFunc ki_close;
  KosIterFinishFunc ki_finish;
} KosIterSlots;

typedef struct {
  PyTypeObject type;
  KosCallSlots *tp_kos_call;   /* NULL: not in-eval callable */
  KosIterSlots *tp_kos_iter;   /* NULL: not in-eval iterable */
} KosEvaluatedTypeObject;

extern KosEvaluatedTypeObject KosEvaluatedType;   /* abstract root */
```

Detection is the existing marker-base pattern: one subtype check against
the root, then a null test on the suite pointer. The root's
`tp_basicsize` is `sizeof(PyObject)`, so adopting it constrains no
instance layout.

```c
#define KosEvaluated_Check(obj) \
  (PyType_IsSubtype(Py_TYPE(obj), &KosEvaluatedType.type))

#define KosIter_Check(obj) \
  (KosEvaluated_Check(obj) && KosEvaluated_ITER_SLOTS(Py_TYPE(obj)))
```

### Slot resolution and heap subclasses

Today's call dispatch casts `Py_TYPE(callable)` to
`KosCallableTypeObject` directly (`callable.c:107,120`,
`utils.c:243`). A Python-level subclass of a `Py_TPFLAGS_BASETYPE`
adopter (KosPartial allows this) produces a plain `PyHeapTypeObject`;
the cast then reads past the end of `PyTypeObject` into heap-type
fields. The subtype check passes and the read is garbage. This hazard is
pre-existing; the migration is the moment to close it.

`KosEvaluated_ITER_SLOTS` / `KosEvaluated_CALL_SLOTS` resolve through
the first static type in the `tp_base` chain:

```c
static inline KosEvaluatedTypeObject *kos_evaluated_resolve(PyTypeObject *t) {
  while (t && (t->tp_flags & Py_TPFLAGS_HEAPTYPE))
    t = t->tp_base;
  return (KosEvaluatedTypeObject *) t;   /* caller checked subtype */
}
```

For the exact static types on the hot paths the loop body never runs.

## Iter suite signatures

```c
typedef enum {
  KOS_ITER_VALUE,       /* *out set: the element (new ref) */
  KOS_ITER_EXHAUSTED,   /* *out: return payload (new ref) or NULL */
  KOS_ITER_AWAIT,       /* *out: awaitable (new ref); site parks on it */
  KOS_ITER_INSTALLED,   /* work spliced into consumer eval; delivery
                           arrives later through the site's dyn node */
  KOS_ITER_ERROR,       /* PyErr set; *out untouched */
} KosIterStepResult;

typedef KosIterStepResult (*KosIterStepFunc)(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,        /* borrowed; NULL for plain next */
    PyObject **out);

typedef KosIterStepResult (*KosIterThrowFunc)(
    KosEvalState *es,
    PyObject *self,
    PyObject *throw_typ,       /* borrowed */
    PyObject *throw_exc,       /* borrowed */
    PyObject **out);

typedef int (*KosIterCloseFunc)(PyObject *self);
    /* demand-path close, GeneratorExit semantics; 0 ok, -1 PyErr.
       Callable outside any eval (dealloc, aclose adapters). */

typedef void (*KosIterFinishFunc)(PyObject *self, int exhausted);
    /* completion bookkeeping after an INSTALLED step delivers:
       clear busy state; exhausted also marks the provider closed.
       Replaces the KosPyAsyncGen downcasts in dynchain. */
```

Contract:

- The caller (opcode site) arms its own boundary state before calling
  `ki_step` -- FOREACH phase, NEXT boundary node, YIELD_FROM node --
  exactly as it does today. The provider never touches site state.
- `KOS_ITER_VALUE` / `KOS_ITER_EXHAUSTED` complete synchronously; the
  site disarms and proceeds (push, branch to else pc, default, raise
  StopIteration per site rules).
- `KOS_ITER_AWAIT` parks the site on a Python awaitable and promises
  re-entry: the site returns `EVAL_AWAIT(*out)` with its boundary
  armed, and on revive calls `ki_step(es, self, send_val=result)`
  again -- the provider's own phase state knows what the value is. An
  exception from the parked awaitable re-enters through `ki_throw`, so
  the provider mediates its own conversions (a mid-fetch
  StopAsyncIteration becomes `KOS_ITER_EXHAUSTED` inside the map, not
  at the site's raise walk). Sites therefore carry a phase
  discriminator in their boundary nodes: the legacy awaited-value-is-
  the-element phase for plain Python async iterators, and a
  provider-resume phase that feeds the value back into the suite.
- `KOS_ITER_INSTALLED` means the provider spliced work (frames, its own
  dyn node) onto the consumer context; the site returns `EVAL_NORMAL`
  and the result is delivered later at the site's dyn node by the
  return/raise walks. `ki_finish` is called at that delivery.
- Re-entrancy guarding (`in_progress` today) is the provider's own
  business, inside `ki_step` / `ki_throw`.

The step-may-complete-later shape is not new design; it is the current
agen contract (`iter_next_step_kos_agen` returns `EVAL_NORMAL` after
attach, delivery happens in `kos_dyn_foreach_deliver_yield_kos_agen`)
promoted from hardcoded type dispatch to an interface.

## Migration: call suite

Mechanical re-expression, no behavior change.

1. Add `evaluated.h/c`: structs above, abstract root type, resolution
   helpers. Root registers before `callable` in module init order.
2. `KosCallableTypeObject` becomes `KosEvaluatedTypeObject` +
   `KosCallSlots`. Adopters (`callable.c`, `func.c/h`, `partial.c/h`,
   `demand.c/h`, `cont.c/h`, `object.c`, `values.c/h`) convert their
   static initializers: the four flat fields move into a
   `static KosCallSlots`, referenced from `tp_kos_call`.
3. `KosCallable_Check` becomes subtype-of-root plus call-suite null
   test. Slot readers (`callable.c:74-120`, `utils.c:243`,
   `eval/eval_utils.c` call paths) go through the resolving accessors.
4. `tp_base` values are unchanged where adopters already chain to
   `KosCallableType`; `KosCallableType` itself chains to the new root
   (or is renamed to it -- decide during implementation, alias the old
   name either way).

## Migration: agen onto the iter suite

`KosPyAsyncGenType` becomes a `KosEvaluatedTypeObject` with
`tp_kos_iter` set and `tp_base` the root. Instance struct unchanged.

Slot implementations factor code that already exists:

- `ki_step`: the guard + attach sequence currently duplicated per site.
  Closed/no-ctx returns `KOS_ITER_EXHAUSTED`; `in_progress` returns
  `KOS_ITER_ERROR` (RuntimeError); otherwise set `in_progress`, run
  `attach_agen_gen_checked` (`eval.c:1454`), return
  `KOS_ITER_INSTALLED`. `send_val`, when present, replaces the install
  placeholder on TOS (the YIELD_FROM send path today,
  `yield_from_drive_kos_async_gen`). Attach failure: reset
  `in_progress`, return `KOS_ITER_ERROR`; the site runs its own undo,
  as the three `op_attach_..._for_*` variants do today.
- `ki_throw`: set the pending exception, attach, `KOS_ITER_INSTALLED`
  (the YF throw ingress pattern).
- `ki_close`: `KosPyAsyncGen_Teardown` / `asyncgen_close_drive`
  (iter.c), already demand-path and exception-fenced.
- `ki_finish`: `in_progress = 0; if (exhausted) closed = 1;` -- the two
  lines every dynchain delivery site currently writes by downcast.

`attach_agen_gen_checked` and the flatten helpers move from `eval.c`
into the agen's unit (they become provider implementation detail); the
per-site undo helpers stay with the sites.

## Migration: opcode and dynchain sites

Each site swaps `KosPyAsyncGen_Check` for `KosIter_Check` and its
inlined agen logic for slot dispatch. Fallback for non-providers is the
existing CPython-protocol path, unchanged.

- `ITER_NEXT` (`opcode/ITER_NEXT.c`): provider branch arms the FOREACH
  phase (`KOS_FOREACH_ITER_STEP_AWAITING_KOS_AGEN`, renamed
  `..._AWAITING_KOS_ITER`), calls `ki_step`, maps VALUE -> push,
  EXHAUSTED -> exhausted+else-pc, AWAIT -> `EVAL_AWAIT` with
  `..._AWAITING_PY` phase, INSTALLED -> `EVAL_NORMAL`, ERROR -> undo
  phase + `EVAL_RAISE`.
- `OBJECT_NEXT` core (`object_next_step`, shared by VAR_NEXT/FAST_NEXT):
  same mapping; EXHAUSTED honors the n==2 default before StopIteration.
  The NEXT boundary push precedes the step call, as today.
- `YIELD_FROM`: `yield_from_classify` gains `KosIter_Check` as the
  first (flattening-preference) case, replacing the agen check; the
  drive function calls `ki_step` / `ki_throw` / `ki_close` for
  send/throw/close actions. `KOS_YF_KOS_ASYNC_GEN` kind renames to
  `KOS_YF_KOS_ITER`.
- `kos_iter_acquire` (`eval/eval_utils.c:1243`): passthrough condition
  becomes `KosIter_Check`.
- Dynchain delivery (`dyn_foreach.c`, `dyn_next.c`, `dyn_walk.c`):
  replace the `KosPyAsyncGen` downcasts with
  `KosIter_Check(obj) -> ki_finish(obj, exhausted)`.
- `dyn_boundary.c:145,164`: the is-this-a-kos-iter-step vs
  python-awaitable discrimination becomes `KosIter_Check`.
- `dyn_walk.c:65` (GEN node owner walk) stays generator-specific: GEN
  nodes belong to gen-backed providers; other providers bring their own
  node types with their own walk handlers.

Order of work: call-suite phases land first (pure refactor, full suite
green), then agen slots + site dispatch in one change (behavior-neutral,
the corpus and phase 1-8 suites are the check), then adapters, then
KosMap.

## Python-surface adapters (reusable fillers, not base behavior)

The extended type supplies nothing Python-facing. A provider builds its
own `PyAsyncMethods` / method table from exported adapter functions, so
the work of a correct `__anext__` awaitable is written once:

```c
PyObject *KosIterAdapt_Anext(PyObject *self);           /* am_anext */
PyObject *KosIterAdapt_Asend(PyObject *self, PyObject *v);
PyObject *KosIterAdapt_Athrow(PyObject *self, PyObject *args);
PyObject *KosIterAdapt_Aclose(PyObject *self, PyObject *ignored);
PyObject *KosIterAdapt_DemandIter(PyObject *self);      /* tp_iter */

PyObject *KosCallAdapt_CallAsync(                       /* tp_call */
    PyObject *self, PyObject *args, PyObject *kwds);
PyObject *KosCallAdapt_CallDemand(                      /* tp_call */
    PyObject *self, PyObject *args, PyObject *kwds);

PyObject *KosCallAdapt_VectorAsync(                     /* vectorcallfunc */
    PyObject *self, PyObject *const *args, size_t nargsf,
    PyObject *kwnames);
PyObject *KosCallAdapt_VectorDemand(                    /* vectorcallfunc */
    PyObject *self, PyObject *const *args, size_t nargsf,
    PyObject *kwnames);
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

The two `KosCallAdapt_*` entries do the same for the call suite,
generalizing the two `tp_call` behaviors that exist:

- `KosCallAdapt_CallAsync` is the KosFunction `tp_call` shape: prepare
  a frame on a fresh context, dispatch the call suite's install into
  it, and wrap the context for the Python caller -- `KosCoroutine`, or
  the async-generator wrapper when the installed code yields (the
  `KosCoroutine_NewFromCode` branch). Going through `ki_install`
  rather than code-plus-namespace directly means any call-suite
  provider gets it, partial chains included, with the install-slot
  flattening intact.
- `KosCallAdapt_CallDemand` is the Demand `tp_call` shape:
  `demand_sync_ex` generalized -- install onto a demand-flagged root
  frame, drive to completion, `KoskariSyncViolation` on real
  suspension, yielding callables returning a DemandGenerator (which
  becomes `KosIterAdapt_DemandIter` over the resulting provider).

The `Vector*` pair carries the same two behaviors as `vectorcallfunc`
implementations, dispatching `ki_vinstall` when the suite provides it
and falling back to tuple+dict `ki_install` otherwise. The wiring
convention is the one `KosFunction` already implements (func.c:2250):
a per-instance `vectorcallfunc` field, `tp_vectorcall_offset` naming
it, `Py_TPFLAGS_HAVE_VECTORCALL`, the field assigned at construction.

A call-suite provider fills its Python call surface with whichever
pair matches: async-by-default types take `CallAsync` /
`VectorAsync`, demand-surface types take `CallDemand` /
`VectorDemand`. A type wiring the vector pair sets `tp_call` to
`PyVectorcall_Call` so tuple+dict calls route through the same path;
a type skipping vectorcall wires the `Call*` adapter directly. The
existing function, expression, and partial objects migrate onto these
during the call-suite phase.
- `KosPyAsyncGen` then swaps its hand-rolled `am_anext` / `asend` /
  `athrow` / `aclose` / `tp_iter` entries for the adapters, and
  `KosPyAsyncGenASend` / `KosDemandGenerator` become the shared stepper
  types rather than agen-private ones.

Adapter migration can trail the opcode migration; the agen's existing
Python surface keeps working against its own machinery until the swap.

## KosMap

The goal object: `(map f seq)` as a native in-eval iterable. Duck-typed,
no `builtins.map` inheritance. A `KosEvaluatedTypeObject` with
`tp_kos_iter` (and `tp_kos_call` NULL).

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

- Whether `KosCallableType` survives as an intermediate base or the
  root replaces it outright; pure naming, decide at implementation.
- The AWAIT re-entry contract needs the boundary revive path
  (`revive_await` election, `dyn_boundary` discrimination) to
  distinguish value-is-element parks from provider-resume parks;
  verify the phase discriminator covers the YIELD_FROM node the same
  way as FOREACH/NEXT.
- The mid-delivery `EVAL_AWAIT` escape for S3xF2 (see Edges): confirm
  the delivery walk call sites can propagate a KosOpResult.
