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
   element through its own NEXT boundary (KosIterRequest_Next, all
   three protocols), INVOKE applies the function through the call path
   -- and completes by returning the element, delivered to the
   consumer's boundary as the step value. Exhaustion and errors leave
   the frame as exceptions; the consumer's boundary converts
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
                             push (boundary delivery) or AWAITED */
  KOS_MAP_NF_WAIT_RESULT, /* invoke in flight; result arrives by
                             CALL_VALUE, AWAITED, or stack push */
} KosMapNfPhase;


typedef struct {
  PyObject_HEAD
  PyObject *func;        /* callable applied per element */
  PyObject *source;      /* acquired iterator (sync or async) */
  uint8_t closed;        /* exhausted or closed */
  uint8_t busy;          /* a step is in flight; ki_finish clears */
  uint8_t nf_phase;      /* KosMapNfPhase while the frame is live */
} KosMapObject;


static void map_close_source(KosMapObject *m);


/* === the step's native frame program === */


/**
   Classify a function result: a plain value (or an ambient-await-exempt
   Defer/future handle) is the element; NULL means awaitable (the
   caller parks). result borrowed.

   :return: 1 element, 0 awaitable, -1 error (PyErr set)
*/
static int map_result_is_element(PyObject *result) {

  PyTypeObject *typ = Py_TYPE(result);

  if (! typ->tp_as_async || ! typ->tp_as_async->am_await)
    return 1;
  if (KosDefer_Check(result))
    return 1;
  return KosEval_IsFuture(result);
}


/**
   INVOKE from inside the frame program: apply func to element and
   either complete the frame (EVAL_RETURN delivers the element through
   the consumer's boundary) or leave the invoke in flight. element is
   consumed.
*/
static KosOpResult map_nf_invoke(
    KosEvalState *es,
    KosMapObject *m,
    PyObject *element) {

  /* A frame-wanting KosCallable grafts as a child frame; its return
     arrives as the CALL_VALUE wake. */
  {
    KosCallSlots *cslots;
    if (KosCallable_CheckSlots(m->func, &cslots)
        && KosCallSlots_WantsFrame(cslots, m->func)) {
      PyObject *args = PyTuple_Pack(1, element);
      Py_DECREF(element);
      if (! args)
        return EVAL_RAISE();
      m->nf_phase = KOS_MAP_NF_WAIT_RESULT;
      {
        KosOpResult r = KosEvalState_CallArgs(es, m->func, args, NULL, 0);
        Py_DECREF(args);
        if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE)
          return EVAL_REPROLOGUE();
        return r;
      }
    }
  }

  {
    PyObject *args = PyTuple_Pack(1, element);
    Py_DECREF(element);
    if (! args)
      return EVAL_RAISE();
    {
      PyObject *result = Koskari_AsyncCall(m->func, args, NULL);
      Py_DECREF(args);
      if (! result)
        return EVAL_RAISE();

      switch (map_result_is_element(result)) {
        case 1:
          KosDynFrame_NATIVE_PROG(KosEvalState_FRAME(es)) = NULL;
          return EVAL_RETURN(result);
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
}


/**
   FETCH from inside the frame program: one request against the map's
   own NEXT boundary. A completed element goes straight to INVOKE; a
   grafted or parked source leaves the fetch in flight (the element
   arrives by boundary delivery); exhaustion raises StopIteration for
   the consumer's boundary to convert.
*/
static KosOpResult map_nf_fetch(KosEvalState *es, KosMapObject *m) {

  PyObject *out = NULL;

  if (kos_dyn_push_next_boundary(es, m->source, NULL, 1) < 0)
    return EVAL_RAISE();

  switch (KosIterRequest_Next(es, KosEvalState_DYN_TOP(es), NULL, &out)) {
    case KOS_IREQ_DELIVERED:
      kos_dyn_next_abort_after_failed_attach(es);
      return map_nf_invoke(es, m, out);

    case KOS_IREQ_EXHAUSTED:
      Py_XDECREF(out);
      kos_dyn_next_abort_after_failed_attach(es);
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
      kos_dyn_next_abort_after_failed_attach(es);
      return EVAL_RAISE();
  }
}


/**
   The step's native frame handler: the FETCH/INVOKE machine, woken by
   the eval loop's cadence, boundary deliveries landing on the frame
   stack, awaited resumes, and callee-frame returns. Exceptions are
   always declined -- the raise walk pops the frame and the consumer's
   boundary converts protocol exceptions to exhaustion.
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
      /* Decline: the frame pops and the consumer's boundary owns the
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
  switch (map_result_is_element(value)) {
    case 1:
      KosDynFrame_NATIVE_PROG(KosEvalState_FRAME(es)) = NULL;
      return EVAL_RETURN(value);
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

  /* Fast path: a sync non-provider source and a non-suspending
     function result complete with the evaluator untouched. Anything
     else stages its progress on a fresh native frame and returns
     KOS_ITER_INSTALLED_VALUE -- the frame program owns every
     suspension, and its return delivers the element through the
     consumer's boundary. */
  {
    PyObject *element = NULL;
    PyObject *staged = NULL;      /* pushed onto the frame stack */
    uint8_t staged_phase = KOS_MAP_NF_FETCH;

    if (! KosIter_Check(m->source) && PyIter_Check(m->source)) {
      element = PyIter_Next(m->source);
      if (! element) {
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        m->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }

      {
        KosCallSlots *cslots;
        if (KosCallable_CheckSlots(m->func, &cslots)
            && KosCallSlots_WantsFrame(cslots, m->func)) {
          /* Frame-wanting function: stage the element; the frame
             program invokes it. */
          staged = element;
          staged_phase = KOS_MAP_NF_WAIT_ELEM;
        } else {
          PyObject *args = PyTuple_Pack(1, element);
          Py_DECREF(element);
          if (! args)
            return KOS_ITER_ERROR;
          {
            PyObject *result = Koskari_AsyncCall(m->func, args, NULL);
            Py_DECREF(args);
            if (! result)
              return KOS_ITER_ERROR;
            switch (map_result_is_element(result)) {
              case 1:
                *out = result;
                return KOS_ITER_VALUE;
              case 0:
                /* Awaitable function result: stage it; the frame
                   program awaits it. */
                staged = result;
                staged_phase = KOS_MAP_NF_WAIT_RESULT;
                break;
              default:
                Py_DECREF(result);
                return KOS_ITER_ERROR;
            }
          }
        }
      }
    }

    {
      KosDynFrame *frame = kos_dyn_push_frame_empty(es);
      if (! frame) {
        Py_XDECREF(staged);
        return KOS_ITER_ERROR;
      }
      KosDynFrame_NATIVE_PROG(frame) = (void *) &kos_map_program;
      KosDynFrame_NATIVE_STATE(frame) = (void *) m;

      if (staged) {
        if (KosEvalState_StackPushRef(es, staged) < 0) {
          Py_DECREF(staged);
          return KOS_ITER_ERROR;
        }
        Py_DECREF(staged);
      }

      m->busy = 1;
      m->nf_phase = staged_phase;
      return KOS_ITER_INSTALLED_VALUE;
    }
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
    map_close_source(m);
  }

  PyErr_SetObject(throw_typ, throw_exc);
  return KOS_ITER_ERROR;
}


/**
   Close the source, best-effort and exception-fenced. A provider
   closes through its suite; a sync iterator through close(); an async
   iterator through aclose(), whose awaitable is demand-driven -- an
   immediate completion honors the cleanup, a genuine suspension takes
   the cannot-honor fallback of closing the in-flight iterator.
*/
static void map_close_source(KosMapObject *m) {

  PyObject *ftyp, *fval, *ftb;

  if (! m->source)
    return;

  PyErr_Fetch(&ftyp, &fval, &ftb);

  {
    KosIterSlots *islots;

    if (KosIter_CheckSlots(m->source, &islots)) {
      (void) islots->ki_close(m->source);
      PyErr_Clear();

    } else if (Py_TYPE(m->source)->tp_iternext) {
      PyObject *r = PyObject_CallMethod(m->source, "close", NULL);
      Py_XDECREF(r);
      PyErr_Clear();

    } else {
      PyObject *aw = PyObject_CallMethod(m->source, "aclose", NULL);
      if (aw) {
        PyObject *it = PyObject_GetAwaitable(aw);
        Py_DECREF(aw);
        if (it) {
          PyObject *step = PyObject_CallMethod(it, "send", "O", Py_None);
          if (step) {
            /* Suspend attempt during close; cannot honor further --
               close the iterator so its GeneratorExit cleanup runs. */
            Py_DECREF(step);
            PyObject *r = PyObject_CallMethod(it, "close", NULL);
            Py_XDECREF(r);
          }
          Py_DECREF(it);
        }
      }
      PyErr_Clear();
    }
  }

  PyErr_Restore(ftyp, fval, ftb);
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
  map_close_source(m);

  return 0;
}


static void map_iter_finish(PyObject *self, int exhausted) {

  KosMapObject *m = (KosMapObject *) self;

  m->busy = 0;
  if (exhausted)
    m->closed = 1;
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

  PyObject *source = NULL;
  PyTypeObject *ityp = Py_TYPE(iterable);

  if (KosIter_Check(iterable) || PyIter_Check(iterable)) {
    Py_INCREF(iterable);
    source = iterable;
  } else if (ityp->tp_as_async && ityp->tp_as_async->am_aiter) {
    source = ityp->tp_as_async->am_aiter(iterable);
  } else {
    source = PyObject_GetIter(iterable);
  }
  if (! source)
    return NULL;

  KosMapObject *m = (KosMapObject *) type->tp_alloc(type, 0);
  if (! m) {
    Py_DECREF(source);
    return NULL;
  }

  Py_INCREF(func);
  m->func = func;
  m->source = source;
  m->closed = 0;
  m->busy = 0;
  m->nf_phase = KOS_MAP_NF_FETCH;
  return (PyObject *) m;
}


/* === GC + dealloc === */


static int kosmap_traverse(PyObject *self, visitproc visit, void *arg) {

  KosMapObject *m = (KosMapObject *) self;

  Py_VISIT(m->func);
  Py_VISIT(m->source);
  return 0;
}


static int kosmap_clear(PyObject *self) {

  KosMapObject *m = (KosMapObject *) self;

  Py_CLEAR(m->func);
  Py_CLEAR(m->source);
  return 0;
}


static void kosmap_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);
  kosmap_clear(self);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *kosmap_repr(PyObject *self) {

  return PyUnicode_FromFormat("<KosMap at %p>", self);
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

    .tp_name = "koskari.lib.KosMap",
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
    .ki_pyctx = NULL,
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

  return 0;
}


int koskari_map_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
