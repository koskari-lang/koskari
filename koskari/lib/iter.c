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
   koskari/lib/iter.c

   KosPyAsyncGen, KosPyAsyncGenASend, and the sync iterator adapter.
   Async iteration and sync layer for driving async generators.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Auto via Cursor
*/


#include "lib.h"


static PyObject *asyncio_run_func = NULL;


/* === KosPyAsyncGen type === */


static void asyncgen_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->ctx)
    kos_context_free(agen->ctx);

  Py_TYPE(self)->tp_free(self);
}


static int asyncgen_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->ctx) {
    return kos_context_traverse(agen->ctx, visit, arg);
  }

  return 0;
}


static int asyncgen_clear(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->ctx) {
    kos_context_free(agen->ctx);
    agen->ctx = NULL;
  }

  return 0;
}


static PyObject *asyncgen_aiter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *asyncgen_anext(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->closed) {
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  return KosPyAsyncGenASend_New(agen, Py_None);
}


static PyObject *asyncgen_repr(PyObject *self) {

  return PyUnicode_FromFormat("<async_generator at %p>", self);
}


/*
   iter() – UNSAFE synchronizer. Do not use when an event loop is already
   running; can deadlock. For tests and synchronous scripts only.
*/
static PyObject *asyncgen_iter(PyObject *self);

static PyMethodDef asyncgen_methods[] = {
  { "iter", (PyCFunction) asyncgen_iter, METH_NOARGS,
    "Return a synchronous iterator that drives this async generator via "
    "asyncio.run on each __anext__ (UNSAFE: do not use when an event loop "
    "is already running; can deadlock). For tests and synchronous scripts only." },
  { NULL, NULL, 0, NULL },
};


/* === KosPyAsyncGenSyncIter (internal sync iterator for asyncgen_iter) === */


typedef struct {
  PyObject_HEAD
  PyObject *agen;
} KosPyAsyncGenSyncIter;


static void asyncgen_synciter_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);
  KosPyAsyncGenSyncIter *it = (KosPyAsyncGenSyncIter *) self;
  Py_XDECREF(it->agen);
  Py_TYPE(self)->tp_free(self);
}


static int asyncgen_synciter_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyAsyncGenSyncIter *it = (KosPyAsyncGenSyncIter *) self;
  Py_VISIT(it->agen);
  return 0;
}


static int asyncgen_synciter_clear(PyObject *self) {

  KosPyAsyncGenSyncIter *it = (KosPyAsyncGenSyncIter *) self;
  Py_CLEAR(it->agen);
  return 0;
}


static PyObject *asyncgen_synciter_iternext(PyObject *self) {

  KosPyAsyncGenSyncIter *it = (KosPyAsyncGenSyncIter *) self;
  if (! it->agen || ! asyncio_run_func) {
    if (! asyncio_run_func)
      PyErr_SetString(PyExc_RuntimeError,
                      "asyncio.run not available (koskari.lib init failed?)");
    return NULL;
  }

  PyObject *awaitable = asyncgen_anext(it->agen);
  if (! awaitable)
    return NULL;

  PyObject *result = PyObject_CallOneArg(asyncio_run_func, awaitable);
  Py_DECREF(awaitable);

  if (result)
    return result;

  /* Exception set. Normal yield: __anext__ awaitable raises StopIteration
     with the yielded value (possibly in a 1-tuple). */
  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    PyObject *value = NULL;
    PyGen_FetchStopIterationValue(&value);
    if (value && PyTuple_Check(value) && PyTuple_GET_SIZE(value) == 1) {
      PyObject *item = PyTuple_GET_ITEM(value, 0);
      Py_INCREF(item);
      Py_DECREF(value);
      return item;
    }
    return value;
  }

  /* Generator exhausted: convert StopAsyncIteration to StopIteration. */
  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyObject *exc_typ = NULL, *exc_val = NULL, *exc_tb = NULL;
    PyErr_Fetch(&exc_typ, &exc_val, &exc_tb);
    PyObject *value = koskari_StopAsyncIteration_value(exc_val);
    Py_XDECREF(exc_typ);
    Py_XDECREF(exc_val);
    Py_XDECREF(exc_tb);
    if (value) {
      PyObject *stop = PyObject_CallOneArg(PyExc_StopIteration, value);
      Py_DECREF(value);
      if (stop) {
        PyErr_SetObject(PyExc_StopIteration, stop);
        Py_DECREF(stop);
      } else
        PyErr_SetNone(PyExc_StopIteration);
    } else
      PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  return NULL;
}


static PyTypeObject KosPyAsyncGenSyncIterType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib._AsyncGeneratorSyncIter",
  .tp_basicsize = sizeof(KosPyAsyncGenSyncIter),
  .tp_itemsize = 0,
  .tp_dealloc = asyncgen_synciter_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_traverse = asyncgen_synciter_traverse,
  .tp_clear = asyncgen_synciter_clear,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = asyncgen_synciter_iternext,
};


static PyObject *asyncgen_iter(PyObject *self) {

  if (! asyncio_run_func) {
    PyErr_SetString(PyExc_RuntimeError,
                    "asyncio.run not available (koskari.lib init failed?)");
    return NULL;
  }

  KosPyAsyncGenSyncIter *it = PyObject_GC_New(KosPyAsyncGenSyncIter,
                                              &KosPyAsyncGenSyncIterType);
  if (! it)
    return NULL;

  it->agen = self;
  Py_INCREF(self);
  PyObject_GC_Track((PyObject *) it);
  return (PyObject *) it;
}


static PyAsyncMethods asyncgen_async_methods = {
  .am_aiter = asyncgen_aiter,
  .am_anext = asyncgen_anext,
};


PyTypeObject KosPyAsyncGenType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.AsyncGenerator",
  .tp_basicsize = sizeof(KosPyAsyncGen),
  .tp_itemsize = 0,
  .tp_dealloc = asyncgen_dealloc,
  .tp_repr = asyncgen_repr,
  .tp_as_async = &asyncgen_async_methods,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Koskari async generator wrapper",
  .tp_traverse = asyncgen_traverse,
  .tp_clear = asyncgen_clear,
  .tp_methods = asyncgen_methods,
};


PyObject *KosPyAsyncGen_New(KosContext *ctx) {

  KosPyAsyncGen *agen = PyObject_GC_New(KosPyAsyncGen, &KosPyAsyncGenType);
  if (! agen) {
    kos_context_free(ctx);
    return NULL;
  }

  agen->ctx = ctx;
  agen->in_progress = 0;
  agen->closed = 0;

  PyObject_GC_Track((PyObject *) agen);
  return (PyObject *) agen;
}


/* === KosPyAsyncGenASend type === */


static void asyncgenasend_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_XDECREF(asend->agen);
  Py_XDECREF(asend->send_value);
  Py_XDECREF(asend->throw_exc);

  Py_TYPE(self)->tp_free(self);
}


static int asyncgenasend_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_VISIT(asend->agen);
  Py_VISIT(asend->send_value);
  Py_VISIT(asend->throw_exc);

  return 0;
}


static int asyncgenasend_clear(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_CLEAR(asend->agen);
  Py_CLEAR(asend->send_value);
  Py_CLEAR(asend->throw_exc);

  return 0;
}


static PyObject *asyncgenasend_await(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *asyncgenasend_iternext(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;
  KosPyAsyncGen *agen = asend->agen;

  if (! agen || ! agen->ctx) {
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  if (agen->closed) {
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  /* Re-entrancy guard */
  if (agen->in_progress) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator already being iterated");
    return NULL;
  }

  agen->in_progress = 1;

  /* Drive the context */
  KosEvent ev = {0};
  int res = koskari_eval(agen->ctx, &ev, NULL, NULL);

  agen->in_progress = 0;

  if (res < 0) {
    agen->closed = 1;
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  switch (KosEvent_KIND(&ev)) {
    case KOS_EV_RETURN:
      /* Generator completed; pass return value. */
      agen->closed = 1;
      {
        PyObject *payload = KosEvent_PAYLOAD(&ev);
        PyObject *exc = koskari_StopAsyncIteration_FromValue(
            payload ? payload : Py_None);
        if (KosEvent_PAYLOAD(&ev))
          Py_DECREF(KosEvent_PAYLOAD(&ev));
        if (exc) {
          PyErr_SetObject((PyObject *) Py_TYPE(exc), exc);
          Py_DECREF(exc);
        } else if (! PyErr_Occurred()) {
          PyErr_SetNone(PyExc_StopAsyncIteration);
        }
      }
      return NULL;

    case KOS_EV_RAISE:
      agen->closed = 1;
      if (KosEvent_PAYLOAD(&ev)) {
        PyErr_SetObject((PyObject *) Py_TYPE(KosEvent_PAYLOAD(&ev)), KosEvent_PAYLOAD(&ev));
        Py_DECREF(KosEvent_PAYLOAD(&ev));
      } else {
        PyErr_SetNone(PyExc_StopAsyncIteration);
      }
      return NULL;

    case KOS_EV_YIELD: {
      /* Yielded a value - wrap in StopIteration to signal completion
         of this __anext__ awaitable.  Wrap in 1-tuple so PyErr_SetObject
         does not unpack a tuple payload. */
      PyObject *args = PyTuple_Pack(1, KosEvent_PAYLOAD(&ev));
      Py_XDECREF(KosEvent_PAYLOAD(&ev));
      if (! args) {
        return NULL;
      }
      PyErr_SetObject(PyExc_StopIteration, args);
      Py_DECREF(args);
      return NULL;
    }

    case KOS_EV_AWAIT:
      /* Need to suspend on Python awaitable */
      /* Return the awaitable for the caller to await */
      return KosEvent_PAYLOAD(&ev);
  }

  PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
  return NULL;
}


static PyObject *asyncgenasend_send(PyObject *self, PyObject *value) {

  /* For now, just delegate to iternext */
  return asyncgenasend_iternext(self);
}


static PyObject *asyncgenasend_throw(PyObject *self, PyObject *args) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  /* Mark as throw and delegate */
  asend->is_throw = 1;

  PyObject *typ = NULL;
  if (! PyArg_ParseTuple(args, "O:throw", &typ))
    return NULL;

  Py_XINCREF(typ);
  Py_XDECREF(asend->throw_exc);
  asend->throw_exc = typ;

  return asyncgenasend_iternext(self);
}


static PyObject *asyncgenasend_close(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  if (asend->agen) {
    asend->agen->closed = 1;
  }

  Py_RETURN_NONE;
}


static PyMethodDef asyncgenasend_methods[] = {
  { "send", (PyCFunction) asyncgenasend_send, METH_O,
    "Send a value" },

  { "throw", (PyCFunction) asyncgenasend_throw, METH_VARARGS,
    "Throw an exception" },

  { "close", (PyCFunction) asyncgenasend_close, METH_NOARGS,
    "Close" },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods asyncgenasend_async_methods = {
  .am_await = asyncgenasend_await,
};


static PyObject *asyncgenasend_repr(PyObject *self) {

  return PyUnicode_FromFormat("<async_generator_asend at %p>", self);
}


PyTypeObject KosPyAsyncGenASendType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.async_generator_asend",
  .tp_basicsize = sizeof(KosPyAsyncGenASend),
  .tp_itemsize = 0,
  .tp_dealloc = asyncgenasend_dealloc,
  .tp_repr = asyncgenasend_repr,
  .tp_as_async = &asyncgenasend_async_methods,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Koskari async generator asend awaitable",
  .tp_traverse = asyncgenasend_traverse,
  .tp_clear = asyncgenasend_clear,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = asyncgenasend_iternext,
  .tp_methods = asyncgenasend_methods,
};


PyObject *KosPyAsyncGenASend_New(
    KosPyAsyncGen *agen,
    PyObject *send_value) {

  KosPyAsyncGenASend *asend = PyObject_GC_New(KosPyAsyncGenASend,
                                              &KosPyAsyncGenASendType);
  if (! asend)
    return NULL;

  Py_INCREF(agen);
  asend->agen = agen;

  Py_XINCREF(send_value);
  asend->send_value = send_value;

  asend->is_throw = 0;
  asend->throw_exc = NULL;

  PyObject_GC_Track((PyObject *) asend);
  return (PyObject *) asend;
}


/* === Unit lifecycle === */


int koskari_iter_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  {
    PyObject *asyncio_mod = PyImport_ImportModule("asyncio");
    if (asyncio_mod) {
      asyncio_run_func = PyObject_GetAttrString(asyncio_mod, "run");
      Py_DECREF(asyncio_mod);
    }
  }

  if (PyType_Ready(&KosPyAsyncGenSyncIterType) ||
      PyType_Ready(&KosPyAsyncGenType) ||
      PyType_Ready(&KosPyAsyncGenASendType))
    return -1;

  if (PyDict_SetItemString(dict, "AsyncGenerator",
                           (PyObject *) &KosPyAsyncGenType))
    return -1;

  return 0;
}


int koskari_iter_clear(PyObject *mod) {

  Py_CLEAR(asyncio_run_func);

  (void) mod;
  return 0;
}


// The end.
