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
   koskari/lib/transform/islice.c

   KosISlice: a native lazy islice over one source, duck-typed against
   itertools.islice, implemented as a KosIter provider on the shared
   transform body (transform.c). This unit holds only the islice step
   and the resume chain it arms.

   The counters outlive each step and are wider than the base's
   resume index, so they are this provider's own fields. A step
   discards elements until it reaches the next wanted position, then
   fetches one to emit -- so a single step can run an unbounded number
   of fetches, each of which may suspend when the source is driven.
   That discard run is flattened the way zip's row walk is: one loop
   that leaves only for a fetch that suspends and re-enters where it
   left off, with no frame per discarded element.

   Positions are counted, not indexed, so a spent slice leaves the
   rest of the source unconsumed, exactly as Python's does.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


typedef struct KosISlice {
  KosTransform base;
  Py_ssize_t next;  /* position of the next element to emit */
  Py_ssize_t stop;  /* one past the last position, or -1 for open */
  Py_ssize_t step;  /* positions between emitted elements */
  Py_ssize_t cnt;   /* position the source is about to deliver */
} KosISlice;


/**
   Read one of islice's bound arguments: None takes the default, and
   anything else must be an integer that fits a Py_ssize_t and is not
   negative.

   :return: 0, or -1 with the TypeError Python's islice raises
*/
static int islice_bound(
    PyObject *arg,
    Py_ssize_t dflt,
    const char *what,
    Py_ssize_t *out) {

  Py_ssize_t val;

  if (! arg || arg == Py_None) {
    *out = dflt;
    return 0;
  }

  val = PyNumber_AsSsize_t(arg, PyExc_OverflowError);
  if (val == -1 && PyErr_Occurred()) {
    PyErr_Clear();
    val = -1;
  }

  if (val < 0) {
    PyErr_Format(PyExc_ValueError,
                 "%s for islice() must be None or an integer: "
                 "0 <= x <= sys.maxsize.", what);
    return -1;
  }

  *out = val;
  return 0;
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   islice-step frame refuses by name (see the kos_transform_stage call
   site below and kos_dynframe_clone's resume refusal).

   The walk is carried by the counters rather than by a resume per
   element: SKIP runs the discard loop and only leaves it for a fetch
   that suspends, DISCARD drops that element and hops back into the
   loop, and EMIT completes the step with the element at the wanted
   position. */


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult islice_resume_discard(
    KosEvalState *es, void *state, PyObject *element);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult islice_resume_emit(
    KosEvalState *es, void *state, PyObject *element);


/**
   Advance past the element just emitted, clamping the next wanted
   position to the stop so an overflowing step cannot wrap.
*/
static void islice_advance(KosISlice *s) {

  Py_ssize_t prev = s->next;

  s->next += s->step;
  if (s->next < prev || (s->stop != -1 && s->next > s->stop))
    s->next = s->stop;
}


/**
   SKIP: discard elements until the source reaches the next wanted
   position, then fetch the one to emit. Always entered with a NULL
   value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult islice_resume_skip(
    KosEvalState *es, void *state, PyObject *value) {

  KosISlice *s = (KosISlice *) state;
  KosTransform *t = &s->base;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  while (s->cnt < s->next) {
    if (! t->src_sync) {
      KosOpResult r = KosEvalState_NextResume(es, t->source, NULL,
                                              islice_resume_discard, s);
      RESULT_ENTRY(es, r, islice_resume_discard, s);
    }

    {
      PyObject *element = PyIter_Next(t->source);

      if (! element) {
        if (! PyErr_Occurred())
          PyErr_SetNone(PyExc_StopIteration);
        return EVAL_RAISE();
      }
      Py_DECREF(element);
      s->cnt++;
    }
  }

  /* The slice is spent: stop here rather than consuming the rest. */
  if (s->stop != -1 && s->cnt >= s->stop) {
    PyErr_SetNone(PyExc_StopIteration);
    return EVAL_RAISE();
  }

  if (! t->src_sync) {
    KosOpResult r = KosEvalState_NextResume(es, t->source, NULL,
                                            islice_resume_emit, s);
    RESULT_ENTRY(es, r, islice_resume_emit, s);
  }

  {
    PyObject *element = PyIter_Next(t->source);

    if (! element) {
      if (! PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }
    RESUME_HOP(islice_resume_emit, es, s, element);
  }
}


/**
   DISCARD: a driven source delivered an element the slice skips over.
   Drop it and hop back into the skip loop. A tail call, so an
   arbitrarily long skip stays flat. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult islice_resume_discard(
    KosEvalState *es, void *state, PyObject *element) {

  KosISlice *s = (KosISlice *) state;

  if (! element)
    return EVAL_RAISE();

  Py_DECREF(element);
  s->cnt++;

  RESUME_HOP(islice_resume_skip, es, s, NULL);
}


/**
   EMIT: the element at the wanted position completes the step.
   element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult islice_resume_emit(
    KosEvalState *es, void *state, PyObject *element) {

  KosISlice *s = (KosISlice *) state;

  if (! element)
    return EVAL_RAISE();

  s->cnt++;
  islice_advance(s);

  return KosNativeFrame_ReturnPark(es, element, &s->base.cached_frame);
}


/* === the step === */


static KosIterStepResult islice_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosISlice *s = (KosISlice *) self;
  KosTransform *t = &s->base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  if (t->src_sync) {
    /* Pure C: the whole discard run and the emitted fetch complete
       without the evaluator. */
    while (s->cnt < s->next) {
      PyObject *element = PyIter_Next(t->source);

      if (! element) {
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }
      Py_DECREF(element);
      s->cnt++;
    }

    if (s->stop != -1 && s->cnt >= s->stop) {
      t->closed = 1;
      *out = NULL;
      return KOS_ITER_EXHAUSTED;
    }

    {
      PyObject *element = PyIter_Next(t->source);

      if (! element) {
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }

      s->cnt++;
      islice_advance(s);
      *out = element;
      return KOS_ITER_VALUE;
    }
  }

  /* Driven source: defer the whole walk. Nothing is fetched yet to
     stage, and a request that can suspend must run from inside
     koskari_eval's own dispatch. */
  return kos_transform_stage(es, t, NULL, (void *) islice_resume_skip,
                             "islice-step", pre_top);
}


/* === the type === */


static PyObject *kosislice_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  Py_ssize_t nargs = PyTuple_GET_SIZE(args);
  Py_ssize_t start = 0;
  Py_ssize_t stop = -1;
  Py_ssize_t step = 1;
  KosISlice *s;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "islice takes no keyword arguments");
    return NULL;
  }

  if (nargs < 2 || nargs > 4) {
    PyErr_SetString(PyExc_TypeError,
                    "islice expects 2 to 4 arguments: "
                    "islice(iterable, stop) or "
                    "islice(iterable, start, stop[, step])");
    return NULL;
  }

  if (nargs == 2) {
    if (islice_bound(PyTuple_GET_ITEM(args, 1), -1, "Stop argument", &stop))
      return NULL;

  } else {
    if (islice_bound(PyTuple_GET_ITEM(args, 1), 0, "Indices", &start) ||
        islice_bound(PyTuple_GET_ITEM(args, 2), -1, "Stop argument", &stop))
      return NULL;

    /* The step carries its own message rather than the bound one. */
    if (nargs == 4) {
      PyObject *arg = PyTuple_GET_ITEM(args, 3);

      if (arg != Py_None) {
        step = PyNumber_AsSsize_t(arg, PyExc_OverflowError);
        if (step == -1 && PyErr_Occurred())
          PyErr_Clear();

        if (step < 1) {
          PyErr_SetString(PyExc_ValueError,
                          "Step for islice() must be a positive integer "
                          "or None.");
          return NULL;
        }
      }
    }
  }

  s = (KosISlice *) type->tp_alloc(type, 0);
  if (! s)
    return NULL;

  if (kos_transform_init_one(&s->base, NULL, PyTuple_GET_ITEM(args, 0))) {
    Py_DECREF(s);
    return NULL;
  }

  s->next = start;
  s->stop = stop;
  s->step = step;
  s->cnt = 0;

  return (PyObject *) s;
}


KosEvaluatedTypeObject KosISliceType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.islice",
    .tp_basicsize = sizeof(KosISlice),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosISlice(iterable, stop) or "
              "KosISlice(iterable, start, stop[, step]) - native lazy "
              "islice; iterable both asynchronously (ambient await, "
              "koskari-flattened) and synchronously (demand path)",
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kosislice_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = islice_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


/* === Unit lifecycle === */


int koskari_islice_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosISliceType)))
    return -1;

  if (PyDict_SetItemString(
          dict, "KosISlice",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosISliceType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(
          dict, "islice",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosISliceType)))
    return -1;

  return 0;
}


int koskari_islice_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
