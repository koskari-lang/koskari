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

   ki_step is a two-phase machine: FETCH pulls the next source element,
   INVOKE applies the function through the kos call interface. Both
   phases complete synchronously in C on the fast path (sync source,
   non-suspending function: KOS_ITER_VALUE with the evaluator
   untouched). An asynchronous sub-operation -- a foreign __anext__ in
   FETCH, an awaitable function result in INVOKE -- parks the step on
   the site's await boundary as KOS_ITER_AWAIT; the awaited value
   re-enters through ki_resume (owner-only) and the phase field routes
   it. While parked, every fresh ki_step entry is refused.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === instance === */


typedef enum {
  KOS_MAP_IDLE = 0,     /* between steps */
  KOS_MAP_FETCHING,     /* parked on the source's __anext__ awaitable */
  KOS_MAP_INVOKING,     /* parked on the function result awaitable */
  KOS_MAP_CLOSED,       /* exhausted or closed */
} KosMapPhase;


typedef struct {
  PyObject_HEAD
  PyObject *func;       /* callable applied per element */
  PyObject *source;     /* acquired iterator (sync or async) */
  uint8_t phase;        /* KosMapPhase */
  uint8_t busy;         /* a step is in flight (parked); ki_finish clears */
} KosMapObject;


static void map_close_source(KosMapObject *m);


/* === step machine === */


/**
   Classify and complete the invoke result: a plain value (or an
   ambient-await-exempt Defer/future handle) is the element; any other
   awaitable parks the step. result is consumed.
*/
static KosIterStepResult map_take_invoke_result(
    KosMapObject *m,
    PyObject *result,
    PyObject **out) {

  PyTypeObject *typ = Py_TYPE(result);

  if (! typ->tp_as_async || ! typ->tp_as_async->am_await) {
    m->phase = KOS_MAP_IDLE;
    m->busy = 0;
    *out = result;
    return KOS_ITER_VALUE;
  }

  if (KosDefer_Check(result)) {
    m->phase = KOS_MAP_IDLE;
    m->busy = 0;
    *out = result;
    return KOS_ITER_VALUE;
  }

  {
    int isfut = KosEval_IsFuture(result);
    if (isfut < 0) {
      Py_DECREF(result);
      m->phase = KOS_MAP_IDLE;
      m->busy = 0;
      return KOS_ITER_ERROR;
    }
    if (isfut) {
      m->phase = KOS_MAP_IDLE;
      m->busy = 0;
      *out = result;
      return KOS_ITER_VALUE;
    }
  }

  /* Awaitable function result: park until it completes. */
  m->phase = KOS_MAP_INVOKING;
  m->busy = 1;
  *out = result;
  return KOS_ITER_AWAIT;
}


/**
   INVOKE: apply func to the element. A frame-wanting KosCallable is
   grafted in-eval when a live eval state is available: its frame runs
   as a child of the site's armed boundary and its return IS the
   element (KOS_ITER_INSTALLED_VALUE) -- no coroutine, ambient awaits
   inside the frame. Otherwise the call goes through the kos call
   interface at the Python boundary; an awaitable result (a koskari
   function's coroutine included) parks the step. element is consumed.
*/
static KosIterStepResult map_invoke(
    KosEvalState *es,
    KosMapObject *m,
    PyObject *element,
    PyObject **out) {

  PyObject *args = PyTuple_Pack(1, element);
  Py_DECREF(element);
  if (! args) {
    m->phase = KOS_MAP_IDLE;
    m->busy = 0;
    return KOS_ITER_ERROR;
  }

  if (es) {
    KosCallSlots *cslots;
    if (KosCallable_CheckSlots(m->func, &cslots)
        && KosCallSlots_WantsFrame(cslots, m->func)) {
      /* The graft pushes a callee frame mid-dispatch: flush the
         consumer's live registers first so its stored pc is the
         resume point after delivery, exactly as the CALL path does. */
      KosEvalState_FlushLiveRegsToFrame(es);
      KosOpResult r = KosEvalState_CallArgs(es, m->func, args, NULL, 0);
      Py_DECREF(args);
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE) {
        m->phase = KOS_MAP_INVOKING;
        m->busy = 1;
        return KOS_ITER_INSTALLED_VALUE;
      }
      m->phase = KOS_MAP_IDLE;
      m->busy = 0;
      if (! PyErr_Occurred())
        PyErr_SetString(PyExc_SystemError,
                        "map invoke graft: unexpected install result");
      return KOS_ITER_ERROR;
    }
  }

  PyObject *result = Koskari_AsyncCall(m->func, args, NULL);
  Py_DECREF(args);
  if (! result) {
    m->phase = KOS_MAP_IDLE;
    m->busy = 0;
    return KOS_ITER_ERROR;
  }

  return map_take_invoke_result(m, result, out);
}


/**
   FETCH: pull the next source element. A sync iterator completes
   immediately; an async iterator parks on its __anext__ awaitable.
*/
static KosIterStepResult map_fetch(
    KosEvalState *es,
    KosMapObject *m,
    PyObject **out) {

  PyTypeObject *styp = Py_TYPE(m->source);

  if (styp->tp_iternext) {
    PyObject *element = styp->tp_iternext(m->source);
    if (element)
      return map_invoke(es, m, element, out);

    if (PyErr_Occurred()
        && ! PyErr_ExceptionMatches(PyExc_StopIteration)) {
      m->phase = KOS_MAP_IDLE;
      m->busy = 0;
      return KOS_ITER_ERROR;
    }
    PyErr_Clear();
    m->phase = KOS_MAP_CLOSED;
    m->busy = 0;
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  if (styp->tp_as_async && styp->tp_as_async->am_anext) {
    PyObject *step = styp->tp_as_async->am_anext(m->source);
    if (! step) {
      m->phase = KOS_MAP_IDLE;
      m->busy = 0;
      return KOS_ITER_ERROR;
    }
    {
      PyObject *awaitable = PyObject_GetAwaitable(step);
      Py_DECREF(step);
      if (! awaitable) {
        m->phase = KOS_MAP_IDLE;
        m->busy = 0;
        return KOS_ITER_ERROR;
      }
      m->phase = KOS_MAP_FETCHING;
      m->busy = 1;
      *out = awaitable;
      return KOS_ITER_AWAIT;
    }
  }

  m->phase = KOS_MAP_IDLE;
  m->busy = 0;
  PyErr_SetString(PyExc_TypeError, "KosMap source is not an iterator");
  return KOS_ITER_ERROR;
}


static KosIterStepResult map_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosMapObject *m = (KosMapObject *) self;

  if (m->phase == KOS_MAP_CLOSED) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  if (m->busy) {
    /* A parked step is resumed only through ki_resume by the park's
       owner; every fresh entry is refused. */
    PyErr_SetString(PyExc_RuntimeError, "map is already being iterated");
    return KOS_ITER_ERROR;
  }

  if (send_val && send_val != Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "can't send a value into a map; it takes none");
    return KOS_ITER_ERROR;
  }

  return map_fetch(es, m, out);
}


static KosIterStepResult map_iter_resume(
    KosEvalState *es,
    PyObject *self,
    PyObject *value,
    PyObject **out) {

  KosMapObject *m = (KosMapObject *) self;

  if (! m->busy || m->phase == KOS_MAP_CLOSED) {
    PyErr_SetString(PyExc_RuntimeError, "map was not parked");
    return KOS_ITER_ERROR;
  }

  if (m->phase == KOS_MAP_FETCHING) {
    Py_INCREF(value);
    m->busy = 0;
    return map_invoke(es, m, value, out);
  }

  /* KOS_MAP_INVOKING: the awaited result may itself be awaitable
     (a koskari fn returning a coroutine); reclassify. */
  Py_INCREF(value);
  return map_take_invoke_result(m, value, out);
}


static KosIterStepResult map_iter_throw(
    KosEvalState *es,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosMapObject *m = (KosMapObject *) self;

  (void) es;

  if (m->busy && m->phase == KOS_MAP_FETCHING
      && (PyErr_GivenExceptionMatches(throw_typ, PyExc_StopAsyncIteration)
          || PyErr_GivenExceptionMatches(throw_typ, PyExc_StopIteration))) {
    /* Mid-fetch end-of-source: the map is exhausted, mediated here
       rather than at the site's raise walk. */
    m->phase = KOS_MAP_CLOSED;
    m->busy = 0;
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  if (PyErr_GivenExceptionMatches(throw_typ, PyExc_GeneratorExit)) {
    /* Generator close semantics: run the source's cleanup, mark
       closed, and let GeneratorExit propagate as the success shape. */
    m->phase = KOS_MAP_CLOSED;
    m->busy = 0;
    map_close_source(m);
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_ITER_ERROR;
  }

  m->phase = (m->phase == KOS_MAP_CLOSED) ? KOS_MAP_CLOSED : KOS_MAP_IDLE;
  m->busy = 0;
  PyErr_SetObject(throw_typ, throw_exc);
  return KOS_ITER_ERROR;
}


/**
   Close the source, best-effort and exception-fenced. A provider
   closes through its suite; a sync iterator through close(); an async
   iterator through aclose(), whose awaitable is demand-driven -- an
   immediate completion honors the cleanup, a genuine suspension takes
   the cannot-honor fallback of closing the in-flight iterator (its
   GeneratorExit runs the cleanup synchronously where possible), the
   same posture the generator's demand-path teardown takes.
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

  if (m->phase == KOS_MAP_CLOSED)
    return 0;

  m->phase = KOS_MAP_CLOSED;
  m->busy = 0;
  map_close_source(m);

  return 0;
}


static void map_iter_finish(PyObject *self, int exhausted) {

  KosMapObject *m = (KosMapObject *) self;

  m->busy = 0;
  if (exhausted)
    m->phase = KOS_MAP_CLOSED;
  else if (m->phase != KOS_MAP_CLOSED)
    m->phase = KOS_MAP_IDLE;
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
  m->phase = KOS_MAP_IDLE;
  m->busy = 0;
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
    .ki_resume = map_iter_resume,
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
