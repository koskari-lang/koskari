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
   asynchronous runs under one CPS resume-carrying frame, whose
   resumes carry the three-phase progression -- FETCH requests the
   next source element (KosEvalState_NextResume), INVOKE applies the
   predicate (KosEvalState_CallResult) with the element held on the
   filter, VERDICT tests the predicate's result and either completes
   the frame with the held element or fetches again. A rejected
   element loops by a tail call into FETCH, never by C
   recursion, so an arbitrarily long run of rejects stays flat. The
   frame is parked on the filter between steps
   (KosNativeFrame_ReturnPark), stripped inert so the cache holds no
   references. Exhaustion and errors leave the frame as exceptions;
   the consumer's iteration node converts StopIteration per its own
   rules.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === instance === */


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
} KosFilterObject;


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   filter-step frame refuses by name (see the KosResumeFrame_Install
   call sites below and kos_dynframe_clone's resume refusal).

   The FETCH -> INVOKE -> VERDICT progression each step runs is
   carried by the resume set on the frame, not by a phase field: the
   resume function pointer is itself the state. */


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_do_fetch(
    KosEvalState *es, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_do_invoke(
    KosEvalState *es, void *state, PyObject *element);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_after_verdict(
    KosEvalState *es, void *state, PyObject *value);


/**
   Loop back to FETCH after a rejected element. A tail call, so an
   arbitrarily long reject run stays flat: each rejection reaches the
   next fetch by discarding its own frame, never by nesting, and never
   by a bounce through the trampoline.
*/
#define filter_refetch(es, f) \
  RESUME_HOP(filter_resume_do_fetch, (es), (f), NULL)


/**
   FETCH: request the next source element. Always entered with a NULL
   value -- either cold, from the driven-source install, or from
   filter_refetch after a rejected element. A NULL with an exception
   already set is the raise walk propagating that exception; this step
   always lets it propagate (the consumer's iteration node owns the
   conversion, StopIteration truncation parity included).
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_do_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosFilterObject *f = (KosFilterObject *) state;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  /* A sync source pulls directly through tp_iternext -- no iteration
     node -- so a rejected element costs one tail call. */
  if (f->src_sync) {
    PyObject *element = PyIter_Next(f->source);

    if (! element) {
      if (! PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }
    RESUME_HOP(filter_resume_do_invoke, es, f, element);
  }

  {
    KosOpResult r = KosEvalState_NextResume(es, f->source, NULL,
                                            filter_resume_do_invoke, f);
    RESULT_ENTRY(es, r, filter_resume_do_invoke, f);
  }
}


/**
   INVOKE: test element against the predicate. An immediate verdict
   resolves inline; a frame-wanting predicate goes through CallResult,
   the element held on the filter until its verdict settles. element
   is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_do_invoke(
    KosEvalState *es, void *state, PyObject *element) {

  KosFilterObject *f = (KosFilterObject *) state;

  if (! element)
    return EVAL_RAISE();

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
    filter_refetch(es, f);
  }

  /* Every calling shape goes through the one entry --
     KosEvalState_Call resolves the predicate and prepares (or declines
     to prepare) a frame per its own suite, so a predicate that wants
     no frame still runs in this evaluator rather than through the
     Python boundary. pending steals the element's reference and holds
     it across the call; the argument vector borrows it for the call's
     duration. */
  f->pending = element;
  {
    KosOpResult r = KosEvalState_CallResult(es, f->pred.cs_callable,
                                            &element, 1, NULL, 0,
                                            filter_resume_after_verdict, f);
    RESULT_ENTRY(es, r, filter_resume_after_verdict, f);
  }
}


/**
   VERDICT: a true verdict completes the step with the held element, a
   false one releases it and fetches again. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_after_verdict(
    KosEvalState *es, void *state, PyObject *value) {

  KosFilterObject *f = (KosFilterObject *) state;
  int ok;

  if (! value)
    return EVAL_RAISE();

  ok = PyObject_IsTrue(value);
  Py_DECREF(value);
  if (ok < 0)
    return EVAL_RAISE();

  if (ok) {
    PyObject *element = f->pending;

    f->pending = NULL;
    return KosNativeFrame_ReturnPark(es, element, &f->cached_frame);
  }

  Py_CLEAR(f->pending);
  filter_refetch(es, f);
}


/* === the iter suite === */



/**
   Undo a partially installed step before reporting KOS_ITER_ERROR:
   release the held element and pop everything pushed since the step
   began (the program frame, a failed call's prepped frame), so the
   suite contract -- a non-INSTALLED outcome leaves no partial state
   -- holds on every error exit. The step's real exception is fenced
   across the unwind.
*/
static KosIterStepResult filter_step_abort(
    KosEvalState *es,
    KosFilterObject *f,
    KosDynNode *pre_top) {

  Py_CLEAR(f->pending);

  if (KosEvalState_DYN_TOP(es) != pre_top) {
    PyObject *ftyp, *fval, *ftb;

    PyErr_Fetch(&ftyp, &fval, &ftb);
    {
      /* a leave in flight here is abandoned with the step */
      KosOpResult wr = kos_dyn_unwind_to_node(es, pre_top,
                                              KOS_XFER_EVENT_RAISE, NULL);
      if (wr.kind == KOS_RES_AWAIT)
        Py_XDECREF(wr.val);
    }
    PyErr_Restore(ftyp, fval, ftb);
  }
  return KOS_ITER_ERROR;
}


static KosIterStepResult filter_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosFilterObject *f = (KosFilterObject *) self;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);

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

      /* Any KosCallable predicate's verdict must come back to this
         step, so a resume-carrying frame installs here and the element
         is staged raw for filter_resume_do_invoke, which makes the
         call in this evaluator. filter_iter_step runs before
         koskari_eval drives this chain (see stepper_step), so the call
         itself -- which can install a callee or need an await -- must
         happen from inside that later dispatch, not here. */
      if (f->pred.cs_kind != KOS_CALL_SHAPE_PLAIN) {
        KosDynFrame *frame = KosResumeFrame_Install(
            es, "filter-step", &f->cached_frame);

        if (! frame) {
          Py_DECREF(element);
          return filter_step_abort(es, f, pre_top);
        }
        if (KosEvalState_StackPushRef(es, element) < 0) {
          Py_DECREF(element);
          return filter_step_abort(es, f, pre_top);
        }
        Py_DECREF(element);
        KosDynFrame_RESUME_FN(frame) = (void *) filter_resume_do_invoke;
        KosDynFrame_RESUME_STATE(frame) = f;
        f->busy = 1;
        return KOS_ITER_INSTALLED_VALUE;
      }

      /* A plain python predicate needs no evaluator at all. */
      {
        PyObject *result = PyObject_CallOneArg(f->pred.cs_callable, element);

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
            /* Awaitable verdict: stage it raw on a resume-carrying
               frame, which owns every suspension; the await itself
               happens from inside koskari_eval's own dispatch. */
            KosDynFrame *frame = KosResumeFrame_Install(
                es, "filter-step", &f->cached_frame);

            if (! frame) {
              Py_DECREF(result);
              Py_DECREF(element);
              return filter_step_abort(es, f, pre_top);
            }
            if (KosEvalState_StackPushRef(es, result) < 0) {
              Py_DECREF(result);
              Py_DECREF(element);
              return filter_step_abort(es, f, pre_top);
            }
            Py_DECREF(result);
            f->pending = element;  /* steals */
            KosDynFrame_RESUME_FN(frame) =
                (void *) filter_resume_after_verdict;
            KosDynFrame_RESUME_STATE(frame) = f;
            f->busy = 1;
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

  /* Driven source (KosIter provider or python async iterator): set the
     cold-start fetch resume and defer the whole step -- nothing is
     fetched yet to stage, and the request itself (which can suspend)
     must run from inside koskari_eval's own dispatch. */
  {
    KosDynFrame *frame = KosResumeFrame_Install(
        es, "filter-step", &f->cached_frame);

    if (! frame)
      return filter_step_abort(es, f, pre_top);
    KosDynFrame_RESUME_FN(frame) = (void *) filter_resume_do_fetch;
    KosDynFrame_RESUME_STATE(frame) = f;
    f->busy = 1;
    return KOS_ITER_INSTALLED_VALUE;
  }
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
  /* the context teardown runs first: a ReturnPark-armed frame still
     on the chain parks into cached_frame, and the free below reclaims
     it either way */
  kosfilter_clear(self);
  if (f->cached_frame) {
    kos_dynframe_free(f->cached_frame);
    f->cached_frame = NULL;
  }
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
