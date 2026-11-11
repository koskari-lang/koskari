/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


#ifndef KOSKARI_EVALUATED_H
#define KOSKARI_EVALUATED_H


#include <Python.h>

#include "eval.h"


/**
   KosEvaluatedTypeObject: the extended type object for native types
   the evaluator dispatches to directly.

   Each in-eval interface family is a suite embedded inline, mirroring
   CPython's tp_as_* design without the indirection: the call suite
   here, the iter suite once it exists. Membership in a family is by its
   defining slot (a callable has kc_vinstall), not by a
   pointer being non-NULL.

   Detection is a marker-base subtype check against KosEvaluatedType,
   whose tp_basicsize is sizeof(PyObject), so adopting it constrains no
   instance layout. Suite reads resolve through the first static type
   in the tp_base chain: a heap subclass of an adopter carries no
   suites of its own.

   The marker's metatype is KosEvaluatedMetaType, and PyType_Ready
   hands it on to every adopter, so a heap subclass of an adopter is
   built by it and laid out as a KosEvaluatedHeapTypeObject. Its
   kos_call_kind records, at class creation, whether the subclass
   overrides the static type's __call__: INHERIT (zero, the type's
   zero-filled default) resolves to the static type's suite; KOS (a
   KosCallable __call__) resolves to an override suite whose install
   runs that __call__ in the calling evaluator; PY (any other) resolves
   to an empty suite, with no install, so the instance is a plain
   Python callable called through its tp_call. The metatype's tp_new
   classifies a new type and its tp_setattro reclassifies a type and
   its subclasses when __call__ is set or deleted. Two limits: a
   second base whose metaclass does not derive from this one raises
   CPython's metaclass conflict, and type.__setattr__ called on the
   type directly bypasses the hook, so the type keeps its last kind.
*/


/**
   Install functions receive borrowed references: callable, the argv
   vector, and kwnames must not be DECREF'd by the implementation.
   argv holds nargs positionals followed by one value per kwnames
   entry (PEP 590 shape); tuple+dict callers spread onto this shape
   through KosArgSpread.

   ctx is the context of the running evaluator.

   The install owns its frame and reports what it did, one of:

   - RETURN(val): the call completed; val is a new reference, not yet
     settled (it may be an awaitable); nothing was pushed and no frame
     was touched. The caller settles or delivers it.
   - NORMAL / POLL: a callee frame is installed and current, pushed by
     the install (KosContext_InstallCallFrame), and the call's result
     arrives by that frame's return.
   - REPROLOGUE / AWAIT: the install pushed a code-less frame and set
     a resume on it (KosContext_CallInto); the resume delivers.
   - RAISE: error, PyErr set.

   tail_call: 1 if this is a tail call; 0 otherwise. An install that
   takes a frame reuses the caller's frame for a tail call
   (KosContext_InstallCallFrame does this); one that forwards the
   call to another callable passes tail_call through unchanged; one
   that completes in hand ignores it, and the RETURN that follows every
   TAIL_CALL retires the caller's frame. An install never pops or
   resets a frame on its own.
*/
typedef KosOpResult (*KosVectorInstallFunc)(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


/**
   The call suite: the in-eval call interface plus the Python-boundary
   workers. A type whose suite carries kc_vinstall is a KosCallable.

   kc_pycall is the Python-boundary worker: how this callable is invoked
   from outside any running eval, returning whatever its surface dictates
   (a Coroutine or AsyncGenerator for async-surface types, a completed
   value for demand-surface types). kc_pyvectorcall is its optional
   vector-optimized variant. Koskari_AsyncCall and the KosCallAdapt_*
   fillers dispatch through these; no caller needs per-type knowledge.
*/
typedef struct KosCallSlots {
  KosVectorInstallFunc kc_vinstall;       /* the install entry */
  ternaryfunc kc_pycall;                  /* Python-boundary call worker */
  vectorcallfunc kc_pyvectorcall;         /* vector variant; may be NULL */
} KosCallSlots;


/**
   Outcomes of an in-eval iterator step or throw.

   VALUE and EXHAUSTED complete synchronously; the site proceeds (push,
   branch to else pc, default, StopIteration per site rules). The
   INSTALLED pair means the provider spliced work onto the consumer
   context; the site re-enters the prologue and the result is delivered
   later to the site's resume. ERROR leaves PyErr set; on any
   non-INSTALLED outcome the provider has fully undone its own partial
   state. A provider whose step needs its own suspensions or
   sub-iteration runs them under a CPS resume frame (the resume
   contract is in eval.h); the suite itself never suspends.
*/
typedef enum {
  KOS_ITER_VALUE = 0,   /* *out: the element (new ref) */
  KOS_ITER_EXHAUSTED,   /* *out: return payload (new ref) or NULL */
  KOS_ITER_INSTALLED,   /* spliced own frames; deferred delivery */
  KOS_ITER_INSTALLED_VALUE, /* spliced a frame whose return IS the
                               element, delivered to the site's resume
                               as a frame return */
  KOS_ITER_ERROR,       /* PyErr set; *out untouched */
} KosIterStepResult;


typedef KosIterStepResult (*KosIterStepFunc)(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,        /* borrowed; NULL for plain next. A
                                  PROTOCOL send only (a generator's
                                  sent value) */
    PyObject **out);

typedef KosIterStepResult (*KosIterThrowFunc)(
    KosContext *ctx,
    PyObject *self,
    PyObject *throw_typ,       /* borrowed */
    PyObject *throw_exc,       /* borrowed */
    PyObject **out);

/**
   Demand-path close with GeneratorExit semantics; 0 ok, -1 with PyErr
   set. Callable outside any eval (dealloc, aclose adapters).
*/
typedef int (*KosIterCloseFunc)(PyObject *self);

/**
   Completion bookkeeping after an INSTALLED step delivers: clear busy
   state; exhausted also marks the provider closed
*/
typedef void (*KosIterFinishFunc)(PyObject *self, int exhausted);

/**
   The provider's durable Python-boundary context, when it owns one:
   context-level state (parameterize extents, the in-flight await
   iterator) lives across steps there, and every Python adapter step
   of the provider runs on it. A NULL slot means the adapter uses a
   context of its own. A NULL return is a failure with the error set,
   and the step fails: a provider with a durable context is never
   stepped on a context it cannot name, since its state (a generator's
   GEN node, for one) may be held by that context's chain.
*/
typedef struct KosContext *(*KosIterPyCtxFunc)(PyObject *self);

/**
   Is a step in flight? 1 with the provider's error set when
   one is, 0 otherwise. method names the Python method the refusal
   answers ("anext", "athrow", "aclose") when the caller is one, so
   the provider can word it as that method's; NULL otherwise.
*/
typedef int (*KosIterBusyFunc)(PyObject *self, const char *method);


/**
   The iter suite: the in-eval iteration interface. A type whose suite
   carries ki_step is a KosIter provider; the next, for-each step and
   yield-from resumes dispatch it for flattened stepping, and the
   delivery paths report completion through ki_finish. The Python
   adapters (iterate.c) drive ki_step against the ki_pyctx context.

   ki_busy returns 1 with the provider's error set when a step
   is in flight, 0 otherwise; a NULL slot means the provider is never
   busy. The Python adapters ask it before they begin a drive on the
   ki_pyctx context, which a drive in flight may share; ki_step and
   ki_throw still refuse a busy provider themselves, for the opcode
   sites that step on the running context.
*/
typedef struct KosIterSlots {
  KosIterStepFunc ki_step;
  KosIterThrowFunc ki_throw;
  KosIterCloseFunc ki_close;
  KosIterFinishFunc ki_finish;
  KosIterPyCtxFunc ki_pyctx;
  KosIterBusyFunc ki_busy;
  /* the durable context holds no state between completed steps; the
     adapters recycle it at their reset points like an owned context */
  uint8_t ki_pyctx_recyclable;
} KosIterSlots;


/* A step of iter, a provider with suite islots, is over: tell the
   provider, exhausted when the step was its end. A provider that
   recycles its durable context re-derives exhaustion from its source
   and stays open (a mapped function's StopIteration is a truncation
   that builtins.map resumes from), so the end is withheld from it. */
static inline void kos_iter_finish(
    PyObject *iter,
    KosIterSlots *islots,
    int exhausted) {

  islots->ki_finish(iter, exhausted && ! islots->ki_pyctx_recyclable);
}


/**
   The await suite: how the evaluator takes an awaitable of this type
   onto the running chain instead of driving it through its Python
   protocol. An await is a native frame whose resume is ka_install:
   the settle installs the frame, uncounted against the frame limit,
   and delivers the owned awaitable to the kind as it is, with no
   state (the kind does not settle). The work the kind installs is
   what the limit counts, and a kind takes its awaitable before any
   refusal, so a refused await leaves the awaitable finished. The
   kind runs as that frame's resume and completes the frame the way a
   resume on a native frame does: a value in hand returns from the
   frame (RESULT_FRAME_RETURN); work it stands for replaces the frame
   (kos_dyn_swap_frame_for_tail) or sets a resume of its own on it and
   makes its request; an awaitable already in flight hands itself to
   kos_await_python, the Python-protocol drive on the same frame. Any
   reference the kind owes is released before it tails. A failed
   delivery, NULL with the error pending, reaches the kind when a poll
   between the settle and the hop took a signal and dropped the staged
   awaitable; the kind declines it.
   ka_implicit_plain says an implicit site takes the value unawaited,
   as a Defer handle is; only an explicit await installs it.
*/
typedef struct KosAwaitSlots {
  const KosResumeKind *ka_install;
  uint8_t ka_implicit_plain;
} KosAwaitSlots;


typedef struct {
  PyTypeObject type;
  KosCallSlots kos_call;
  KosIterSlots kos_iter;
  KosAwaitSlots kos_await;
} KosEvaluatedTypeObject;


extern KosEvaluatedTypeObject KosEvaluatedType;


/**
   How a heap subclass of an adopter is called, recorded by the
   metatype. INHERIT is zero: the static type's suite applies.
*/
typedef enum KosCallKind {
  KOS_CALL_KIND_INHERIT = 0,
  KOS_CALL_KIND_KOS,      /* __call__ is a KosCallable */
  KOS_CALL_KIND_PY,       /* __call__ is any other override */
} KosCallKind;


/**
   The layout KosEvaluatedMetaType gives a heap type: CPython's heap
   type followed by the recorded call kind. The field holds no
   references.
*/
typedef struct {
  PyHeapTypeObject ht;
  uint8_t kos_call_kind;
} KosEvaluatedHeapTypeObject;


extern PyTypeObject KosEvaluatedMetaType;


/**
   The suites for KOS_CALL_KIND_KOS and KOS_CALL_KIND_PY, indexed by
   kind - 1. KOS carries only kc_vinstall; the Python boundary calls the
   instance through its tp_call. PY is empty: no install, a plain
   Python callable.
*/
extern KosCallSlots kos_call_override_suites[2];


#define KOS_EVALUATED_PYTYPE(typeobj) (&((typeobj).type))

/**
   Is the type an evaluated one? Answered by walking tp_base, which
   reaches the evaluated base whenever it is a base at all. The marker
   type is not a base type and is not exported, so a class reaches it
   only through a static adopter; that adopter is the solid base of
   any class built on it, so it is the class's tp_base, and a second
   base with a layout of its own is refused as a layout conflict.
   KosEvaluated_Resolve walks the same chain and relies on the same
   facts. A plain builtin's walk is two loads and two compares, so
   the test serves the per-element paths that classify an iterator.
*/
static inline int KosEvaluated_CheckType(PyTypeObject *t) {

  PyTypeObject *const base = KOS_EVALUATED_PYTYPE(KosEvaluatedType);

  for (; t; t = t->tp_base) {
    if (t == base)
      return 1;
  }
  return 0;
}

#define KosEvaluated_Check(obj)                                         \
  ((obj) && KosEvaluated_CheckType(Py_TYPE(obj)))


/**
   Resolve a type to its extended type object: the type itself when
   static, else the first static type in its tp_base chain. Caller must
   have established KosEvaluated_Check on the instance; the walk body
   never runs for the static types on the hot paths.
*/
static inline KosEvaluatedTypeObject *KosEvaluated_Resolve(PyTypeObject *t) {

  while (t && (t->tp_flags & Py_TPFLAGS_HEAPTYPE))
    t = t->tp_base;
  return (KosEvaluatedTypeObject *) t;
}


/**
   Resolve a type to its call suite. A static type's is its own. A heap
   type's is the override suite its recorded kind names, or for
   INHERIT the suite of the first static type in its tp_base chain.
   The caller has established membership (KosEvaluated_CheckType), so
   the walk ends on a static type.
*/
static inline KosCallSlots *KosEvaluated_CallSlots(PyTypeObject *t) {

  if (unlikely(t->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
    uint8_t kind = ((KosEvaluatedHeapTypeObject *) t)->kos_call_kind;

    if (kind)
      return &kos_call_override_suites[kind - 1];
    do
      t = t->tp_base;
    while (t->tp_flags & Py_TPFLAGS_HEAPTYPE);
  }
  return &((KosEvaluatedTypeObject *) t)->kos_call;
}


/**
   Check-and-resolve in one: when obj is a KosIter provider (a
   KosEvaluated whose suite carries a step slot), store the resolved
   suite into *out_slots and return 1; otherwise return 0 with
   *out_slots untouched.
*/
static inline int KosIter_CheckSlots(
    PyObject *obj,
    KosIterSlots **out_slots) {

  if (! obj || ! KosEvaluated_Check(obj))
    return 0;

  KosEvaluatedTypeObject *et = KosEvaluated_Resolve(Py_TYPE(obj));
  if (! et || ! et->kos_iter.ki_step)
    return 0;

  *out_slots = &et->kos_iter;
  return 1;
}


static inline int kos_iter_check(PyObject *obj) {

  KosIterSlots *slots;
  return KosIter_CheckSlots(obj, &slots);
}


/**
   Check-and-resolve in one: when obj is an awaitable with an await
   suite (a KosEvaluated whose suite carries ka_install),
   store the resolved suite into *out_slots and return 1; otherwise
   return 0 with *out_slots untouched.
*/
static inline int KosAwait_CheckSlots(
    PyObject *obj,
    KosAwaitSlots **out_slots) {

  if (! obj || ! KosEvaluated_Check(obj))
    return 0;

  KosEvaluatedTypeObject *et = KosEvaluated_Resolve(Py_TYPE(obj));
  if (! et || ! et->kos_await.ka_install)
    return 0;

  *out_slots = &et->kos_await;
  return 1;
}


/**
   Bind iter into req for a next request, classified for the fetch
   (kos_iter_fetch): KOS_IREQ_KIND_PROVIDER with islots its suite,
   else islots NULL and PY_SYNC for a type with tp_iternext, PY_ASYNC
   for one with am_anext, NONE for an object that is no iterator. Sync
   is tried first: a python object answering both protocols is a sync
   iterator here, as Python's next takes it, where the for-each bind
   (kos_dyn_iter_request_bind_iter) prefers its async protocol. For a
   request on the stack of one next entry, and for a record that holds
   its source for many elements and so classifies it once. iter is
   borrowed; the record's holder owns the reference.
*/
static inline void kos_iter_request_bind_next(
    KosIterRequest *req,
    PyObject *iter) {

  req->iter = iter;
  req->send = NULL;
  if (KosIter_CheckSlots(iter, &req->islots)) {
    req->kind = KOS_IREQ_KIND_PROVIDER;
    return;
  }
  req->islots = NULL;
  if (! iter)
    req->kind = KOS_IREQ_KIND_NONE;
  else if (PyIter_Check(iter))
    req->kind = KOS_IREQ_KIND_PY_SYNC;
  else if (Py_TYPE(iter)->tp_as_async && Py_TYPE(iter)->tp_as_async->am_anext)
    req->kind = KOS_IREQ_KIND_PY_ASYNC;
  else
    req->kind = KOS_IREQ_KIND_NONE;
}


/**
   Whether kind and islots are an answer either bind gives for iter
   (kos_iter_request_bind_next, kos_dyn_iter_request_bind_iter): the
   kind's protocol is one iter has, and islots is its suite exactly
   when it is a provider. The consistency test behind a fetch under
   KOS_EVAL_SAFETYNET.
*/
static inline int kos_iter_kind_fits(
    PyObject *iter,
    uint8_t kind,
    KosIterSlots *islots) {

  KosIterSlots *fresh = NULL;
  int provider = KosIter_CheckSlots(iter, &fresh);
  PyTypeObject *tp;

  if (islots != (provider ? fresh : NULL))
    return 0;
  switch (kind) {
    case KOS_IREQ_KIND_PROVIDER:
      return provider;
    case KOS_IREQ_KIND_PY_SYNC:
      return ! provider && iter && PyIter_Check(iter);
    case KOS_IREQ_KIND_PY_ASYNC:
      tp = iter ? Py_TYPE(iter) : NULL;
      return ! provider && tp && tp->tp_as_async && tp->tp_as_async->am_anext;
    case KOS_IREQ_KIND_NONE:
      tp = iter ? Py_TYPE(iter) : NULL;
      return ! provider && ! (iter && PyIter_Check(iter))
          && ! (tp && tp->tp_as_async && tp->tp_as_async->am_anext);
    default:
      return 0;
  }
}

#define KosIter_Check(obj) kos_iter_check(obj)


/**
   One step of a provider through its suite, with no request record:
   ki_step, with a StopIteration ending the step read as the
   provider's exhaustion, the provider finished and the exception's
   value in *out as the end's payload. The step entries with a record
   and the ones without share it. send_val borrowed; *out a new ref on
   VALUE and EXHAUSTED.
*/
KosIterStepResult kos_iter_step_provider(
    struct KosContext *ctx,
    PyObject *iter,
    KosIterSlots *islots,
    PyObject *send_val,
    PyObject **out);


/**
   kos_iter_step_provider's twin for a throw: ki_throw, with a
   StopIteration ending the throw read as exhaustion the same way. The
   pair is borrowed.
*/
KosIterStepResult kos_iter_throw_provider(
    struct KosContext *ctx,
    PyObject *iter,
    KosIterSlots *islots,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out);


/**
   A sync step returned NULL: a StopIteration or a clean NULL is
   exhaustion, any other error propagates. had_next is 0 when the type
   lost its tp_iternext since it was classified. Out of line: the cold
   end of kos_iter_next_sync.
*/
KosIterRequestStatus kos_iter_next_sync_ended(int had_next);


/**
   One step of a sync iterator through tp_iternext alone, as CPython's
   FOR_ITER makes it: DELIVERED with *out the element, else the cold
   end's answer, EXHAUSTED or ERROR with the error set. The caller has
   classified iter as KOS_IREQ_KIND_PY_SYNC. The one sync step every
   fetch makes, from a bound request or a provider's own.
*/
KOS_OP_INLINE KosIterRequestStatus kos_iter_next_sync(
    PyObject *iter,
    PyObject **out) {

  iternextfunc next = Py_TYPE(iter)->tp_iternext;

  *out = likely(next) ? next(iter) : NULL;
  if (likely(*out))
    return KOS_IREQ_DELIVERED;
  return kos_iter_next_sync_ended(next != NULL);
}


/**
   Request the next element of the bound iterator req describes from a
   provider's step, which runs outside the drive and so cannot defer
   in place. A plain python iterator answers now: DELIVERED with *out
   the element (new ref), EXHAUSTED, or ERROR with PyErr set. Any
   other kind is deferred: a code-less frame carrying resume and state
   is installed (through cache, as KosResumeFrame_Install takes it) and
   CONTINUES is returned, so the step reports its work installed and
   resume makes the request through KosContext_NextResumeBound once
   the drive reaches the frame. ERROR with nothing installed when the
   frame cannot be. The one place a provider asks for an element from
   its step; the kind was decided when the source was bound, never by
   the provider.
*/
KOS_OP_INLINE KosIterRequestStatus KosContext_NextOrDefer(
    KosContext *ctx,
    const KosIterRequest *req,
    const KosResumeKind *resume,
    void *state,
    KosDynFrame **cache,
    PyObject **out) {

  *out = NULL;

  if (likely(req->kind == KOS_IREQ_KIND_PY_SYNC))
    return kos_iter_next_sync(req->iter, out);

  if (! KosResumeFrame_Install(ctx, resume, state, cache))
    return KOS_IREQ_ERROR;
  return KOS_IREQ_CONTINUES;
}


int koskari_evaluated_init(PyObject *mod);
int koskari_evaluated_clear(PyObject *mod);


#endif /* KOSKARI_EVALUATED_H */


// The end.
