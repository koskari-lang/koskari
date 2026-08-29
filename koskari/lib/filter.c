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
   koskari/lib/filter.c

   KosFilter: a native lazy filter over one source, duck-typed against
   builtins.filter, implemented as a KosIter provider.

   The fast path is pure C: a sync source element and an immediate
   verdict -- the element's own truthiness for a None predicate, or a
   non-suspending predicate result -- emit or skip with the evaluator
   untouched, skipped elements never leaving the loop. Anything
   asynchronous installs one native frame whose handler runs the
   three-phase machine -- FETCH requests the next source element
   (KosNativeFrame_RequestNext), INVOKE applies the predicate with the
   element held on the filter, VERDICT tests the predicate's result
   and either completes the frame with the held element or loops back
   to FETCH. The frame is parked on the filter between steps
   (KosNativeFrame_ReturnPark), stripped inert so the cache holds no
   references. Exhaustion and errors leave the frame as exceptions;
   the consumer's iteration node converts StopIteration per its own
   rules.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === instance === */


typedef enum {
  KOS_FILTER_NF_FETCH = 0,  /* next wake starts a fetch */
  KOS_FILTER_NF_WAIT_ELEM,  /* fetch in flight; element arrives by
                               stack push or AWAITED */
  KOS_FILTER_NF_WAIT_VERDICT, /* predicate in flight over pending;
                               verdict arrives by CALL_VALUE, AWAITED,
                               or stack push */
} KosFilterNfPhase;


typedef struct {
  PyObject_HEAD
  KosCallShape pred;     /* predicate, shape resolved at construction;
                            cs_callable NULL for the None predicate
                            (the element's truthiness decides) */
  PyObject *source;      /* acquired iterator (sync or async) */
  PyObject *pending;     /* element under test while a verdict is in
                            flight; owned */
  KosDynFrame *cached_frame; /* inert frame parked between steps */
  KosContext *ctx;       /* durable drive context (ki_pyctx), created
                            lazily; adapter-recycled, freed with
                            the filter */
  uint8_t src_sync;      /* source steps through tp_iternext alone */
  uint8_t closed;        /* exhausted or closed */
  uint8_t busy;          /* a step is in flight; ki_finish clears */
  uint8_t nf_phase;      /* KosFilterNfPhase while the frame is live */
} KosFilterObject;


/* === the step's native frame program === */


static KosOpResult filter_nf_handler(
    KosEvalState *es,
    KosNativeFrameEvent ev,
    PyObject *value);


/* No clone entry: replaying a captured step would re-consume source
   elements, so continuation capture across a filter step is refused. */
static const KosNativeProgram kos_filter_program = {
  .np_name = "filter-step",
  .np_handler = filter_nf_handler,
  .np_clone_state = NULL,
  .np_free_state = NULL,
};


static KosOpResult filter_nf_fetch(KosEvalState *es, KosFilterObject *f);


/**
   VERDICT from inside the frame program: value is the predicate's
   completed result (consumed). A true verdict completes the frame
   with the held element, parking the frame on the filter; a false one
   releases the element and loops back to FETCH.
*/
static KosOpResult filter_nf_verdict(
    KosEvalState *es,
    KosFilterObject *f,
    PyObject *value) {

  int ok = PyObject_IsTrue(value);

  Py_DECREF(value);
  if (ok < 0)
    return EVAL_RAISE();

  if (ok) {
    PyObject *element = f->pending;
    f->pending = NULL;
    return KosNativeFrame_ReturnPark(es, element, &f->cached_frame);
  }

  Py_CLEAR(f->pending);
  f->nf_phase = KOS_FILTER_NF_FETCH;
  return filter_nf_fetch(es, f);
}


/**
   INVOKE from inside the frame program: test element against the
   predicate. An immediate verdict resolves in place; a frame-wanting
   predicate installs as a child frame (its return arrives as the
   CALL_VALUE wake) and an awaitable predicate result parks, the
   element held on the filter either way. element is consumed.
*/
static KosOpResult filter_nf_invoke(
    KosEvalState *es,
    KosFilterObject *f,
    PyObject *element) {

  /* None predicate: the element's own truthiness is the verdict. */
  if (! f->pred.cs_callable) {
    int ok = PyObject_IsTrue(element);

    if (ok < 0) {
      Py_DECREF(element);
      return EVAL_RAISE();
    }
    if (ok)
      return KosNativeFrame_ReturnPark(es, element, &f->cached_frame);

    Py_DECREF(element);
    f->nf_phase = KOS_FILTER_NF_FETCH;
    return filter_nf_fetch(es, f);
  }

  if (f->pred.cs_kind == KOS_CALL_SHAPE_FRAME) {
    f->pending = element;
    f->nf_phase = KOS_FILTER_NF_WAIT_VERDICT;
    {
      KosOpResult r = KosEvalState_Call(es, f->pred.cs_callable,
                                        &element, 1, NULL, 0);
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE)
        return EVAL_REPROLOGUE();
      return r;
    }
  }

  {
    PyObject *result = KosCallShape_PyCallOne(&f->pred, element);

    if (! result) {
      Py_DECREF(element);
      return EVAL_RAISE();
    }

    switch (KosEval_ResultIsValue(result)) {
      case 1:
        f->pending = element;
        return filter_nf_verdict(es, f, result);
      case 0: {
        /* Awaitable verdict: park; the completion wakes as AWAITED
           (or lands on the stack when it finishes eagerly). */
        f->pending = element;
        f->nf_phase = KOS_FILTER_NF_WAIT_VERDICT;
        {
          KosOpResult pr = KosEvalState_PushOrAwait(es, result);
          if (pr.kind == KOS_RES_NORMAL || pr.kind == KOS_RES_REPROLOGUE)
            return EVAL_REPROLOGUE();
          return pr;
        }
      }
      default:
        Py_DECREF(result);
        Py_DECREF(element);
        return EVAL_RAISE();
    }
  }
}


/**
   FETCH from inside the frame program: the next source element. A
   sync source pulls directly through tp_iternext -- no iteration node
   -- so the rejected-element loop stays cheap. A driven source runs
   one request (KosNativeFrame_RequestNext): a completed element goes
   straight to INVOKE; installed or suspended work leaves the fetch in
   flight, the element arriving by stack delivery. Exhaustion raises
   StopIteration for the consumer's iteration node to convert.
*/
static KosOpResult filter_nf_fetch(KosEvalState *es, KosFilterObject *f) {

  PyObject *out = NULL;

  if (f->src_sync) {
    PyObject *element = PyIter_Next(f->source);

    if (! element) {
      if (PyErr_Occurred())
        return EVAL_RAISE();
      KosDynFrame_NATIVE_PROG(KosEvalState_FRAME(es)) = NULL;
      PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }
    return filter_nf_invoke(es, f, element);
  }

  switch (KosNativeFrame_RequestNext(es, f->source, &out)) {
    case KOS_IREQ_DELIVERED:
      return filter_nf_invoke(es, f, out);

    case KOS_IREQ_EXHAUSTED:
      KosDynFrame_NATIVE_PROG(KosEvalState_FRAME(es)) = NULL;
      PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();

    case KOS_IREQ_CONTINUES:
      f->nf_phase = KOS_FILTER_NF_WAIT_ELEM;
      return EVAL_REPROLOGUE();

    case KOS_IREQ_EXIT:
      f->nf_phase = KOS_FILTER_NF_WAIT_ELEM;
      return EVAL_AWAIT(out);

    case KOS_IREQ_ERROR:
    default:
      return EVAL_RAISE();
  }
}


/**
   The step's native frame handler: the FETCH/INVOKE/VERDICT machine,
   woken by the eval loop's iteration call, stack deliveries, awaited
   resumes, and callee-frame returns. Exceptions are always declined
   -- the raise walk pops the frame and the consumer's iteration node
   converts protocol exceptions to exhaustion.
*/
static KosOpResult filter_nf_handler(
    KosEvalState *es,
    KosNativeFrameEvent ev,
    PyObject *value) {

  KosFilterObject *f =
      (KosFilterObject *) KosDynFrame_NATIVE_STATE(KosEvalState_FRAME(es));

  if (! f) {
    Py_XDECREF(value);
    PyErr_SetString(PyExc_SystemError, "filter frame lost its state");
    return EVAL_RAISE();
  }

  switch (ev) {
    case KOS_NFRAME_EXCEPTION:
      /* Decline: the frame pops and the consumer's iteration node owns
         the conversion. */
      return EVAL_RAISE();

    case KOS_NFRAME_ITERATE:
      if (f->nf_phase == KOS_FILTER_NF_FETCH)
        return filter_nf_fetch(es, f);
      /* A delivery landed on the frame stack: pop it and route. */
      value = KosEvalState_StackPopRef(es);
      if (! value)
        return EVAL_RAISE();
      break;

    case KOS_NFRAME_CALL_VALUE:
    case KOS_NFRAME_AWAITED:
      break;

    default:
      Py_XDECREF(value);
      PyErr_SetString(PyExc_SystemError, "filter frame: unexpected wake");
      return EVAL_RAISE();
  }

  if (f->nf_phase == KOS_FILTER_NF_WAIT_ELEM)
    return filter_nf_invoke(es, f, value);

  /* KOS_FILTER_NF_WAIT_VERDICT: the predicate completed -- or handed
     back another awaitable (a koskari function's coroutine);
     reclassify. */
  switch (KosEval_ResultIsValue(value)) {
    case 1:
      return filter_nf_verdict(es, f, value);
    case 0: {
      KosOpResult pr = KosEvalState_PushOrAwait(es, value);
      if (pr.kind == KOS_RES_NORMAL || pr.kind == KOS_RES_REPROLOGUE)
        return EVAL_REPROLOGUE();
      return pr;
    }
    default:
      Py_DECREF(value);
      return EVAL_RAISE();
  }
}


/* === the iter suite === */


static KosIterStepResult filter_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosFilterObject *f = (KosFilterObject *) self;

  if (f->closed) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  if (f->busy) {
    PyErr_SetString(PyExc_RuntimeError,
                    "filter is already being iterated");
    return KOS_ITER_ERROR;
  }

  if (send_val && send_val != Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "can't send a value into a filter; it takes none");
    return KOS_ITER_ERROR;
  }

  if (f->src_sync) {
    /* Pure C while verdicts are immediate: fetch, test, emit or skip.
       Skipped elements never leave this loop. */
    for (;;) {
      PyObject *element = PyIter_Next(f->source);

      if (! element) {
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        f->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }

      if (! f->pred.cs_callable) {
        int ok = PyObject_IsTrue(element);

        if (ok < 0) {
          Py_DECREF(element);
          return KOS_ITER_ERROR;
        }
        if (ok) {
          *out = element;
          return KOS_ITER_VALUE;
        }
        Py_DECREF(element);
        continue;
      }

      /* A frame-wanting predicate's verdict must come back to the
         program, so the frame installs here and the predicate runs as
         its child, the element held on the filter. */
      if (f->pred.cs_kind == KOS_CALL_SHAPE_FRAME) {
        if (! KosNativeFrame_Install(es, &kos_filter_program, f,
                                     NULL, &f->cached_frame)) {
          Py_DECREF(element);
          return KOS_ITER_ERROR;
        }
        f->pending = element;
        f->nf_phase = KOS_FILTER_NF_WAIT_VERDICT;
        {
          KosOpResult r = KosEvalState_Call(es, f->pred.cs_callable,
                                            &element, 1, NULL, 0);
          if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE) {
            f->busy = 1;
            return KOS_ITER_INSTALLED_VALUE;
          }
          return KOS_ITER_ERROR;
        }
      }

      {
        PyObject *result = KosCallShape_PyCallOne(&f->pred, element);

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
            if (ok) {
              *out = element;
              return KOS_ITER_VALUE;
            }
            Py_DECREF(element);
            continue;
          }
          case 0: {
            /* Awaitable verdict: stage it on the frame program, which
               owns every suspension. */
            KosDynFrame *frame = KosNativeFrame_Install(
                es, &kos_filter_program, f, result, &f->cached_frame);
            Py_DECREF(result);
            if (! frame) {
              Py_DECREF(element);
              return KOS_ITER_ERROR;
            }
            f->pending = element;
            f->busy = 1;
            f->nf_phase = KOS_FILTER_NF_WAIT_VERDICT;
            return KOS_ITER_INSTALLED_VALUE;
          }
          default:
            Py_DECREF(result);
            Py_DECREF(element);
            return KOS_ITER_ERROR;
        }
      }
    }
  }

  /* Driven source (KosIter provider or python async iterator): the
     whole step runs as the frame program, starting at FETCH. */
  if (! KosNativeFrame_Install(es, &kos_filter_program, f,
                               NULL, &f->cached_frame))
    return KOS_ITER_ERROR;
  f->busy = 1;
  f->nf_phase = KOS_FILTER_NF_FETCH;
  return KOS_ITER_INSTALLED_VALUE;
}


static KosIterStepResult filter_iter_throw(
    KosEvalState *es,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosFilterObject *f = (KosFilterObject *) self;

  (void) es;
  (void) out;

  if (f->busy) {
    /* A step is in flight and its driver owns the filter's state:
       every second caller is refused, GeneratorExit included -- the
       same rule CPython applies to a running async generator. */
    PyErr_SetString(PyExc_RuntimeError,
                    "filter is already being iterated");
    return KOS_ITER_ERROR;
  }

  if (PyErr_GivenExceptionMatches(throw_typ, PyExc_GeneratorExit)) {
    /* Generator close semantics: run the source's cleanup, mark
       closed, and let GeneratorExit propagate as the success shape. */
    f->closed = 1;
    f->busy = 0;
    kos_iter_close(f->source);
  }

  PyErr_SetObject(throw_typ, throw_exc);
  return KOS_ITER_ERROR;
}


static int filter_iter_close(PyObject *self) {

  KosFilterObject *f = (KosFilterObject *) self;

  if (f->closed)
    return 0;

  /* Declined while a step is in flight; that drive's completion owns
     the state. Reached from teardown paths, so no error is raised. */
  if (f->busy)
    return 0;

  f->closed = 1;
  f->busy = 0;
  kos_iter_close(f->source);

  return 0;
}


static void filter_iter_finish(PyObject *self, int exhausted) {

  KosFilterObject *f = (KosFilterObject *) self;

  f->busy = 0;
  Py_CLEAR(f->pending);
  if (exhausted)
    f->closed = 1;

}


/**
   The filter's durable drive context, created lazily: python-adapter
   drives host their work here across steps instead of allocating a
   context per element. The suite marks it recyclable, so the adapters
   reset it at their completion points; a creation failure falls back
   to an adapter-owned host.
*/
static KosContext *filter_iter_pyctx(PyObject *self) {

  KosFilterObject *f = (KosFilterObject *) self;

  if (! f->ctx) {
    f->ctx = KosContext_New(NULL);
    if (! f->ctx)
      PyErr_Clear();
  }
  return f->ctx;
}


/* === construction === */


/**
   KosFilter(func, iterable). func may be None, keeping truthy
   elements. The iterator is acquired eagerly with the PUSH_FOREACH
   preference order: a KosIter provider or any iterator passes through
   as itself, an async iterable yields its async iterator, otherwise
   the sync protocol.
*/
static PyObject *kosfilter_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *func = NULL;
  PyObject *iterable = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError,
                    "KosFilter takes no keyword arguments");
    return NULL;
  }
  if (! PyArg_ParseTuple(args, "OO:KosFilter", &func, &iterable))
    return NULL;

  if (func != Py_None && ! PyCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError,
                    "KosFilter func must be callable or None");
    return NULL;
  }

  PyObject *source = kos_iter_acquire(iterable);
  if (! source)
    return NULL;

  KosFilterObject *f = (KosFilterObject *) type->tp_alloc(type, 0);
  if (! f) {
    Py_DECREF(source);
    return NULL;
  }

  f->source = source;

  /* Resolve the predicate's calling shape and the source's protocol
     once; both are fixed for the filter's lifetime. */
  if (func == Py_None) {
    f->pred.cs_callable = NULL;
    f->pred.cs_slots = NULL;
    f->pred.cs_kind = KOS_CALL_SHAPE_PLAIN;
  } else {
    KosCallShape_Init(&f->pred, func);
  }
  f->src_sync = KosIter_CheckSync(source);

  f->pending = NULL;
  f->cached_frame = NULL;
  f->ctx = NULL;
  f->closed = 0;
  f->busy = 0;
  f->nf_phase = KOS_FILTER_NF_FETCH;
  return (PyObject *) f;
}


/* === GC + dealloc === */


static int kosfilter_traverse(PyObject *self, visitproc visit, void *arg) {

  KosFilterObject *f = (KosFilterObject *) self;

  Py_VISIT(f->pred.cs_callable);
  Py_VISIT(f->source);
  Py_VISIT(f->pending);
  if (f->ctx)
    return KosContext_Traverse(f->ctx, visit, arg);
  return 0;
}


static int kosfilter_clear(PyObject *self) {

  KosFilterObject *f = (KosFilterObject *) self;

  KosCallShape_Clear(&f->pred);
  Py_CLEAR(f->source);
  Py_CLEAR(f->pending);
  if (f->ctx) {
    KosContext *ctx = f->ctx;
    f->ctx = NULL;
    KosContext_Free(ctx);
  }
  return 0;
}


static void kosfilter_dealloc(PyObject *self) {

  KosFilterObject *f = (KosFilterObject *) self;

  PyObject_GC_UnTrack(self);
  if (f->cached_frame) {
    kos_dynframe_free(f->cached_frame);
    f->cached_frame = NULL;
  }
  kosfilter_clear(self);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *kosfilter_repr(PyObject *self) {

  return PyUnicode_FromFormat("<koskari filter object at %p>", self);
}


/* === Python surface === */


static PyObject *kosfilter_aiter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyMethodDef kosfilter_methods[] = {
  { "asend", (PyCFunction) KosIterAdapt_Asend, METH_O,
    "Send a value into the filter step; returns an awaitable" },

  { "athrow", (PyCFunction) KosIterAdapt_Athrow, METH_VARARGS,
    "Throw an exception into the filter; returns an awaitable" },

  { "aclose", (PyCFunction) KosIterAdapt_Aclose, METH_NOARGS,
    "Close the filter; returns an awaitable" },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods kosfilter_async_methods = {
  .am_aiter = kosfilter_aiter,
  .am_anext = KosIterAdapt_Anext,
};


KosEvaluatedTypeObject KosFilterType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.filter",
    .tp_basicsize = sizeof(KosFilterObject),
    .tp_itemsize = 0,
    .tp_dealloc = kosfilter_dealloc,
    .tp_repr = kosfilter_repr,
    .tp_as_async = &kosfilter_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosFilter(func, iterable) - native lazy filter; a None "
              "func keeps truthy elements; iterable both asynchronously "
              "(ambient await, koskari-flattened) and synchronously "
              "(demand path)",
    .tp_traverse = kosfilter_traverse,
    .tp_clear = kosfilter_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kosfilter_methods,
    .tp_new = kosfilter_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = filter_iter_step,
    .ki_throw = filter_iter_throw,
    .ki_close = filter_iter_close,
    .ki_finish = filter_iter_finish,
    .ki_pyctx = filter_iter_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


/* === Unit lifecycle === */


int koskari_filter_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosFilterType)))
    return -1;

  if (PyDict_SetItemString(
          dict, "KosFilter",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosFilterType)))
    return -1;

  /* short alias matching the class __name__ */
  if (PyDict_SetItemString(
          dict, "filter",
          (PyObject *) KOS_EVALUATED_PYTYPE(KosFilterType)))
    return -1;

  return 0;
}


int koskari_filter_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
