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


/* === KosPyAsyncGen type === */


/**
   Teardown a frozen async generator: throw GeneratorExit, run to completion,
   or fall back to pure cleanup if the gen attempts to suspend (yield/await).

   Assumes agen is frozen: agen->gen set, GEN.active holds chain,
   KosContext_DYN_TOP(agen->ctx) == NULL.

   On success (primary path): chain unwound, gen released, ctx ready for free.
   On fallback: chain freed, gen released, ctx ready for free.
   Does not free ctx; caller does that.  For clear, caller also sets
   agen->gen = NULL and agen->ctx = NULL.
*/
static void asyncgen_teardown_frozen(KosPyAsyncGen *agen) {
  KosDynGen *gen = (KosDynGen *) agen->gen;
  KosContext *ctx = agen->ctx;
  KosDynNode *active;
  PyObject *throw_exc = NULL;
  KosEvent ev = {0};
  int res;

  if (! gen || ! KosDynGen_ACTIVE(gen)) {
    /* No active chain; release and let caller free ctx */
    if (gen)
      kos_dyngen_release(gen);
    agen->gen = NULL;
    return;
  }

  throw_exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
  if (! throw_exc) {
    PyErr_Clear();
    goto fallback;
  }

  res = koskari_eval(ctx, &ev, NULL, PyExc_GeneratorExit, throw_exc);
  Py_DECREF(throw_exc);

  if (res < 0) {
    /* Exception path: clear GeneratorExit or StopIteration (success) */
    if (PyErr_ExceptionMatches(PyExc_GeneratorExit)
        || PyErr_ExceptionMatches(PyExc_StopIteration)) {
      PyErr_Clear();
      goto primary_success;
    }
    PyErr_Clear();
    goto fallback;
  }

  if (KosEvent_KIND(&ev) == KOS_EV_AWAIT || KosEvent_KIND(&ev) == KOS_EV_YIELD) {
    /* Suspend attempt during close; cannot honor */
    if (KosEvent_PAYLOAD(&ev))
      Py_DECREF(KosEvent_PAYLOAD(&ev));
    if (KosEvent_EXC_TYPE(&ev))
      Py_DECREF(KosEvent_EXC_TYPE(&ev));
    goto fallback;
  }

  if (KosEvent_KIND(&ev) == KOS_EV_RETURN) {
    if (KosEvent_PAYLOAD(&ev))
      Py_DECREF(KosEvent_PAYLOAD(&ev));
    goto primary_success;
  }

  if (KosEvent_KIND(&ev) == KOS_EV_RAISE) {
    if (PyErr_ExceptionMatches(PyExc_GeneratorExit)
        || PyErr_ExceptionMatches(PyExc_StopIteration)) {
      PyErr_Clear();
    } else {
      PyErr_Clear();
    }
    if (KosEvent_PAYLOAD(&ev))
      Py_DECREF(KosEvent_PAYLOAD(&ev));
    if (KosEvent_EXC_TYPE(&ev))
      Py_DECREF(KosEvent_EXC_TYPE(&ev));
    goto primary_success;
  }

  /* Unexpected event */
  if (KosEvent_PAYLOAD(&ev))
    Py_DECREF(KosEvent_PAYLOAD(&ev));
  if (KosEvent_EXC_TYPE(&ev))
    Py_DECREF(KosEvent_EXC_TYPE(&ev));
  goto fallback;

primary_success:
  if (kos_dyn_gen_unwind_exhausted(ctx) < 0) {
    goto fallback;
  }
  agen->gen = NULL;
  return;

fallback:
  PyErr_Clear();
  active = (KosDynNode *) KosDynGen_ACTIVE(gen);
  KosDynGen_ACTIVE(gen) = NULL;
  if (active)
    kos_dynchain_free(active, (KosDynNode *) gen);
  kos_dyngen_release(gen);
  agen->gen = NULL;
}


static void asyncgen_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->gen && agen->ctx && ! KosContext_DYN_TOP(agen->ctx)) {
    asyncgen_teardown_frozen(agen);
  } else if (agen->gen) {
    kos_dyngen_release((KosDynGen *) agen->gen);
    agen->gen = NULL;
  }
  if (agen->ctx)
    kos_context_free(agen->ctx);

  Py_TYPE(self)->tp_free(self);
}


static int asyncgen_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;
  int res;

  if (agen->gen && agen->ctx && ! KosContext_DYN_TOP(agen->ctx)) {
    res = kos_dynnode_traverse((KosDynNode *) agen->gen, visit, arg);
    if (res)
      return res;
  }
  if (agen->ctx)
    return kos_context_traverse(agen->ctx, visit, arg);

  return 0;
}


static int asyncgen_clear(PyObject *self) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) self;

  if (agen->gen && agen->ctx && ! KosContext_DYN_TOP(agen->ctx)) {
    asyncgen_teardown_frozen(agen);
  } else if (agen->gen) {
    kos_dyngen_release((KosDynGen *) agen->gen);
    agen->gen = NULL;
  }
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

  return KosPyAsyncGenASend_New(agen, NULL);
}


static PyObject *asyncgen_repr(PyObject *self) {

  return PyUnicode_FromFormat("<async_generator at %p>", self);
}


/*
   iter() – Return a sync iterator that drives this async generator.
   Reuses a single event loop per iteration session. Safe when an event
   loop is already running.
*/
static PyObject *asyncgen_iter(PyObject *self) {

  return Koskari_IterSync(self);
}

static PyMethodDef asyncgen_methods[] = {
  { "iter", (PyCFunction) asyncgen_iter, METH_NOARGS,
    "Return a synchronous iterator that drives this async generator. "
    "Reuses a loop per session. Safe when an event loop is already running." },
  { NULL, NULL, 0, NULL },
};


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
  Py_XDECREF(asend->throw_typ);
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
  Py_VISIT(asend->throw_typ);
  Py_VISIT(asend->throw_exc);

  return 0;
}


static int asyncgenasend_clear(PyObject *self) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_CLEAR(asend->agen);
  Py_CLEAR(asend->send_value);
  Py_CLEAR(asend->throw_typ);
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

  PyObject *send_val = asend->send_value;
  PyObject *throw_typ = asend->throw_typ;
  PyObject *throw_exc = asend->throw_exc;
  asend->send_value = NULL;
  asend->throw_typ = NULL;
  asend->throw_exc = NULL;

  /* Drive the context */
  KosEvent ev = {0};
  int res = koskari_eval(agen->ctx, &ev,
                        send_val,
                        throw_typ,
                        throw_exc);

  Py_XDECREF(send_val);
  Py_XDECREF(throw_typ);
  Py_XDECREF(throw_exc);

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
      if (KosEvent_EXC_TYPE(&ev)) {
        if (KosEvent_PAYLOAD(&ev)) {
          PyErr_SetObject(KosEvent_EXC_TYPE(&ev), KosEvent_PAYLOAD(&ev));
          Py_DECREF(KosEvent_PAYLOAD(&ev));
        } else {
          PyErr_SetNone(KosEvent_EXC_TYPE(&ev));
        }
        Py_XDECREF(KosEvent_EXC_TYPE(&ev));
      } else if (KosEvent_PAYLOAD(&ev)) {
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

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  Py_XINCREF(value);
  Py_CLEAR(asend->send_value);
  asend->send_value = value;
  asend->is_throw = 0;
  Py_CLEAR(asend->throw_typ);
  Py_CLEAR(asend->throw_exc);

  return asyncgenasend_iternext(self);
}


static PyObject *asyncgenasend_throw(PyObject *self, PyObject *args) {

  KosPyAsyncGenASend *asend = (KosPyAsyncGenASend *) self;

  asend->is_throw = 1;
  Py_CLEAR(asend->send_value);

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  if (! PyArg_ParseTuple(args, "O|OO:throw", &typ, &val, &tb))
    return NULL;

  PyErr_NormalizeException(&typ, &val, &tb);
  PyObject *exc = val;
  if (! exc)
    exc = typ;
  Py_INCREF(typ);
  Py_INCREF(exc);
  Py_CLEAR(asend->throw_typ);
  Py_CLEAR(asend->throw_exc);
  asend->throw_typ = typ;
  asend->throw_exc = exc;

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
  asend->throw_typ = NULL;
  asend->throw_exc = NULL;

  PyObject_GC_Track((PyObject *) asend);
  return (PyObject *) asend;
}


/* === Unit lifecycle === */


int koskari_iter_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosPyAsyncGenType) ||
      PyType_Ready(&KosPyAsyncGenASendType))
    return -1;

  if (PyDict_SetItemString(dict, "AsyncGenerator",
                           (PyObject *) &KosPyAsyncGenType))
    return -1;

  return 0;
}


int koskari_iter_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
