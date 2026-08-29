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

   The fast path is pure C: a sync source element and a non-suspending
   function result complete as KOS_ITER_VALUE with the evaluator
   untouched. Anything asynchronous installs one native frame whose
   handler runs the two-phase machine -- FETCH requests the next source
   element through its own NEXT node (KosIterRequest_Next, all
   three protocols), INVOKE applies the function through the call path
   -- and completes by returning the element, delivered to the
   consumer's iteration node as the step value. Exhaustion and errors leave
   the frame as exceptions; the consumer's iteration node converts
   StopIteration per its own rules, which also gives a mapped
   function's StopIteration builtins.map truncation parity.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === instance === */


typedef enum {
  KOS_MAP_NF_FETCH = 0,   /* next wake starts a fetch */
  KOS_MAP_NF_WAIT_ELEM,   /* fetch in flight; element arrives by stack
                             push (node delivery) or AWAITED */
  KOS_MAP_NF_WAIT_RESULT, /* invoke in flight; result arrives by
                             CALL_VALUE, AWAITED, or stack push */
} KosMapNfPhase;


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
  uint8_t nf_phase;      /* KosMapNfPhase while the frame is live */
} KosMapObject;


/* === the step's native frame program === */


/**
   INVOKE from inside the frame program: apply func to element and
   either complete the frame (EVAL_RETURN delivers the element through
   the consumer's iteration node) or leave the invoke in flight. element is
   consumed.
*/
static KosOpResult map_nf_invoke(
    KosEvalState *es,
    KosMapObject *m,
    PyObject *element) {

  /* A frame-wanting KosCallable installs as a child frame; its return
     arrives as the CALL_VALUE wake. The element's own address is the
     argument vector. */
  if (m->fn.cs_kind == KOS_CALL_SHAPE_FRAME) {
    m->nf_phase = KOS_MAP_NF_WAIT_RESULT;
    {
      KosOpResult r = KosEvalState_Call(es, m->fn.cs_callable,
                                        &element, 1, NULL, 0);
      Py_DECREF(element);
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE)
        return EVAL_REPROLOGUE();
      return r;
    }
  }

  {
    PyObject *result = KosCallShape_PyCallOne(&m->fn, element);
    Py_DECREF(element);
    if (! result)
      return EVAL_RAISE();

    switch (KosEval_ResultIsValue(result)) {
      case 1:
        return KosNativeFrame_ReturnPark(es, result, &m->cached_frame);
      case 0: {
        /* Awaitable result: park; the completion wakes as AWAITED
           (or lands on the stack when it finishes eagerly). */
        m->nf_phase = KOS_MAP_NF_WAIT_RESULT;
        {
          KosOpResult pr = KosEvalState_PushOrAwait(es, result);
          if (pr.kind == KOS_RES_NORMAL || pr.kind == KOS_RES_REPROLOGUE)
            return EVAL_REPROLOGUE();
          return pr;
        }
      }
      default:
        Py_DECREF(result);
        return EVAL_RAISE();
    }
  }
}


/**
   FETCH from inside the frame program: one request for the next
   source element (KosNativeFrame_RequestNext). A completed element
   goes straight to INVOKE; installed or suspended work leaves the
   fetch in flight, the element arriving by stack delivery; exhaustion
   raises StopIteration for the consumer's iteration node to convert.
*/
static KosOpResult map_nf_fetch(KosEvalState *es, KosMapObject *m) {

  PyObject *out = NULL;

  switch (KosNativeFrame_RequestNext(es, m->source, &out)) {
    case KOS_IREQ_DELIVERED:
      return map_nf_invoke(es, m, out);

    case KOS_IREQ_EXHAUSTED:
      KosDynFrame_NATIVE_PROG(KosEvalState_FRAME(es)) = NULL;
      PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();

    case KOS_IREQ_CONTINUES:
      m->nf_phase = KOS_MAP_NF_WAIT_ELEM;
      return EVAL_REPROLOGUE();

    case KOS_IREQ_EXIT:
      m->nf_phase = KOS_MAP_NF_WAIT_ELEM;
      return EVAL_AWAIT(out);

    case KOS_IREQ_ERROR:
    default:
      return EVAL_RAISE();
  }
}


/**
   The step's native frame handler: the FETCH/INVOKE machine, woken by
   the eval loop's cadence, node deliveries landing on the frame
   stack, awaited resumes, and callee-frame returns. Exceptions are
   always declined -- the raise walk pops the frame and the consumer's
   node converts protocol exceptions to exhaustion.
*/
static KosOpResult map_nf_handler(
    KosEvalState *es,
    KosNativeFrameEvent ev,
    PyObject *value);


/* No clone entry: replaying a captured step would re-consume source
   elements, so continuation capture across a map step is refused. */
static const KosNativeProgram kos_map_program = {
  .np_name = "map-step",
  .np_handler = map_nf_handler,
  .np_clone_state = NULL,
  .np_free_state = NULL,
};


static KosOpResult map_nf_handler(
    KosEvalState *es,
    KosNativeFrameEvent ev,
    PyObject *value) {

  KosMapObject *m =
      (KosMapObject *) KosDynFrame_NATIVE_STATE(KosEvalState_FRAME(es));

  if (! m) {
    Py_XDECREF(value);
    PyErr_SetString(PyExc_SystemError, "map frame lost its state");
    return EVAL_RAISE();
  }

  switch (ev) {
    case KOS_NFRAME_EXCEPTION:
      /* Decline: the frame pops and the consumer's iteration node owns the
         conversion (StopIteration truncation parity included). */
      return EVAL_RAISE();

    case KOS_NFRAME_ITERATE:
      if (m->nf_phase == KOS_MAP_NF_FETCH)
        return map_nf_fetch(es, m);
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
      PyErr_SetString(PyExc_SystemError, "map frame: unexpected wake");
      return EVAL_RAISE();
  }

  if (m->nf_phase == KOS_MAP_NF_WAIT_ELEM)
    return map_nf_invoke(es, m, value);

  /* KOS_MAP_NF_WAIT_RESULT: the invoke completed -- or handed back
     another awaitable (a koskari function's coroutine); reclassify. */
  switch (KosEval_ResultIsValue(value)) {
    case 1:
      return KosNativeFrame_ReturnPark(es, value, &m->cached_frame);
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


static KosIterStepResult map_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosMapObject *m = (KosMapObject *) self;

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

    /* A frame-wanting koskari function installs directly through the
       generic call entry -- cached frame, the element's own address as
       the argument vector -- and its return delivers as the element.
       No helper frame is involved. */
    if (m->fn.cs_kind == KOS_CALL_SHAPE_FRAME) {
      KosOpResult r = KosEvalState_Call(es, m->fn.cs_callable,
                                        &element, 1, NULL, 0);
      Py_DECREF(element);
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE) {
        m->busy = 1;
        return KOS_ITER_INSTALLED_VALUE;
      }
      return KOS_ITER_ERROR;
    }

    {
      PyObject *result = KosCallShape_PyCallOne(&m->fn, element);
      Py_DECREF(element);
      if (! result)
        return KOS_ITER_ERROR;

      switch (KosEval_ResultIsValue(result)) {
        case 1:
          *out = result;
          return KOS_ITER_VALUE;
        case 0:
          /* Awaitable function result: stage it on the frame program,
             which owns every suspension. */
          break;
        default:
          Py_DECREF(result);
          return KOS_ITER_ERROR;
      }

      {
        KosDynFrame *frame = KosNativeFrame_Install(
            es, &kos_map_program, m, result, &m->cached_frame);
        Py_DECREF(result);
        if (! frame)
          return KOS_ITER_ERROR;
        m->busy = 1;
        m->nf_phase = KOS_MAP_NF_WAIT_RESULT;
        return KOS_ITER_INSTALLED_VALUE;
      }
    }
  }

  /* Driven source (KosIter provider or python async iterator): the
     whole step runs as the frame program, starting at FETCH. */
  if (! KosNativeFrame_Install(es, &kos_map_program, m, NULL,
                               &m->cached_frame))
    return KOS_ITER_ERROR;
  m->busy = 1;
  m->nf_phase = KOS_MAP_NF_FETCH;
  return KOS_ITER_INSTALLED_VALUE;
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
  m->nf_phase = KOS_MAP_NF_FETCH;
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
  if (m->cached_frame) {
    kos_dynframe_free(m->cached_frame);
    m->cached_frame = NULL;
  }
  kosmap_clear(self);
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
