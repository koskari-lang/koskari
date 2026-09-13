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
   koskari/lib/transform/map.c

   KosMap: a native lazy map over one source, duck-typed against
   builtins.map, implemented as a KosIter provider on the shared
   transform body (transform.c). This unit holds only the map step and
   the resume chain it arms.

   The fast path is pure C: a sync source element and a non-suspending,
   direct-return-guaranteed koskari function complete as KOS_ITER_VALUE
   or a graft install with the evaluator otherwise untouched (cs_direct,
   still unconverted below pending a benchmark-driven decision -- see
   design/archive/cps-consumer-api.md). Anything else -- a driven source, or a
   FRAME-shaped or awaitable-returning function without that guarantee
   -- runs under one CPS resume-carrying frame: FETCH requests the next
   source element via KosEvalState_NextResume, INVOKE applies the
   function via KosEvalState_CallResult, and completion delivers the
   element to the consumer's iteration node. Exhaustion and errors leave
   the frame as exceptions; the consumer's iteration node converts
   StopIteration per its own rules, which also gives a mapped
   function's StopIteration builtins.map truncation parity.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   map-step frame refuses by name (see the kos_transform_stage call
   sites below and kos_dynframe_clone's resume refusal). */


/**
   The invoke completed. The value is settled by contract, so the step
   completes with it directly. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_after_result(
    KosEvalState *es, void *state, PyObject *value) {

  KosTransform *m = (KosTransform *) state;

  /* The raise walk is propagating an exception: let it, so the frame
     pops and the consumer's iteration node owns the conversion
     (StopIteration truncation parity included). */
  if (! value)
    return EVAL_RAISE();

  return KosNativeFrame_ReturnPark(es, value, &m->cached_frame);
}


/**
   INVOKE: apply func to element. Every calling shape goes through the
   one entry -- KosEvalState_Call resolves the callable and prepares
   (or declines to prepare) a frame per its own suite, so a callable
   that wants no frame still runs in this evaluator rather than
   through the Python boundary. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_after_fetch(
    KosEvalState *es, void *state, PyObject *element) {

  KosTransform *m = (KosTransform *) state;
  KosOpResult r;

  if (! element)
    return EVAL_RAISE();

  r = KosEvalState_CallResult(es, m->fn.cs_callable, &element, 1, NULL,
                              0, map_resume_after_result, m);
  Py_DECREF(element);
  RESULT_ENTRY(es, r, map_resume_after_result, m);
}


/**
   Cold-start kickoff for a driven source: the very first NextResume
   call for a freshly installed step. map_iter_step cannot make this
   call itself -- it runs outside koskari_eval, and koskari_eval runs
   as a separate step after install (see stepper_step), never nested
   inside map_iter_step's own C call -- so it just sets this resume and
   lets the trampoline invoke it, from inside koskari_eval, once the
   frame is actually dispatched. Always invoked with NULL.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_start_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosOpResult r;

  (void) value;

  /* The raise walk is propagating an exception: let it, as every
     resume does. */
  if (PyErr_Occurred())
    return EVAL_RAISE();

  r = KosEvalState_NextResume(
      es, ((KosTransform *) state)->source, NULL,
      map_resume_after_fetch, state);
  RESULT_ENTRY(es, r, map_resume_after_fetch, state);
}


/* === the step === */


static KosIterStepResult map_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransform *m = (KosTransform *) self;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;

  if (! kos_transform_step_ready(m, send_val, out, &early))
    return early;

  if (m->src_sync) {
    PyObject *element = PyIter_Next(m->source);

    if (! element) {
      if (PyErr_Occurred())
        return KOS_ITER_ERROR;
      m->closed = 1;
      *out = NULL;
      return KOS_ITER_EXHAUSTED;
    }

    /* A direct-delivery koskari function (cs_direct: its install
       fills a frame whose return IS the element) installs through the
       generic call entry -- cached frame, the element's own address
       as the argument vector -- with no helper frame. */
    if (m->fn.cs_kind == KOS_CALL_SHAPE_FRAME && m->fn.cs_direct) {
      KosOpResult r = KosEvalState_CallResult(es, m->fn.cs_callable,
                                              &element, 1, NULL, 0,
                                              NULL, NULL);
      Py_DECREF(element);
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE) {
        m->busy = 1;
        return KOS_ITER_INSTALLED_VALUE;
      }
      return kos_transform_step_abort(es, m, pre_top);
    }

    /* Any other KosCallable -- a values or bound-method FRAME shape
       without the frame-return guarantee, or one wanting no frame at
       all: stage the element on a resume-carrying frame and defer the
       call to map_resume_after_fetch, which runs it in this evaluator.
       map_iter_step runs before koskari_eval drives this chain (see
       stepper_step), so the call itself -- which can install a callee
       or need an await -- must happen from inside that later dispatch,
       not here. */
    if (m->fn.cs_kind != KOS_CALL_SHAPE_PLAIN)
      return kos_transform_stage(es, m, element,
                                 (void *) map_resume_after_fetch,
                                 "map-step", pre_top);

    /* A plain python callable needs no evaluator at all. */
    {
      PyObject *result = PyObject_CallOneArg(m->fn.cs_callable, element);
      Py_DECREF(element);
      if (! result)
        return KOS_ITER_ERROR;

      switch (KosEval_ResultIsValue(result)) {
        case 1:
          *out = result;
          return KOS_ITER_VALUE;
        case 0:
          /* Awaitable function result: stage it, unclassified, and
             defer to map_resume_after_result the same way -- the
             await it needs must happen from inside koskari_eval's
             own dispatch, not from map_iter_step. */
          return kos_transform_stage(es, m, result,
                                     (void *) map_resume_after_result,
                                     "map-step", pre_top);
        default:
          Py_DECREF(result);
          return KOS_ITER_ERROR;
      }
    }
  }

  /* Driven source (KosIter provider or python async iterator): set the
     cold-start kickoff resume and defer the first fetch entirely --
     nothing is fetched yet to stage, and the request itself (which can
     suspend) must run from inside koskari_eval's own dispatch. */
  return kos_transform_stage(es, m, NULL, (void *) map_resume_start_fetch,
                             "map-step", pre_top);
}


/* === the type === */


static const KosTransformSpec map_spec = {
  .has_callable = 1,
  .none_means = KOS_TF_NONE_REJECT,
};


static PyObject *kosmap_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  return kos_transform_new(type, args, kwds, &map_spec);
}


KosEvaluatedTypeObject KosMapType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.map",
    .tp_basicsize = sizeof(KosTransform),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosMap(func, iterable) - native lazy map; iterable both "
              "asynchronously (ambient await, koskari-flattened) and "
              "synchronously (demand path)",
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kosmap_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = map_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


/* === Unit lifecycle === */


int koskari_map_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosMapType)))
    return -1;

  if (PyDict_SetItemString(dict, "KosMap",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosMapType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(dict, "map",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosMapType)))
    return -1;

  return 0;
}


int koskari_map_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
