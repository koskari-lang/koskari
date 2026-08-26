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


typedef struct {
  PyTypeObject type;
  KosCallSlots kos_call;
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


int koskari_evaluated_init(PyObject *mod);
int koskari_evaluated_clear(PyObject *mod);


#endif /* KOSKARI_EVALUATED_H */


// The end.
