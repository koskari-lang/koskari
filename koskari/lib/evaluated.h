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
   defining slot (a callable has ki_install or ki_vinstall), not by a
   pointer being non-NULL.

   Detection is a marker-base subtype check against KosEvaluatedType,
   whose tp_basicsize is sizeof(PyObject), so adopting it constrains no
   instance layout. Suite reads resolve through the first static type
   in the tp_base chain: a Python-level subclass of an adopter is a
   plain PyHeapTypeObject without the extended fields, and reading past
   its PyTypeObject would be garbage.
*/


/**
   Install functions receive borrowed references. callable, args_tuple,
   kwds_dict (or args, kwnames for vector) must not be DECREF'd by the
   implementation.

   es must be the same live KosEvalState that KosEvalState_Call* prepared via
   do_frame_prep. Use KosEvalState_CONTEXT(es) when the heap context is needed.

   tail_call: 1 if this is a tail call; 0 otherwise. Most implementations
   ignore this; frame behavior is handled by the caller (KosEvalState_Call*).
*/
typedef KosOpResult (*KosInstallFunc)(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call);


typedef KosOpResult (*KosVectorInstallFunc)(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call);


/**
   Optional per-instance wants_frame. If non-NULL, called with the callable
   instance; return 1 if the call needs a frame (push or reset), 0 if not.
   Overrides ki_wants_frame when present.
*/
typedef int (*KosCallableWantsFrameFunc)(PyObject *callable);


/**
   The call suite: the in-eval call interface plus the Python-boundary
   workers. A type whose suite carries ki_install or ki_vinstall is a
   KosCallable.

   ki_pycall is the Python-boundary worker: how this callable is invoked
   from outside any running eval, returning whatever its surface dictates
   (a Coroutine or AsyncGenerator for async-surface types, a completed
   value for demand-surface types). ki_pyvectorcall is its optional
   vector-optimized variant. Koskari_AsyncCall and the KosCallAdapt_*
   fillers dispatch through these; no caller needs per-type knowledge.
*/
typedef struct KosCallSlots {
  KosInstallFunc ki_install;              /* tuple+dict; may be NULL */
  KosVectorInstallFunc ki_vinstall;       /* vectorcall; may be NULL */
  int ki_wants_frame;                     /* default when fn is NULL */
  KosCallableWantsFrameFunc ki_wants_frame_fn;  /* if non-NULL, overrides */
  ternaryfunc ki_pycall;                  /* Python-boundary call worker */
  vectorcallfunc ki_pyvectorcall;         /* vector variant; may be NULL */
} KosCallSlots;


/**
   Outcomes of an in-eval iterator step or throw.

   VALUE and EXHAUSTED complete synchronously; the site proceeds (push,
   branch to else pc, default, StopIteration per site rules). AWAIT
   parks the site on a Python awaitable with its boundary armed.
   INSTALLED means the provider spliced its frames onto the consumer
   context; the site returns EVAL_NORMAL and the result is delivered
   later at the site's dyn node by the return/raise walks, which call
   ki_finish. ERROR leaves PyErr set; on any non-INSTALLED outcome the
   provider has fully undone its own partial state.
*/
typedef enum {
  KOS_ITER_VALUE = 0,   /* *out: the element (new ref) */
  KOS_ITER_EXHAUSTED,   /* *out: return payload (new ref) or NULL */
  KOS_ITER_AWAIT,       /* *out: awaitable (new ref); site parks */
  KOS_ITER_INSTALLED,   /* spliced into consumer eval; deferred delivery */
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
} KosIterSlots;


typedef struct {
  PyTypeObject type;
  KosCallSlots kos_call;
  KosIterSlots kos_iter;
} KosEvaluatedTypeObject;


extern KosEvaluatedTypeObject KosEvaluatedType;


#define KOS_EVALUATED_PYTYPE(typeobj) (&((typeobj).type))

#define KosEvaluated_Check(obj)                                         \
  ((obj) && PyType_IsSubtype(Py_TYPE(obj),                              \
                             KOS_EVALUATED_PYTYPE(KosEvaluatedType)))


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


int koskari_evaluated_init(PyObject *mod);
int koskari_evaluated_clear(PyObject *mod);


#endif /* KOSKARI_EVALUATED_H */


// The end.
