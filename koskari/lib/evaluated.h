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
   here, the iter suite when it lands. Membership in a family is by its
   defining slot (a callable has kc_vinstall), not by a
   pointer being non-NULL.

   Detection is a marker-base subtype check against KosEvaluatedType,
   whose tp_basicsize is sizeof(PyObject), so adopting it constrains no
   instance layout. Suite reads resolve through the first static type
   in the tp_base chain: a Python-level subclass of an adopter is a
   plain PyHeapTypeObject without the extended fields, and reading past
   its PyTypeObject would be garbage.
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
     the install (KosEvalState_InstallCallFrame), and the call's result
     arrives by that frame's return.
   - REPROLOGUE / AWAIT: the install pushed a code-less frame and set
     a resume on it (KosEvalState_CallInto); the resume delivers.
   - RAISE: error, PyErr set.

   tail_call: 1 if this is a tail call; 0 otherwise. An install that
   takes a frame reuses the caller's frame for a tail call
   (KosEvalState_InstallCallFrame does this); one that forwards the
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
   later at the site's iteration node. ERROR leaves PyErr set; on any
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
                               element: the site sets its iteration node's
                               request to value mode */
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
   The provider's durable Python-boundary drive context, when it owns
   one: context-level state (parameterize extents, the in-flight await
   iterator) lives across steps there. NULL slot or NULL return means
   the adapter owns its own host context.
*/
typedef struct KosContext *(*KosIterPyCtxFunc)(PyObject *self);


/**
   The iter suite: the in-eval iteration interface. A type whose suite
   carries ki_step is a KosIter provider; NEXT, ITER_NEXT, and
   YIELD_FROM dispatch it for flattened stepping, and the dynchain
   delivery walks report completion through ki_finish. The Python
   adapters (iterate.c) drive ki_step against the ki_pyctx context.
*/
typedef struct KosIterSlots {
  KosIterStepFunc ki_step;
  KosIterThrowFunc ki_throw;
  KosIterCloseFunc ki_close;
  KosIterFinishFunc ki_finish;
  KosIterPyCtxFunc ki_pyctx;
  uint8_t ki_pyctx_recyclable;  /* the durable context holds no state
                                   between completed steps; the adapters
                                   recycle it at their reset points
                                   exactly like an owned host */
} KosIterSlots;


typedef struct {
  PyTypeObject type;
  KosCallSlots kos_call;
  KosIterSlots kos_iter;
} KosEvaluatedTypeObject;


extern KosEvaluatedTypeObject KosEvaluatedType;


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


static inline KosCallSlots *KosEvaluated_CallSlots(PyTypeObject *t) {

  KosEvaluatedTypeObject *et = KosEvaluated_Resolve(t);
  return et ? &et->kos_call : NULL;
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

#define KosIter_Check(obj) kos_iter_check(obj)


/**
   An acquired iterator that steps through tp_iternext alone, with no
   evaluator involvement: a plain sync iterator that is not a KosIter
   provider. Anything else -- a provider, a python async iterator --
   must be driven through the evaluator.
*/
static inline int KosIter_CheckSync(PyObject *iter) {

  return ! KosIter_Check(iter) && PyIter_Check(iter);
}


/**
   The plain-iterator arm the request entries share: a python iterator
   that is no provider and has tp_iternext answers through it alone,
   with no iteration node, a StopIteration read as exhaustion. Returns
   1 when iter is such an iterator, with *status set and *out the
   element on DELIVERED; 0 when iter needs the node. Inline, so a
   provider's per-element fetch is the type test and the call.
*/
KOS_OP_INLINE int kos_iter_next_plain(
    PyObject *iter,
    PyObject **out,
    KosIterRequestStatus *status) {

  if (! KosIter_CheckSync(iter))
    return 0;

  *out = Py_TYPE(iter)->tp_iternext(iter);
  if (likely(*out)) {
    *status = KOS_IREQ_DELIVERED;
    return 1;
  }

  if (PyErr_Occurred()) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration)) {
      *status = KOS_IREQ_ERROR;
      return 1;
    }
    PyErr_Clear();
  }
  *status = KOS_IREQ_EXHAUSTED;
  return 1;
}


/**
   Request the next element of iter from a provider's step, which
   runs outside the drive and so cannot push an iteration node. A
   plain python iterator answers now: DELIVERED with *out the element
   (new ref), EXHAUSTED, or ERROR with PyErr set. Any other kind is
   deferred: a code-less frame carrying resume and state is installed
   (through cache, as KosResumeFrame_Install takes it) and CONTINUES is
   returned, so the step reports its work installed and resume makes
   the request through KosEvalState_NextResume once the drive reaches
   the frame. ERROR with nothing installed when the frame cannot be.
   The one place a provider asks for an element from its step; the
   kind of the source is decided here, never by the provider.
*/
KOS_OP_INLINE KosIterRequestStatus KosEvalState_NextOrDefer(
    KosContext *ctx,
    PyObject *iter,
    const KosResumeKind *resume,
    void *state,
    KosDynFrame **cache,
    PyObject **out) {

  KosIterRequestStatus status;

  *out = NULL;

  if (kos_iter_next_plain(iter, out, &status))
    return status;

  if (! KosResumeFrame_Install(ctx, resume, state, cache))
    return KOS_IREQ_ERROR;
  return KOS_IREQ_CONTINUES;
}


int koskari_evaluated_init(PyObject *mod);
int koskari_evaluated_clear(PyObject *mod);


#endif /* KOSKARI_EVALUATED_H */


// The end.
