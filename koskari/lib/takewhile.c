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


/**
   koskari/lib/takewhile.c

   KosTakeWhile: a native lazy takewhile over one source, duck-typed
   against itertools.takewhile, implemented as a KosIter provider on
   the shared transform body (transform.c). This unit holds only the
   takewhile step and the resume chain it arms.

   The first element the predicate rejects ends the run. That element
   is consumed and dropped, and the source is left open rather than
   closed, which is what itertools does and what natural exhaustion
   already gives here: ki_finish marks the provider closed without
   closing the source, and kos_transform_close declines once closed.

   The fast path is pure C: a sync source element and an immediate
   verdict emit or stop with the evaluator untouched. Anything
   asynchronous runs under one CPS resume-carrying frame carrying
   FETCH -> INVOKE -> VERDICT, the element held on the transform while
   its verdict settles.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   takewhile-step frame refuses by name (see the kos_transform_stage
   call sites below and kos_dynframe_clone's resume refusal). */


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult takewhile_resume_invoke(
    KosEvalState *es, void *state, PyObject *element);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult takewhile_resume_verdict(
    KosEvalState *es, void *state, PyObject *value);


/**
   The predicate rejected: the run is over. Raising StopIteration is
   how a step reports being finished, and leaves the source untouched.
*/
static KosOpResult takewhile_stop(KosTransform *t) {

  Py_CLEAR(t->pending);
  t->closed = 1;
  PyErr_SetNone(PyExc_StopIteration);
  return EVAL_RAISE();
}


/**
   FETCH: request the next source element. Always entered with a NULL
   value, either cold from the driven-source install or from the
   trampoline.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult takewhile_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosTransform *t = (KosTransform *) state;
  KosOpResult r;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  r = KosEvalState_NextResume(es, t->source, NULL,
                              takewhile_resume_invoke, t);
  RESULT_ENTRY(es, r, takewhile_resume_invoke, t);
}


/**
   INVOKE: test element against the predicate. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult takewhile_resume_invoke(
    KosEvalState *es, void *state, PyObject *element) {

  KosTransform *t = (KosTransform *) state;

  if (! element)
    return EVAL_RAISE();

  /* pending steals the element's reference and holds it across the
     call; the argument vector borrows it for the call's duration. */
  t->pending = element;
  {
    KosOpResult r = KosEvalState_CallResult(es, t->fn.cs_callable,
                                            &element, 1, NULL, 0,
                                            takewhile_resume_verdict, t);
    RESULT_ENTRY(es, r, takewhile_resume_verdict, t);
  }
}


/**
   VERDICT: a true verdict emits the held element, a false one ends
   the run. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult takewhile_resume_verdict(
    KosEvalState *es, void *state, PyObject *value) {

  KosTransform *t = (KosTransform *) state;
  int ok;

  if (! value)
    return EVAL_RAISE();

  ok = PyObject_IsTrue(value);
  Py_DECREF(value);
  if (ok < 0)
    return EVAL_RAISE();

  if (! ok)
    return takewhile_stop(t);

  {
    PyObject *element = t->pending;

    t->pending = NULL;
    return KosNativeFrame_ReturnPark(es, element, &t->cached_frame);
  }
}


/* === the step === */


static KosIterStepResult takewhile_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransform *t = (KosTransform *) self;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  if (t->src_sync) {
    PyObject *element = PyIter_Next(t->source);

    if (! element) {
      if (PyErr_Occurred())
        return KOS_ITER_ERROR;
      t->closed = 1;
      *out = NULL;
      return KOS_ITER_EXHAUSTED;
    }

    /* Any KosCallable predicate's verdict must come back to this
       step, so a resume-carrying frame installs here and the element
       is staged raw for takewhile_resume_invoke, which makes the call
       in this evaluator. */
    if (t->fn.cs_kind != KOS_CALL_SHAPE_PLAIN)
      return kos_transform_stage(es, t, element,
                                 (void *) takewhile_resume_invoke,
                                 "takewhile-step", pre_top);

    /* A plain python predicate needs no evaluator at all. */
    {
      PyObject *result = PyObject_CallOneArg(t->fn.cs_callable, element);

      if (! result) {
        Py_DECREF(element);
        return KOS_ITER_ERROR;
      }

      switch (KosEval_ResultIsValue(result)) {
        case 1: {
          int ok = PyObject_IsTrue(result);

          Py_DECREF(result);
          if (ok < 0) {
            Py_DECREF(element);
            return KOS_ITER_ERROR;
          }
          if (! ok) {
            Py_DECREF(element);
            t->closed = 1;
            *out = NULL;
            return KOS_ITER_EXHAUSTED;
          }
          *out = element;
          return KOS_ITER_VALUE;
        }
        case 0: {
          /* Awaitable verdict: stage it raw on a resume-carrying
             frame, which owns every suspension. The element is held
             on the transform until the verdict settles. */
          KosIterStepResult staged = kos_transform_stage(
              es, t, result, (void *) takewhile_resume_verdict,
              "takewhile-step", pre_top);

          if (staged != KOS_ITER_INSTALLED_VALUE) {
            Py_DECREF(element);
            return staged;
          }
          t->pending = element;  /* steals */
          return staged;
        }
        default:
          Py_DECREF(result);
          Py_DECREF(element);
          return KOS_ITER_ERROR;
      }
    }
  }

  /* Driven source: set the cold-start fetch resume and defer the
     whole step. */
  return kos_transform_stage(es, t, NULL, (void *) takewhile_resume_fetch,
                             "takewhile-step", pre_top);
}


/* === the type === */


static const KosTransformSpec takewhile_spec = {
  .has_callable = 1,
  .none_means = KOS_TF_NONE_REJECT,
};


static PyObject *kostakewhile_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  return kos_transform_new(type, args, kwds, &takewhile_spec);
}


KosEvaluatedTypeObject KosTakeWhileType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.takewhile",
    .tp_basicsize = sizeof(KosTransform),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosTakeWhile(pred, iterable) - native lazy takewhile; "
              "iterable both asynchronously (ambient await, "
              "koskari-flattened) and synchronously (demand path)",
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kostakewhile_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = takewhile_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


/* === Unit lifecycle === */


int koskari_takewhile_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosTakeWhileType)))
    return -1;

  if (PyDict_SetItemString(
          dict, "KosTakeWhile",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosTakeWhileType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(
          dict, "takewhile",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosTakeWhileType)))
    return -1;

  return 0;
}


int koskari_takewhile_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
