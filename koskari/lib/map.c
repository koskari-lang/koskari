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
   koskari/lib/map.c

   KosMap: a native lazy map over one source, duck-typed against
   builtins.map, implemented as a KosIter provider.

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


/* === instance === */


typedef struct {
  PyObject_HEAD
  KosCallShape fn;       /* callable applied per element, shape
                            resolved at construction */
  PyObject *source;      /* acquired iterator (sync or async) */
  KosContext *ctx;       /* durable drive context (ki_pyctx), created
                            lazily; adapter-recycled, freed with
                            the map */
  KosDynFrame *cached_frame; /* inert frame parked between steps */
  uint8_t src_sync;      /* source steps through tp_iternext alone */
  uint8_t closed;        /* exhausted or closed */
  uint8_t busy;          /* a step is in flight; ki_finish clears */
} KosMapObject;


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   map-step frame refuses by name (see KosResumeFrame_Install call
   sites below and kos_dynframe_clone's resume refusal). */


/**
   The invoke completed. The value is settled by contract, so the step
   completes with it directly. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_after_result(
    KosEvalState *es, void *state, PyObject *value) {

  KosMapObject *m = (KosMapObject *) state;

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

  KosMapObject *m = (KosMapObject *) state;
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
      es, ((KosMapObject *) state)->source, NULL,
      map_resume_after_fetch, state);
  RESULT_ENTRY(es, r, map_resume_after_fetch, state);
}


/* === the iter suite === */



/**
   Undo a partially installed step before reporting KOS_ITER_ERROR:
   pop everything pushed since the step began (the program frame, a
   failed call's prepped frame), so the suite contract -- a
   non-INSTALLED outcome leaves no partial state -- holds on every
   error exit. The step's real exception is fenced across the unwind.
*/
static KosIterStepResult map_step_abort(
    KosEvalState *es,
    KosDynNode *pre_top) {

  if (KosEvalState_DYN_TOP(es) != pre_top) {
    PyObject *ftyp, *fval, *ftb;

    PyErr_Fetch(&ftyp, &fval, &ftb);
    {
      /* a leave in flight here is abandoned with the step */
      KosOpResult wr = kos_dyn_unwind_to_node(es, pre_top,
                                              KOS_XFER_EVENT_RAISE);
      if (wr.kind == KOS_RES_AWAIT)
        Py_XDECREF(wr.val);
    }
    PyErr_Restore(ftyp, fval, ftb);
  }
  return KOS_ITER_ERROR;
}


static KosIterStepResult map_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosMapObject *m = (KosMapObject *) self;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);

  if (m->closed) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  if (m->busy) {
    PyErr_SetString(PyExc_RuntimeError, "map is already being iterated");
    return KOS_ITER_ERROR;
  }

  if (send_val && send_val != Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "can't send a value into a map; it takes none");
    return KOS_ITER_ERROR;
  }

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
      return map_step_abort(es, pre_top);
    }

    /* Any other KosCallable -- a values or bound-method FRAME shape
       without the frame-return guarantee, or one wanting no frame at
       all: stage the element on a resume-carrying frame and defer the
       call to map_resume_after_fetch, which runs it in this evaluator.
       map_iter_step runs before koskari_eval drives this chain (see
       stepper_step), so the call itself -- which can install a callee
       or need an await -- must happen from inside that later dispatch,
       not here. */
    if (m->fn.cs_kind != KOS_CALL_SHAPE_PLAIN) {
      KosDynFrame *frame = KosResumeFrame_Install(
          es, "map-step", &m->cached_frame);
      if (! frame) {
        Py_DECREF(element);
        return map_step_abort(es, pre_top);
      }
      if (KosEvalState_StackPushRef(es, element) < 0) {
        Py_DECREF(element);
        return map_step_abort(es, pre_top);
      }
      Py_DECREF(element);
      KosDynFrame_RESUME_FN(frame) = (void *) map_resume_after_fetch;
      KosDynFrame_RESUME_STATE(frame) = m;
      m->busy = 1;
      return KOS_ITER_INSTALLED_VALUE;
    }

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
          break;
        default:
          Py_DECREF(result);
          return KOS_ITER_ERROR;
      }

      {
        KosDynFrame *frame = KosResumeFrame_Install(
            es, "map-step", &m->cached_frame);
        if (! frame) {
          Py_DECREF(result);
          return map_step_abort(es, pre_top);
        }
        if (KosEvalState_StackPushRef(es, result) < 0) {
          Py_DECREF(result);
          return map_step_abort(es, pre_top);
        }
        Py_DECREF(result);
        KosDynFrame_RESUME_FN(frame) = (void *) map_resume_after_result;
        KosDynFrame_RESUME_STATE(frame) = m;
        m->busy = 1;
        return KOS_ITER_INSTALLED_VALUE;
      }
    }
  }

  /* Driven source (KosIter provider or python async iterator): set the
     cold-start kickoff resume and defer the first fetch entirely --
     nothing is fetched yet to stage, and the request itself (which can
     suspend) must run from inside koskari_eval's own dispatch. */
  {
    KosDynFrame *frame = KosResumeFrame_Install(
        es, "map-step", &m->cached_frame);
    if (! frame)
      return map_step_abort(es, pre_top);
    KosDynFrame_RESUME_FN(frame) = (void *) map_resume_start_fetch;
    KosDynFrame_RESUME_STATE(frame) = m;
    m->busy = 1;
    return KOS_ITER_INSTALLED_VALUE;
  }
}




static KosIterStepResult map_iter_throw(
    KosEvalState *es,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosMapObject *m = (KosMapObject *) self;

  (void) es;
  (void) out;

  if (m->busy) {
    /* A step is in flight and its driver owns the map's state: every
       second caller is refused, GeneratorExit included -- the same
       rule CPython applies to a running async generator. */
    PyErr_SetString(PyExc_RuntimeError, "map is already being iterated");
    return KOS_ITER_ERROR;
  }

  if (PyErr_GivenExceptionMatches(throw_typ, PyExc_GeneratorExit)) {
    /* Generator close semantics: run the source's cleanup, mark
       closed, and let GeneratorExit propagate as the success shape. */
    m->closed = 1;
    m->busy = 0;
    kos_iter_close(m->source);
  }

  PyErr_SetObject(throw_typ, throw_exc);
  return KOS_ITER_ERROR;
}


static int map_iter_close(PyObject *self) {

  KosMapObject *m = (KosMapObject *) self;

  if (m->closed)
    return 0;

  /* Declined while a step is in flight; that drive's completion owns
     the state. Reached from teardown paths, so no error is raised. */
  if (m->busy)
    return 0;

  m->closed = 1;
  m->busy = 0;
  kos_iter_close(m->source);

  return 0;
}


static void map_iter_finish(PyObject *self, int exhausted) {

  KosMapObject *m = (KosMapObject *) self;

  m->busy = 0;
  if (exhausted)
    m->closed = 1;

}


/**
   The map's durable drive context, created lazily: python-adapter
   drives host their work here across steps instead of allocating a
   context per element. The suite marks it recyclable, so the adapters
   reset it at their completion points; a creation failure falls back
   to an adapter-owned host.
*/
static KosContext *map_iter_pyctx(PyObject *self) {

  KosMapObject *m = (KosMapObject *) self;

  if (! m->ctx) {
    m->ctx = KosContext_New(NULL);
    if (! m->ctx)
      PyErr_Clear();
  }
  return m->ctx;
}


/* === construction === */


/**
   KosMap(func, iterable). The iterator is acquired eagerly with the
   PUSH_FOREACH preference order: a KosIter provider or any iterator
   passes through as itself, an async iterable yields its async
   iterator, otherwise the sync protocol.
*/
static PyObject *kosmap_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *func = NULL;
  PyObject *iterable = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "KosMap takes no keyword arguments");
    return NULL;
  }
  if (! PyArg_ParseTuple(args, "OO:KosMap", &func, &iterable))
    return NULL;

  if (! PyCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError, "KosMap func must be callable");
    return NULL;
  }

  PyObject *source = kos_iter_acquire(iterable);
  if (! source)
    return NULL;

  KosMapObject *m = (KosMapObject *) type->tp_alloc(type, 0);
  if (! m) {
    Py_DECREF(source);
    return NULL;
  }

  m->source = source;

  /* Resolve the function's calling shape and the source's protocol
     once; both are fixed for the map's lifetime. */
  KosCallShape_Init(&m->fn, func);
  m->src_sync = KosIter_CheckSync(source);

  m->ctx = NULL;
  m->cached_frame = NULL;
  m->closed = 0;
  m->busy = 0;
  return (PyObject *) m;
}


/* === GC + dealloc === */


static int kosmap_traverse(PyObject *self, visitproc visit, void *arg) {

  KosMapObject *m = (KosMapObject *) self;

  Py_VISIT(m->fn.cs_callable);
  Py_VISIT(m->source);
  if (m->ctx)
    return KosContext_Traverse(m->ctx, visit, arg);
  return 0;
}


static int kosmap_clear(PyObject *self) {

  KosMapObject *m = (KosMapObject *) self;

  KosCallShape_Clear(&m->fn);
  Py_CLEAR(m->source);
  if (m->ctx) {
    KosContext *ctx = m->ctx;
    m->ctx = NULL;
    KosContext_Free(ctx);
  }
  return 0;
}


static void kosmap_dealloc(PyObject *self) {

  KosMapObject *m = (KosMapObject *) self;

  PyObject_GC_UnTrack(self);
  /* the context teardown runs first: a ReturnPark-armed frame still
     on the chain parks into cached_frame, and the free below reclaims
     it either way */
  kosmap_clear(self);
  if (m->cached_frame) {
    kos_dynframe_free(m->cached_frame);
    m->cached_frame = NULL;
  }
  Py_TYPE(self)->tp_free(self);
}


static PyObject *kosmap_repr(PyObject *self) {

  return PyUnicode_FromFormat("<koskari map object at %p>", self);
}


/* === Python surface === */


static PyObject *kosmap_aiter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyMethodDef kosmap_methods[] = {
  { "asend", (PyCFunction) KosIterAdapt_Asend, METH_O,
    "Send a value into the map step; returns an awaitable" },

  { "athrow", (PyCFunction) KosIterAdapt_Athrow, METH_VARARGS,
    "Throw an exception into the map; returns an awaitable" },

  { "aclose", (PyCFunction) KosIterAdapt_Aclose, METH_NOARGS,
    "Close the map; returns an awaitable" },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods kosmap_async_methods = {
  .am_aiter = kosmap_aiter,
  .am_anext = KosIterAdapt_Anext,
};


KosEvaluatedTypeObject KosMapType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.map",
    .tp_basicsize = sizeof(KosMapObject),
    .tp_itemsize = 0,
    .tp_dealloc = kosmap_dealloc,
    .tp_repr = kosmap_repr,
    .tp_as_async = &kosmap_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosMap(func, iterable) - native lazy map; iterable both "
              "asynchronously (ambient await, koskari-flattened) and "
              "synchronously (demand path)",
    .tp_traverse = kosmap_traverse,
    .tp_clear = kosmap_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kosmap_methods,
    .tp_new = kosmap_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = map_iter_step,
    .ki_throw = map_iter_throw,
    .ki_close = map_iter_close,
    .ki_finish = map_iter_finish,
    .ki_pyctx = map_iter_pyctx,
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
