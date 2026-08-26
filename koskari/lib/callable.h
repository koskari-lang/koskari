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


#ifndef KOSKARI_CALLABLE_H
#define KOSKARI_CALLABLE_H


#include <Python.h>

#include "eval.h"
#include "evaluated.h"


struct KosContext;


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
   The call suite: the in-eval call interface of a KosEvaluatedTypeObject,
   referenced from its tp_kos_call. A type carrying a non-NULL call suite is
   a KosCallable.
*/
struct KosCallSlots {
  KosInstallFunc ki_install;              /* tuple+dict; may be NULL */
  KosVectorInstallFunc ki_vinstall;       /* vectorcall; may be NULL */
  int ki_wants_frame;                     /* default when fn is NULL */
  KosCallableWantsFrameFunc ki_wants_frame_fn;  /* if non-NULL, overrides */
};

typedef struct KosCallSlots KosCallSlots;


#define KosCallable_Check(obj)                                          \
  ((obj) && KosEvaluated_Check(obj)                                     \
   && KosEvaluated_CallSlots(Py_TYPE(obj)) != NULL)


/**
   Effective wants_frame for a callable: the per-instance hook when the
   suite provides one, else the static flag. 0 when the type carries no
   call suite.
*/
static inline int KosCallable_WantsFrame(PyObject *callable) {

  KosCallSlots *slots = KosEvaluated_CallSlots(Py_TYPE(callable));

  if (! slots)
    return 0;
  if (slots->ki_wants_frame_fn)
    return slots->ki_wants_frame_fn(callable);
  return slots->ki_wants_frame;
}


/* === KosCallable_* : KosCallable-only invocation (no type check, no frame prep) === */

/**
   KosCallable_Call: invoke a KosCallable with vectorcall-style args.

   callable MUST be a KosCallable; no type check is performed. Caller is
   responsible for frame/stack state; this function only dispatches to the
   install slot. Prefers ki_vinstall when present; otherwise builds
   tuple+dict and uses ki_install.

   es must match the KosEvalState_Call* caller. callable, args, kwnames are
   borrowed.
*/
KosOpResult KosCallable_Call(KosEvalState *es,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call);

/**
   KosCallable_CallArgs: invoke a KosCallable with tuple+dict args.

   callable MUST be a KosCallable; no type check is performed. Caller is
   responsible for frame/stack state; this function only dispatches to
   ki_install.

   es must match the KosEvalState_Call* caller. callable, args_tuple, kwds_dict
   are borrowed.
*/
KosOpResult KosCallable_CallArgs(KosEvalState *es,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call);

/**
   KosCallable_CallNoArgs: invoke a KosCallable with no arguments.

   callable MUST be a KosCallable; no type check is performed. Caller is
   responsible for frame/stack state; this function only dispatches to
   ki_install with NULL args_tuple and NULL kwds_dict.

   es must match the KosEvalState_Call* caller. callable is borrowed.
*/
KosOpResult KosCallable_CallNoArgs(KosEvalState *es,
    PyObject *callable, int tail_call);


/* === Python-slot adapters === */

/**
   Invoke a callable from Python with the async surface: the call
   returns a Coroutine (or an AsyncGenerator for yielding code) rather
   than driving to completion. The async sibling of Koskari_DemandSync:
   one dispatcher over the koskari callable shapes (Demand keeps its
   demand surface, BoundMethod prepends self, Function and Expression
   bind and wrap their code, KosPartial merges and follows its inner
   callable), with PyObject_Call as the fallback for everything else.

   callable, args, kwds are borrowed.
*/
PyObject *Koskari_AsyncCall(
    PyObject *callable, PyObject *args, PyObject *kwds);

/**
   tp_call filler for an async-surface callable: Koskari_AsyncCall.
*/
PyObject *KosCallAdapt_CallAsync(
    PyObject *self, PyObject *args, PyObject *kwds);

/**
   vectorcallfunc filler with the same async semantics. An exact
   KosFunction takes the vector-optimized formals path; other callables
   build tuple+dict and defer to Koskari_AsyncCall.
*/
PyObject *KosCallAdapt_VectorAsync(
    PyObject *self, PyObject *const *args, size_t nargsf,
    PyObject *kwnames);

/**
   tp_call filler for a demand-surface callable: run self to completion on
   the demand path (Koskari_DemandSync). Real suspension raises
   KoskariSyncViolation; a yielding callable returns a DemandGenerator.
*/
PyObject *KosCallAdapt_CallDemand(
    PyObject *self, PyObject *args, PyObject *kwds);

/**
   vectorcallfunc filler with the same demand semantics; builds tuple+dict
   and defers to the demand path.
*/
PyObject *KosCallAdapt_VectorDemand(
    PyObject *self, PyObject *const *args, size_t nargsf,
    PyObject *kwnames);


int koskari_callable_init(PyObject *mod);
int koskari_callable_clear(PyObject *mod);


#endif


// The end.
