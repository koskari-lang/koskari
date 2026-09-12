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
   koskari/lib/cycle.c

   KosCycle: a native endless cycle over one source, duck-typed
   against itertools.cycle, implemented as a KosIter provider on the
   shared transform body (transform.c). This unit holds only the cycle
   step and the resume chain it arms.

   The first pass yields each element and keeps a copy. When the
   source runs out, the copies become the source: the iterator is
   released and every later step reads the buffer directly, so no
   iterator is re-acquired per lap and an async original replays in
   pure C with the evaluator untouched.

   That makes this the third provider that catches a source running
   out instead of propagating it, after batched and zip-longest --
   here exhaustion means start replaying, and only an empty buffer
   means the cycle is empty. A source of no elements is exhausted
   immediately rather than looping forever over nothing.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


typedef struct KosCycle {
  KosTransform base;
  PyObject *buf;    /* list of everything the first pass produced */
  Py_ssize_t pos;   /* read position within buf once replaying */
  uint8_t filled;   /* the source is spent; buf is the source now */
} KosCycle;


/**
   Is the pending failure just the source running out?
*/
static int cycle_spent(void) {

  return PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_StopIteration);
}


/**
   Take the next element from the buffer, wrapping at its end. Only
   called once the buffer is known non-empty.

   :return: a new reference
*/
static PyObject *cycle_replay(KosCycle *c) {

  PyObject *item = PyList_GET_ITEM(c->buf, c->pos);

  c->pos++;
  if (c->pos >= PyList_GET_SIZE(c->buf))
    c->pos = 0;

  Py_INCREF(item);
  return item;
}


/**
   The source is spent. Release the iterator -- the base's close loop
   reads a NULL slot as nothing to close -- and switch to replaying.

   :return: 1 when there is anything to replay, 0 for an empty cycle
*/
static int cycle_switch_to_buffer(KosCycle *c) {

  Py_CLEAR(c->base.source);
  c->filled = 1;
  c->pos = 0;

  return PyList_GET_SIZE(c->buf) != 0;
}


/**
   Keep an element for later laps and hand back the caller's own
   reference to it. element is consumed.

   :return: element, or NULL with PyErr set
*/
static PyObject *cycle_keep(KosCycle *c, PyObject *element) {

  if (PyList_Append(c->buf, element) < 0) {
    Py_DECREF(element);
    return NULL;
  }
  return element;
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   cycle-step frame refuses by name (see the kos_transform_stage call
   site below and kos_dynframe_clone's resume refusal). */


/**
   DELIVER: the first pass produced an element, or ran out. Running
   out is not a failure -- it is the switch to replaying -- so the
   pending StopIteration is handled here rather than propagated.
   element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult cycle_resume_deliver(
    KosEvalState *es, void *state, PyObject *element) {

  KosCycle *c = (KosCycle *) state;
  KosTransform *t = &c->base;

  if (! element) {
    if (! cycle_spent())
      return EVAL_RAISE();

    PyErr_Clear();
    if (! cycle_switch_to_buffer(c)) {
      PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }

    {
      PyObject *item = cycle_replay(c);

      return KosNativeFrame_ReturnPark(es, item, &t->cached_frame);
    }
  }

  element = cycle_keep(c, element);
  if (! element)
    return EVAL_RAISE();

  return KosNativeFrame_ReturnPark(es, element, &t->cached_frame);
}


/**
   Cold-start kickoff for a driven source, made from inside
   koskari_eval's own dispatch rather than from the step. Always
   invoked with NULL.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult cycle_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosOpResult r;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  r = KosEvalState_NextResume(
      es, ((KosTransform *) state)->source, NULL,
      cycle_resume_deliver, state);
  RESULT_ENTRY(es, r, cycle_resume_deliver, state);
}


/* === the step === */


static KosIterStepResult cycle_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosCycle *c = (KosCycle *) self;
  KosTransform *t = &c->base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  /* Replaying: pure C from here on, whatever the original source
     was. */
  if (c->filled) {
    *out = cycle_replay(c);
    return KOS_ITER_VALUE;
  }

  if (t->src_sync) {
    PyObject *element = PyIter_Next(t->source);

    if (! element) {
      if (PyErr_Occurred())
        return KOS_ITER_ERROR;

      if (! cycle_switch_to_buffer(c)) {
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }

      *out = cycle_replay(c);
      return KOS_ITER_VALUE;
    }

    *out = cycle_keep(c, element);
    return *out ? KOS_ITER_VALUE : KOS_ITER_ERROR;
  }

  /* Driven source: set the cold-start kickoff resume and defer the
     fetch entirely. */
  return kos_transform_stage(es, t, NULL, (void *) cycle_resume_fetch,
                             "cycle-step", pre_top);
}


/* === the type === */


static const KosTransformSpec cycle_spec = {
  .has_callable = 0,
  .none_means = KOS_TF_NONE_REJECT,
};


static PyObject *koscycle_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *self = kos_transform_new(type, args, kwds, &cycle_spec);
  KosCycle *c;

  if (! self)
    return NULL;

  c = (KosCycle *) self;
  c->buf = PyList_New(0);
  if (! c->buf) {
    Py_DECREF(self);
    return NULL;
  }
  c->pos = 0;
  c->filled = 0;

  return self;
}


static int koscycle_traverse(PyObject *self, visitproc visit, void *arg) {

  KosCycle *c = (KosCycle *) self;

  Py_VISIT(c->buf);
  return kos_transform_traverse(self, visit, arg);
}


static int koscycle_clear(PyObject *self) {

  KosCycle *c = (KosCycle *) self;

  Py_CLEAR(c->buf);
  return kos_transform_clear(self);
}


static void koscycle_dealloc(PyObject *self) {

  KosCycle *c = (KosCycle *) self;

  PyObject_GC_UnTrack(self);
  Py_CLEAR(c->buf);

  kos_transform_dealloc(self);
}


KosEvaluatedTypeObject KosCycleType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.cycle",
    .tp_basicsize = sizeof(KosCycle),
    .tp_itemsize = 0,
    .tp_dealloc = koscycle_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosCycle(iterable) - native endless cycle; iterable "
              "both asynchronously (ambient await, koskari-flattened) "
              "and synchronously (demand path)",
    .tp_traverse = koscycle_traverse,
    .tp_clear = koscycle_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = koscycle_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = cycle_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


/* === Unit lifecycle === */


int koskari_cycle_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosCycleType)))
    return -1;

  if (PyDict_SetItemString(
          dict, "KosCycle",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosCycleType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(
          dict, "cycle",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosCycleType)))
    return -1;

  return 0;
}


int koskari_cycle_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
