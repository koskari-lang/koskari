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
   koskari/lib/starmap.c

   KosStarMap: a native lazy starmap over one source, duck-typed
   against itertools.starmap, implemented as a KosIter provider on the
   shared transform body (transform.c). This unit holds only the
   starmap step and the resume chain it arms.

   Where map calls its function with the element, starmap calls it
   with the element's own items as the argument vector. The element is
   unpacked with PySequence_Tuple, which hands back an exact tuple
   untouched, so the common case of a tuple source costs no copy.
   Nothing else differs from map, and this provider carries no state
   of its own beyond the base.

   The fast path is pure C: a sync source element and a plain python
   function complete as KOS_ITER_VALUE with the evaluator untouched. A
   direct-return koskari function installs through the generic call
   entry with no helper frame. Anything else -- a driven source, a
   FRAME-shaped function without that guarantee, an awaitable result
   -- runs under one CPS resume-carrying frame.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/**
   Unpack an element into the argument vector starmap calls with.
   Returns a new reference to a tuple, or NULL with the TypeError a
   non-iterable element earns.
*/
static PyObject *starmap_unpack(PyObject *element) {

  PyObject *args = PySequence_Tuple(element);

  Py_DECREF(element);
  return args;
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   starmap-step frame refuses by name (see the kos_transform_stage
   call sites below and kos_dynframe_clone's resume refusal). */


/**
   The invoke completed. The value is settled by contract, so the step
   completes with it directly. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult starmap_resume_after_result(
    KosEvalState *es, void *state, PyObject *value) {

  KosTransform *t = (KosTransform *) state;

  if (! value)
    return EVAL_RAISE();

  return KosNativeFrame_ReturnPark(es, value, &t->cached_frame);
}


/**
   INVOKE: unpack the element and apply func to its items. Every
   calling shape goes through the one entry, so a function that wants
   no frame still runs in this evaluator rather than through the
   Python boundary. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult starmap_resume_invoke(
    KosEvalState *es, void *state, PyObject *element) {

  KosTransform *t = (KosTransform *) state;
  PyObject *args;
  KosOpResult r;

  if (! element)
    return EVAL_RAISE();

  args = starmap_unpack(element);
  if (! args)
    return EVAL_RAISE();

  r = KosEvalState_CallResult(es, t->fn.cs_callable,
                              PySequence_Fast_ITEMS(args),
                              PyTuple_GET_SIZE(args), NULL, 0,
                              starmap_resume_after_result, t);
  Py_DECREF(args);
  RESULT_ENTRY(es, r, starmap_resume_after_result, t);
}


/**
   Cold-start kickoff for a driven source: the very first NextResume
   call for a freshly installed step, made from inside koskari_eval's
   own dispatch rather than from the step. Always invoked with NULL.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult starmap_resume_start_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosOpResult r;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  r = KosEvalState_NextResume(
      es, ((KosTransform *) state)->source, NULL,
      starmap_resume_invoke, state);
  RESULT_ENTRY(es, r, starmap_resume_invoke, state);
}


/* === the step === */


static KosIterStepResult starmap_iter_step(
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
    PyObject *args;

    if (! element) {
      if (PyErr_Occurred())
        return KOS_ITER_ERROR;
      t->closed = 1;
      *out = NULL;
      return KOS_ITER_EXHAUSTED;
    }

    /* A FRAME-shaped function's call must run inside koskari_eval's
       own dispatch, so the element is staged raw and unpacked by
       starmap_resume_invoke. The direct-return shape below is the one
       exception: its install needs no helper frame. */
    if (t->fn.cs_kind != KOS_CALL_SHAPE_PLAIN && ! t->fn.cs_direct)
      return kos_transform_stage(es, t, element,
                                 (void *) starmap_resume_invoke,
                                 "starmap-step", pre_top);

    args = starmap_unpack(element);
    if (! args)
      return KOS_ITER_ERROR;

    /* A direct-delivery koskari function (cs_direct: its install
       fills a frame whose return IS the element) installs through the
       generic call entry with no helper frame. */
    if (t->fn.cs_kind == KOS_CALL_SHAPE_FRAME) {
      KosOpResult r = KosEvalState_CallResult(es, t->fn.cs_callable,
                                              PySequence_Fast_ITEMS(args),
                                              PyTuple_GET_SIZE(args),
                                              NULL, 0, NULL, NULL);
      Py_DECREF(args);
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE) {
        t->busy = 1;
        return KOS_ITER_INSTALLED_VALUE;
      }
      return kos_transform_step_abort(es, t, pre_top);
    }

    /* A plain python callable needs no evaluator at all. */
    {
      PyObject *result = PyObject_Vectorcall(
          t->fn.cs_callable, PySequence_Fast_ITEMS(args),
          (size_t) PyTuple_GET_SIZE(args), NULL);

      Py_DECREF(args);
      if (! result)
        return KOS_ITER_ERROR;

      switch (KosEval_ResultIsValue(result)) {
        case 1:
          *out = result;
          return KOS_ITER_VALUE;
        case 0:
          /* Awaitable function result: stage it, unclassified, and
             defer to starmap_resume_after_result -- the await it
             needs must happen from inside koskari_eval's own
             dispatch. */
          return kos_transform_stage(es, t, result,
                                     (void *) starmap_resume_after_result,
                                     "starmap-step", pre_top);
        default:
          Py_DECREF(result);
          return KOS_ITER_ERROR;
      }
    }
  }

  /* Driven source: set the cold-start kickoff resume and defer the
     first fetch entirely -- nothing is fetched yet to stage, and the
     request itself (which can suspend) must run from inside
     koskari_eval's own dispatch. */
  return kos_transform_stage(es, t, NULL,
                             (void *) starmap_resume_start_fetch,
                             "starmap-step", pre_top);
}


/* === the type === */


static const KosTransformSpec starmap_spec = {
  .has_callable = 1,
  .none_means = KOS_TF_NONE_REJECT,
};


static PyObject *kosstarmap_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  return kos_transform_new(type, args, kwds, &starmap_spec);
}


KosEvaluatedTypeObject KosStarMapType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.starmap",
    .tp_basicsize = sizeof(KosTransform),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosStarMap(func, iterable) - native lazy starmap; "
              "iterable both asynchronously (ambient await, "
              "koskari-flattened) and synchronously (demand path)",
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kosstarmap_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = starmap_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


/* === Unit lifecycle === */


int koskari_starmap_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosStarMapType)))
    return -1;

  if (PyDict_SetItemString(
          dict, "KosStarMap",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosStarMapType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(
          dict, "starmap",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosStarMapType)))
    return -1;

  return 0;
}


int koskari_starmap_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
