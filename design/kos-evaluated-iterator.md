# KosEvaluated type and the in-eval iteration interface

Status: fully landed -- substrate, call suite, iter suite, the
KosIterRequest sidecar, site dispatch, the native-frame typed wake,
KosMap as a native frame program, and the Python adapters. The
sections below describe the code as built; "History" records the
scattered first cut this replaced and the defects that motivated the
redesign.

## Motivation

The evaluator had a detectable in-eval interface for calls but none for
iteration: every site that flattened a generator into the running eval
dispatched on the concrete `KosPyAsyncGen` type. A new native iterable
wanting in-eval stepping (awaits passing through the consumer's eval,
no intermediary coroutines) had no surface to adopt. The iter suite
supplied that surface, and KosMap proved it; what the first cut got
wrong is where the in-flight step state lives, and the redesign below
fixes that.

## The extended type (landed)

One extended type object with the interface suites embedded inline,
CPython's `tp_as_*` family design without the pointer hop. Lives in
`evaluated.h/c`; the abstract root `_KosEvaluated` has
`tp_basicsize == sizeof(PyObject)`, so adopting it constrains no
instance layout.

```c
typedef struct {
  PyTypeObject type;
  KosCallSlots kos_call;
  KosIterSlots kos_iter;
} KosEvaluatedTypeObject;
```

Membership in a family is by its defining slot, not pointer presence: a
callable carries an install slot; an iter provider carries a step slot.
The root's zeroed suites make it neither.

Detection and resolution:

- `KosEvaluated_Check`: subtype test against the root.
- `KosEvaluated_Resolve`: walk `tp_base` past heap types to the first
  static type, so Python-level subclasses of adopters resolve to the
  static suite instead of reading garbage past a heap type.
- `KosCallable_CheckSlots(obj, &slots)` / `KosIter_CheckSlots(obj,
  &slots)`: check-and-resolve in one, storing the suite out-param on
  success; one resolve serves the whole dispatch sequence.

Planned: a spare `tp_flags` user bit stamped on adopting static types,
making the membership test O(1) and the resolve walk the only walk.

## Call suite (landed)

All ten adopters (Function, Expression, BoundMethod, KosPartial,
Demand, Procedure, both continuations, Values, Apply) embed their suite
inline and chain to the root. `ki_pycall` / `ki_pyvectorcall` are the
Python-boundary workers; `Koskari_AsyncCall` and the `KosCallAdapt_*`
fillers dispatch through them with no per-type knowledge.

The call-suite slots carry the `kc_` prefix (`kc_install`,
`kc_vinstall`, `kc_wants_frame`, `kc_wants_frame_fn`, `kc_pycall`,
`kc_pyvectorcall`); `ki_` is the iter-suite prefix.

Known open work, independent of the iteration redesign: collapsing the
dual install entries onto a single vector-shaped install with a central
tuple spread, and re-expressing `demand_sync_ex`'s type switch over the
suite.

## History: the scattered first cut

The first landing worked -- the full matrix was green under
test/asan/valgrind/gdb -- but the knowledge of "what does this
in-flight step mean" existed in six cooperating copies:

1. per-site mode encodings: the FOREACH `iter_step_phase`, the NEXT
   `resume` byte, the YIELD_FROM phase values;
2. the `dyn_boundary` predicates (`resume_park`, `value_mode`,
   `awaiting_py`) that decode those encodings, with overlapping truth
   sets whose correctness depends on caller check-order;
3. the eval delivery forks (yield event, return-at-frame Cases 2/2b,
   the revive-await fork, the raise walk's boundary block) consulting
   the predicates;
4. `kos_eval_boundary_feed` translating deliveries into suite entries;
5. per-provider routing flags (map's `fetch_parked`, `fetch_grafted`,
   `fetch_graft_mode`) re-disambiguating what a `ki_resume` payload
   means;
6. the Python adapters' `drive_mode` plus the `KOS_CTX_GEN_ROOT_RETURN`
   context flag re-deriving the same distinctions at the chain root.

Every review defect was a disagreement between two copies:

- D1: `kos_dyn_iter_boundary_iter_obj` has no YIELD_FROM arm, so a
  mediated provider under `yield-from` raises "mediated boundary lost
  its iter provider" (reproduced).
- D2: the same NULL reaches the raise walk's value-mode cleanup, so an
  error escaping a YF value graft skips `ki_finish` and wedges the
  provider busy.
- D3: `agen_iter_drive`'s frozen send-swap error paths return with
  `in_progress` still set, wedging the generator.

The redesign below removed the code they lived in; all three defects
have regression tests (`tests/reproducers/test_yield_from_mediated.py`
for D1/D2; D3's window closed with `agen_iter_drive`'s single error
exit).

## The architecture as built: request down, complete up, typed wake

One architecture sentence: a requestor asks for the next element
through one API; a completer reports its outcome through one API; in
between, one sidecar record owns all in-flight state and delivers
typed wake events. Opcode sites, native impls, and the Python adapters
are all requestors with the same three words.

### The request record (dynchain.h)

```c
typedef struct KosIterRequest {
  PyObject *iter;       /* stepped iterator; strong ref while armed */
  uint8_t mode;         /* KOS_IREQ_* */
} KosIterRequest;
```

Embedded in the FOREACH, NEXT, and YIELD_FROM union arms, replacing
`iter_step_phase`, the NEXT `resume` byte, and the YF mediation
phases. The iterator lives in exactly one field, read through
`kos_dyn_iter_request(node)`; arming and mode changes go through
`kos_dyn_iter_request_arm` / `_set_mode`, which keep the FOREACH
boundary capability in lockstep.

Modes (sidecar-private): `IDLE`, `INSTALLED` (provider frames in-eval),
`AWAITING_PY` (parked python async step; the awaited completion IS the
element), `GRAFT_VALUE` (the next frame return below is the element).
There is no mediated mode: composition happens by node stacking, not
by feeding a provider (see KosMap below).

### Status and event vocabularies

```c
typedef enum {
  KOS_IREQ_DELIVERED,   /* *fast_out: the element (new ref) */
  KOS_IREQ_EXHAUSTED,   /* *fast_out: end payload (new ref) or NULL */
  KOS_IREQ_CONTINUES,   /* work installed in-eval; wake comes later */
  KOS_IREQ_EXIT,        /* suspension: propagate the eval result */
  KOS_IREQ_ERROR,       /* PyErr set */
} KosIterRequestStatus;

typedef enum {
  KOS_IWAKE_VALUE,      /* the requested element arrived */
  KOS_IWAKE_END,        /* the source is exhausted (payload or NULL) */
  KOS_IWAKE_EXCEPTION,  /* the request failed; PyErr set */
} KosIterWakeEvent;

typedef KosOpResult (*KosIterWakeFn)(
    KosEvalState *es, void *owner, KosIterWakeEvent ev, PyObject *value);
```

The wake sees only request completions. Awaited values, provider
parks, mediated source yields, and delegation traffic are the
sidecar's internal business; a requestor cannot observe them and
therefore cannot mis-route them.

### The request API (eval.h / eval.c)

```c
KosIterRequestStatus KosIterRequest_Next(
    KosEvalState *es, KosDynNode *node,
    PyObject *send_val /* borrowed */, PyObject **fast_out);

KosOpResult KosIterRequest_Deliver(
    KosEvalState *es, KosDynNode *node,
    KosIterWakeEvent ev, PyObject *value /* consumed */);
```

`Next` performs one three-protocol step against the node's armed
iterator: a KosIter provider through `ki_step` (arming INSTALLED
first, so mid-step deliveries can elect the boundary), a sync iterator
through `tp_iternext`, a python async iterator through its
eager-stepped `__anext__`. DELIVERED and EXHAUSTED complete
synchronously with no mode written beyond the provider arm; CONTINUES
means work is installed (the caller re-enters the prologue -- an
installed frame may be code-less and cannot be dispatched into from
stale registers); EXIT is a parked python async step (mode
AWAITING_PY; the payload goes to the event loop and the awaited
completion delivers as the element).

`Deliver` is the single entry the eval delivery points call: VALUE
runs the node's yield tail (stage the element / branch bookkeeping /
re-emit, plus `ki_finish` and disarm); END unwinds a completing gen
chain when one is installed and runs the exhaustion tail (else-pc
branch, default push, StopIteration re-emit per site arity,
`ki_finish(iter, 1)`). The delivery points -- the yield event, the
gen-return case, the value-graft frame-return case, and the
revive-await fork -- each reduce to one Deliver call. The raise walk's
exhaustion conversion keeps its single copy in `dyn_walk.c` +
`kos_dyn_handle` (the walk's control flow differs from the eval-side
mappings); `kos_dyn_iter_boundary_exhaustion_exc` reads the record
mode.

Completion needs no third entry: in-flight work reports upward through
the machinery that already exists. A value graft completes by
returning -- the frame's parent is the boundary armed GRAFT_VALUE, and
the return case Delivers the value. Exhaustion completes by raising
StopIteration and declining, and the consumer's boundary converts it;
this is also what gives a mapped function's StopIteration builtins.map
truncation parity for free.

### The native frame: typed wake and completion helper

```c
typedef enum KosNativeFrameEvent {
  KOS_NFRAME_ITERATE,     /* eval loop cadence; no value */
  KOS_NFRAME_CALL_VALUE,  /* a callee frame returned; value owned */
  KOS_NFRAME_AWAITED,     /* awaited resume delivered; value owned */
  KOS_NFRAME_EXCEPTION,   /* raise walk offers the pending PyErr */
} KosNativeFrameEvent;

typedef KosOpResult (*KosNativeHandlerFn)(
    KosEvalState *, KosNativeFrameEvent, PyObject *);
```

A native frame carries a pointer to a static per-program descriptor
plus a state pointer:

```c
typedef struct KosNativeProgram {
  const char *np_name;
  KosNativeHandlerFn np_handler;
  int (*np_clone_state)(void *state, void **out);
  void (*np_free_state)(void *state);
} KosNativeProgram;
```

A live frame's state is borrowed; its owner guarantees the lifetime
spans the frame's. Continuation capture across the frame uses
`np_clone_state` when the program provides one -- the copy owns the
cloned state (`KOS_FRAME_OWNS_NATIVE_STATE`) and frees it through
`np_free_state` -- and otherwise fails with an error naming
`np_name`. Apply provides a trivial clone and stays capturable; map
provides none, since replaying a captured step would re-consume
source elements. The raise walk offers EXCEPTION to a native frame
before tearing through it; a handler that declines returns
`EVAL_RAISE()` and the walk pops the frame.
`KosNativeFrame_Return(es, value)` performs the return unwind walk,
FINALLY/async-unwind outcomes, frame detach/free, and parent delivery;
apply uses it. Boundary deliveries that land while a native frame is
current arrive by stack push, and the entry-phase resume delivery
hands a revive value to the handler as AWAITED.

### Sites

ITER_NEXT routes every protocol through one `Next` call on the FOREACH
node (armed for the loop's lifetime at PUSH_FOREACH); OBJECT_NEXT
pushes its NEXT boundary for providers and parked async steps and
keeps the bare-iterator fast arm; YIELD_FROM's KOS_ITER drive is
`ki_step`/`ki_throw` plus record arming. CONTINUES always maps to
`EVAL_REPROLOGUE`. The per-kind delivery tails in
`dyn_next.c`/`dyn_foreach.c`/`dyn_yield_from.c` are the single-copy
wake implementations, dispatched by node kind from Deliver; the
record's `wake`/`owner` fields are reserved for requestors that are
not chain nodes.

### Providers

The suite is what a provider is:

- `ki_step(es, self, send_val, out)` -- one outcome: VALUE, EXHAUSTED,
  ERROR, INSTALLED (own frames, e.g. the generator's flatten attach),
  or INSTALLED_VALUE (a frame whose return is the element). The suite
  never parks and never returns awaitables; a provider whose step
  needs its own suspensions or sub-iteration runs them as a native
  frame program.
- `ki_throw(es, self, typ, exc, out)` -- exception ingress (a throw
  into a suspended generator is a real protocol operation).
- `ki_close(self)` -- demand close, GeneratorExit semantics.
- `ki_finish(self, exhausted)` -- completion bookkeeping, called from
  the delivery tails and the adapters' terminals.
- `ki_pyctx(self)` -- durable drive context when the provider owns one.

`ki_resume` is gone (nothing parks at the suite layer), and with it
the owner-only-resume contract, the mediated boundary mode, the feed
loop, and the whole KOS_ITER_AWAIT/INSTALLED_RESUME surface.

### KosMap as a native frame program

`KosMapObject` keeps `func`, `source`, `closed`, `busy`, and the frame
program's phase byte. `ki_step`'s fast path is pure C: sync source
element plus non-suspending function result completes as
`KOS_ITER_VALUE` with the evaluator untouched. Anything else stages
its progress (a fetched element, a pending awaitable) on a fresh
code-less native frame and returns `KOS_ITER_INSTALLED_VALUE`.

The handler is the two-phase machine:

1. FETCH: push a 1-arg NEXT boundary on the map's own frame and call
   `Next(source)`. DELIVERED goes straight to INVOKE; CONTINUES/EXIT
   leave the fetch in flight -- the element arrives by boundary
   delivery onto the frame stack (ITERATE pops it) or as AWAITED;
   exhaustion raises StopIteration for the consumer's boundary to
   convert.
2. INVOKE: a frame-wanting KosCallable grafts as a callee frame
   (CALL_VALUE wake); a python callable goes through
   `Koskari_AsyncCall`, with Defer/future handles ambient-await-exempt
   and awaitable results parked via `KosEvalState_PushOrAwait`.
3. Complete by `EVAL_RETURN(element)` -- the consumer's GRAFT_VALUE
   boundary delivers it as the step value and calls
   `ki_finish(map, 0)`.

Exceptions are always declined: the frame pops and the consumer's
boundary owns every conversion. While a step is in flight, `ki_throw`
refuses every second caller (RuntimeError, GeneratorExit included --
the rule CPython applies to a running async generator) and `ki_close`
declines silently, since it is reached from teardown paths where the
live drive owns the state. Nested maps need no forwarding code --
each level is one more frame + boundary pair on the chain, and the
walks already compose them.

### Python adapters

Provider parks no longer exist, so the stepper's park machinery
(park_iter, the eager-await helpers, the resume feed) is gone. Both
adapters share one drive shape: suite entry against the host (durable
`ki_pyctx` or adapter-owned), `koskari_eval` for installed work,
`stepper_resume_await` for mid-work awaits, and terminal rendering.
`drive_mode` survives as one bit: a value graft's root return is the
element; anything else's return is exhaustion. A StopIteration
surfacing at the host root is the provider's exhaustion (converted to
the surface protocol and `ki_finish(provider, 1)`); an adapter-owned
host is discarded after a root completion. The demand generator forces
`KOS_FRAME_DEMAND` onto installed frames so genuine suspension denies
in-eval.

## Invariants

Hard-won; violating any of these reproduces a fixed bug.

- EVAL_AWAIT payloads are pre-stepped: whatever escapes to the event
  loop has already had its first `send(None)`, with the in-flight
  iterator recorded in the context's `py_await_iter`. Handing a raw
  awaitable to the loop double-steps it.
- Generators freeze on yield: the gen chain detaches into `GEN.active`
  at every yield and re-splices on the next install. No machinery may
  assume a yielded source is still chained.
- A root frame return marks the context dead and leaves the frame in
  place; an adapter-owned host must be discarded after a root
  completion.
- After an install, a frame carrying bytecode (a generator attached)
  continues dispatch directly; a code-less native frame must take
  `EVAL_REPROLOGUE` -- dispatching into it reads a NULL code. The
  fresh-entry FAST-namespace check also tolerates a NULL code for the
  same reason.
- Demand extents deny genuine suspension via `KOS_FRAME_DEMAND`
  inherited at frame push; eager-completing awaitables pass.
- `do_frame_prep` and `kos_dyn_push_frame_empty` support an empty
  context (no current frame): native-frame installs happen at the
  chain root in adapter hosts.
- Never pass an out-param into the same call that computes it
  (argument evaluation order is unspecified); sequence the call and
  the use.
- Function-raised StopIteration truncates a map (builtins.map parity);
  source return payloads are discarded.

## Naming

`Source` left the API surface entirely. The call suite carries the
`kc_` prefix, the iter suite `ki_`. No initialism prefixes; enums
spell their namespace.

## Error semantics (settled)

An error from the source or the function propagates out of the map
like any other exception -- a demand-denial KoskariSyncViolation
included. The map does not destroy its inner state on error: it holds
its acquired iterator (obtained through the shared `kos_iter_acquire`,
the same acquisition PUSH_FOREACH and the *_ITER opcodes use), and
whether a later step can succeed is the source's own decision -- a
source built to recover may; one wedged mid-await surfaces its own
complaint. Cleanup happens when the map loses its final reference and
is destroyed, which releases the function and the iterator.

## Open questions

- The call-suite single-install collapse and the `tp_flags` membership
  bit: both independent of this arc, sequenced after it.
