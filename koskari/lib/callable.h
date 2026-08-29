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
   Check-and-resolve in one: when obj is a KosCallable (a KosEvaluated
   whose suite carries the install slot), store the resolved suite into
   *out_slots and return 1; otherwise return 0 with *out_slots
   untouched. The single resolve serves the whole call sequence --
   frame prep and dispatch take the suite from here rather than
   re-resolving.
*/
static inline int KosCallable_CheckSlots(
    PyObject *obj,
    KosCallSlots **out_slots) {

  if (! obj || ! KosEvaluated_Check(obj))
    return 0;

  KosEvaluatedTypeObject *et = KosEvaluated_Resolve(Py_TYPE(obj));
  if (! et)
    return 0;

  KosCallSlots *slots = &et->kos_call;
  if (! slots->kc_vinstall)
    return 0;

  *out_slots = slots;
  return 1;
}


static inline int kos_callable_check(PyObject *obj) {

  KosCallSlots *slots;
  return KosCallable_CheckSlots(obj, &slots);
}

#define KosCallable_Check(obj) kos_callable_check(obj)


/**
   Effective wants_frame for a callable: the per-instance hook when the
   suite provides one, else the static flag. 0 when the type carries no
   call suite.
*/
static inline int KosCallable_WantsFrame(PyObject *callable) {

  KosCallSlots *slots;

  if (! KosCallable_CheckSlots(callable, &slots))
    return 0;
  if (slots->kc_wants_frame_fn)
    return slots->kc_wants_frame_fn(callable);
  return slots->kc_wants_frame;
}


static inline int KosCallSlots_WantsFrame(
    KosCallSlots *slots,
    PyObject *callable) {

  if (slots->kc_wants_frame_fn)
    return slots->kc_wants_frame_fn(callable);
  return slots->kc_wants_frame;
}


/* === KosCallShape: a callable held for repeated invocation === */

/**
   How a held callable is invoked, resolved once:

   FRAME -- a frame-wanting KosCallable; invoke through the evaluator
   (KosEvalState_Call), its frame installing on the chain and its
   return arriving as the callee-return delivery.
   BOXED -- a KosCallable with a python-boundary worker and no frame;
   invoke through KosCallShape_PyCallOne.
   PLAIN -- an ordinary python callable; invoke through
   KosCallShape_PyCallOne.
*/
typedef enum KosCallShapeKind {
  KOS_CALL_SHAPE_FRAME = 0,
  KOS_CALL_SHAPE_BOXED,
  KOS_CALL_SHAPE_PLAIN,
} KosCallShapeKind;


/**
   A callable an object holds and invokes repeatedly, with its calling
   shape resolved at construction rather than per call. cs_callable is
   owned -- the holder's traverse visits it and its clear releases it
   through KosCallShape_Clear. cs_slots is the borrowed static-type
   suite, NULL for PLAIN.
*/
typedef struct KosCallShape {
  PyObject *cs_callable;
  KosCallSlots *cs_slots;
  uint8_t cs_kind;
  uint8_t cs_direct;   /* FRAME only: the install fills a frame whose
                          return is the call's value, guaranteed --
                          true for an exact non-yielding KosFunction.
                          Other FRAME shapes (values, bound methods)
                          may complete inline instead, so a caller
                          relying on frame-return delivery must host
                          them under a frame of its own. */
} KosCallShape;


/* exactness check for cs_direct; the full declaration is in func.h */
extern KosEvaluatedTypeObject KosFunctionType;


/**
   Resolve callable's shape into *shape, taking a reference.
*/
static inline void KosCallShape_Init(
    KosCallShape *shape,
    PyObject *callable) {

  Py_INCREF(callable);
  shape->cs_callable = callable;

  if (KosCallable_CheckSlots(callable, &shape->cs_slots)) {
    shape->cs_kind = KosCallSlots_WantsFrame(shape->cs_slots, callable)
        ? KOS_CALL_SHAPE_FRAME : KOS_CALL_SHAPE_BOXED;
  } else {
    shape->cs_slots = NULL;
    shape->cs_kind = KOS_CALL_SHAPE_PLAIN;
  }

  shape->cs_direct =
      (shape->cs_kind == KOS_CALL_SHAPE_FRAME
       && Py_TYPE(callable) == KOS_EVALUATED_PYTYPE(KosFunctionType));
}


static inline void KosCallShape_Clear(KosCallShape *shape) {

  Py_CLEAR(shape->cs_callable);
  shape->cs_slots = NULL;
}


/**
   One-argument call at the python boundary through the resolved
   shape: a PLAIN callable directly, otherwise the suite's
   python-boundary worker. arg is borrowed.

   :return: the result (new ref) or NULL with PyErr set
*/
PyObject *KosCallShape_PyCallOne(KosCallShape *shape, PyObject *arg);


/* === KosCallable_* : KosCallable-only invocation (no type check, no frame prep) === */

/**
   KosCallable_CallSlots: invoke kc_vinstall through an
   already-resolved suite with vectorcall-style args.

   es must match the KosEvalState_Call* caller. callable, args, kwnames are
   borrowed.
*/
KosOpResult KosCallable_CallSlots(KosEvalState *es,
    KosCallSlots *slots, PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call);

/**
   KosCallable_CallArgsSlots: the central tuple-to-vector spread --
   borrow the tuple's item array as the vector (extended with the
   dict's values under a kwnames tuple when keywords are present) and
   invoke kc_vinstall.
*/
KosOpResult KosCallable_CallArgsSlots(KosEvalState *es,
    KosCallSlots *slots, PyObject *callable,
    PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call);

/**
   KosCallable_Call: as KosCallable_CallSlots, resolving the suite from
   the callable's type. callable MUST be a KosCallable; no type check is
   performed.
*/
KosOpResult KosCallable_Call(KosEvalState *es,
    PyObject *callable,
    PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames,
    int tail_call);

/**
   KosCallable_CallArgs: as KosCallable_CallArgsSlots, resolving the
   suite from the callable's type. callable MUST be a KosCallable; no
   type check is performed.
*/
KosOpResult KosCallable_CallArgs(KosEvalState *es,
    PyObject *callable, PyObject *args_tuple, PyObject *kwds_dict,
    int tail_call);

/**
   KosCallable_CallNoArgs: dispatch kc_vinstall with no arguments.
   callable MUST be a KosCallable; no type check is performed.
*/
KosOpResult KosCallable_CallNoArgs(KosEvalState *es,
    PyObject *callable, int tail_call);


/* === Python-slot adapters === */

/**
   Invoke a callable from Python through the kos call interface: resolve
   the call suite and dispatch its kc_pycall worker; PyObject_Call is the
   fallback for anything without one. The result is whatever the type's
   surface dictates -- a Coroutine or AsyncGenerator for async-surface
   types, a completed value for demand-surface types.

   callable, args, kwds are borrowed. args may be NULL.
*/
PyObject *Koskari_AsyncCall(
    PyObject *callable, PyObject *args, PyObject *kwds);

/**
   tp_call filler for an async-surface callable: Koskari_AsyncCall.
*/
PyObject *KosCallAdapt_CallAsync(
    PyObject *self, PyObject *args, PyObject *kwds);

/**
   vectorcallfunc filler with the same semantics: dispatch the suite's
   kc_pyvectorcall when present, else build tuple+dict and defer to
   Koskari_AsyncCall.
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
