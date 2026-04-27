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
   koskari/lib/coro.c

   KosCoroutine and KosCoroutine_NewFromCode. Run code as coroutine;
   calling returns a Coroutine or AsyncGenerator.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Auto via Cursor
*/


#include "lib.h"
#include "debug.h"


static PyObject *coro_str_send = NULL;
static PyObject *coro_str_throw = NULL;
static PyObject *coro_str_close = NULL;


/* === Run code as coroutine === */


PyObject *KosAsyncGen_NewFromCodeAndNs(KosCode *code, PyObject *ns) {

  KosDynGen *gen = kos_dyngen_new(NULL);
  if (! gen) {
    Py_DECREF(ns);
    return NULL;
  }
  KosDynFrame *frame = kos_dynframe_new(
      (KosDynNode *) gen, code, ns, NULL);
  if (! frame) {
    kos_dyngen_free(gen);
    Py_DECREF(ns);
    return NULL;
  }
  KosDynGen_ACTIVE(gen) = (struct KosDynNode *) frame;
  KosContext *ctx = KosContext_New(frame);
  if (! ctx) {
    kos_dynframe_free(frame);
    kos_dyngen_free(gen);
    return NULL;
  }
  PyObject *agen = KosPyAsyncGen_New(ctx);
  if (! agen)
    return NULL;
  Py_XINCREF(KosCode_NAME(code));
  ((KosPyAsyncGen *) agen)->code_name = KosCode_NAME(code);
  KosDynGen_OWNER(gen) = (PyObject *) agen;
  ((KosPyAsyncGen *) agen)->gen = (void *) gen;
  kos_dyngen_acquire(gen);
  return agen;
}


PyObject *KosCoroutine_NewFromCode(PyObject *code, PyObject *ns) {

  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "expected Code, got %s",
                 Py_TYPE(code)->tp_name);
    return NULL;
  }

  if (! ns) {
    PyErr_SetString(PyExc_TypeError, "namespace is required");
    return NULL;
  }

  Py_INCREF(ns);

  if (KosCode_YIELDS((KosCode *) code))
    return KosAsyncGen_NewFromCodeAndNs((KosCode *) code, ns);

  KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code, ns, NULL);
  if (! frame)
    return NULL;

  KosContext *ctx = KosContext_New(frame);
  if (! ctx) {
    kos_dynframe_free(frame);
    return NULL;
  }
  return KosCoroutine_New(ctx);
}


/* === KosCoroutine type === */


static void coroutine_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosCoroutine *coro = (KosCoroutine *) self;

  if (coro->ctx)
    KosContext_Free(coro->ctx);

  Py_XDECREF(coro->pending_await);

  Py_TYPE(self)->tp_free(self);
}


static int coroutine_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosCoroutine *coro = (KosCoroutine *) self;

  Py_VISIT(coro->pending_await);

  if (coro->ctx) {
    return KosContext_Traverse(coro->ctx, visit, arg);
  }

  return 0;
}


static int coroutine_clear(PyObject *self) {

  KosCoroutine *coro = (KosCoroutine *) self;

  Py_CLEAR(coro->pending_await);

  if (coro->ctx) {
    KosContext_Free(coro->ctx);
    coro->ctx = NULL;
  }

  return 0;
}


static PyObject *coroutine_send(PyObject *self, PyObject *value);
static PyObject *coroutine_throw(PyObject *self, PyObject *args);
static PyObject *coroutine_close(PyObject *self);


static PyObject *coroutine_await(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *coroutine_iternext(PyObject *self) {

  return coroutine_send(self, Py_None);
}


static PyObject *coroutine_send(PyObject *self, PyObject *value) {

  KosCoroutine *coro = (KosCoroutine *) self;

  /* Re-entrancy guard */
  if (coro->in_send) {
    PyErr_SetString(PyExc_RuntimeError,
                    "coroutine already being awaited");
    return NULL;
  }

  /* Check state */
  if (coro->state == KOS_CORO_DONE) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  /* If suspended on yield, pass value as send_val */
  PyObject *send_val = NULL;
  PyObject *throw_exc = NULL;
  if (coro->state == KOS_CORO_YIELDED) {
    send_val = (value != NULL) ? value : Py_None;
    Py_INCREF(send_val);
    coro->state = KOS_CORO_RUNNING;
  }

  /* If waiting on Python awaitable, forward to it */
  if (coro->state == KOS_CORO_WAITING_PY) {
    KosEvalState coro_es;
    KosEvalState_Init(&coro_es, coro->ctx);
    if (KosEvalState_PythonAwaitIter(&coro_es)) {
      PyObject *result = PyObject_CallMethodObjArgs(
          KosEvalState_PythonAwaitIter(&coro_es), coro_str_send, value, NULL);

      if (result) {
        /* Still yielding */
        return result;
      }

      if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
        /* Python awaitable completed - pass result as send_val and drive again */
        PyObject *await_result = NULL;
        PyGen_FetchStopIterationValue(&await_result);
        if (! await_result)
          await_result = Py_None;

        KosEvalState_PythonAwaitClearIter(&coro_es);
        coro->state = KOS_CORO_RUNNING;
        send_val = await_result;
        Py_INCREF(send_val);
        /* fall through to drive and handle ev */
      } else {
        /* Exception from Python awaitable */
        KosEvalState_PythonAwaitClearIter(&coro_es);
        coro->state = KOS_CORO_DONE;
        return NULL;
      }
    }
  }

  coro->in_send = 1;
  coro->state = KOS_CORO_RUNNING;

  KosEvent ev = {0};
  int res = koskari_eval(coro->ctx, &ev, send_val,
                        throw_exc ? (PyObject *) Py_TYPE(throw_exc) : NULL,
                        throw_exc);
  Py_XDECREF(send_val);

  coro->in_send = 0;

  SAYF("[coroutine_send] koskari_eval -> res=%d KosEvent_KIND(&ev)=%d, payload=%R\n",
        res, KosEvent_KIND(&ev), KosEvent_PAYLOAD(&ev));

  if (res < 0) {
    coro->state = KOS_CORO_DONE;
    return NULL;
  }

  switch (KosEvent_KIND(&ev)) {
    case KOS_EV_RETURN:
      coro->state = KOS_CORO_DONE;

      // we do this manually to bypass the problem with tuple payloads.
      // There is an annoying extra behavior in Python where if you set an
      // exception with a tuple payload, it unpacks it as the args for the
      // exception. So we'll create the exception instance ourselves to
      // avoid that.
      PyObject *err = PyObject_CallOneArg(PyExc_StopIteration, KosEvent_PAYLOAD(&ev));
      PyErr_SetObject(PyExc_StopIteration, err);
      Py_DECREF(err);

      Py_XDECREF(KosEvent_PAYLOAD(&ev));
      return NULL;

    case KOS_EV_RAISE:
      coro->state = KOS_CORO_DONE;
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
        PyErr_SetNone(PyExc_StopIteration);
      }
      return NULL;

    case KOS_EV_YIELD:
      coro->state = KOS_CORO_YIELDED;
      return KosEvent_PAYLOAD(&ev);

    case KOS_EV_AWAIT: {
      SAYF("[eval] coroutine_send KOS_EV_AWAIT branch (payload=%d)\n",
            KosEvent_PAYLOAD(&ev) ? 1 : 0);
      if (! KosEvent_PAYLOAD(&ev)) {
        PyErr_SetString(PyExc_RuntimeError,
                        "KOS_EV_AWAIT event has NULL payload");
        coro->state = KOS_CORO_DONE;
        return NULL;
      }
      /* Driver already stepped the awaitable in push_or_await and set
          ctx->py_await_iter. Do not call eager_step_awaitable again (would
          send(None) twice and corrupt state). Just suspend and return the
          yielded value to the event loop. */
      coro->state = KOS_CORO_WAITING_PY;
      return KosEvent_PAYLOAD(&ev);  /* transfer ownership to caller (asyncio) */
    }

    case KOS_EV_NONE:
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      return NULL;

    default:
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      return NULL;
  }
}


static PyObject *coroutine_throw(PyObject *self, PyObject *args) {

  KosCoroutine *coro = (KosCoroutine *) self;

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  if (! PyArg_ParseTuple(args, "O|OO:throw", &typ, &val, &tb))
    return NULL;

  if (coro->state == KOS_CORO_DONE) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  /* If yielded, throw will inject exception and run (no special case) */
  /* If waiting on Python awaitable, forward throw */
  if (coro->state == KOS_CORO_WAITING_PY) {
    KosEvalState coro_es;
    KosEvalState_Init(&coro_es, coro->ctx);
    if (KosEvalState_PythonAwaitIter(&coro_es)) {
      PyObject *result = PyObject_CallMethodObjArgs(
          KosEvalState_PythonAwaitIter(&coro_es), coro_str_throw, typ, val, tb,
          NULL);

      if (result) {
        return result;
      }

      if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
        PyObject *await_result = NULL;
        PyGen_FetchStopIterationValue(&await_result);
        if (! await_result)
          await_result = Py_None;
        KosEvalState_PythonAwaitClearIter(&coro_es);
        coro->state = KOS_CORO_RUNNING;
        KosEvalState_PythonResumeSet(&coro_es, await_result, KOS_RESUME_AWAIT);
        return coroutine_send(self, Py_None);
      } else {
        KosEvalState_PythonAwaitClearIter(&coro_es);
        coro->state = KOS_CORO_DONE;
        return NULL;
      }
    }
  }

  /* Normalize exception; pass as throw_exc to evaluator */
  PyErr_NormalizeException(&typ, &val, &tb);
  PyObject *exc = val;
  if (! exc)
    exc = typ;
  Py_XINCREF(exc);

  coro->in_send = 1;
  coro->state = KOS_CORO_RUNNING;
  KosEvent ev = {0};
  int res = koskari_eval(coro->ctx, &ev, NULL, typ, exc);
  Py_DECREF(exc);
  coro->in_send = 0;

  if (res < 0) {
    coro->state = KOS_CORO_DONE;
    return NULL;
  }
  switch (KosEvent_KIND(&ev)) {
    case KOS_EV_RETURN:
      coro->state = KOS_CORO_DONE;
      PyObject *err = PyObject_CallOneArg(PyExc_StopIteration, KosEvent_PAYLOAD(&ev));
      PyErr_SetObject(PyExc_StopIteration, err);
      Py_DECREF(err);
      Py_XDECREF(KosEvent_PAYLOAD(&ev));
      return NULL;
    case KOS_EV_RAISE:
      coro->state = KOS_CORO_DONE;
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
        PyErr_SetNone(PyExc_StopIteration);
      }
      return NULL;
    case KOS_EV_YIELD:
      coro->state = KOS_CORO_YIELDED;
      return KosEvent_PAYLOAD(&ev);
    case KOS_EV_AWAIT:
      if (! KosEvent_PAYLOAD(&ev)) {
        PyErr_SetString(PyExc_RuntimeError, "KOS_EV_AWAIT event has NULL payload");
        coro->state = KOS_CORO_DONE;
        return NULL;
      }
      coro->state = KOS_CORO_WAITING_PY;
      return KosEvent_PAYLOAD(&ev);
    case KOS_EV_NONE:
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      return NULL;
    default:
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      return NULL;
  }
}


static PyObject *coroutine_close(PyObject *self) {

  KosCoroutine *coro = (KosCoroutine *) self;

  if (coro->state == KOS_CORO_DONE) {
    Py_RETURN_NONE;
  }

  /* Close any pending Python awaitable */
  {
    KosEvalState coro_es;
    KosEvalState_Init(&coro_es, coro->ctx);
    if (KosEvalState_PythonAwaitIter(&coro_es)) {
      PyObject *result = PyObject_CallMethodObjArgs(
          KosEvalState_PythonAwaitIter(&coro_es), coro_str_close, NULL);
      Py_XDECREF(result);
      PyErr_Clear();
      KosEvalState_PythonAwaitClearIter(&coro_es);
    }
  }

  /* Throw GeneratorExit */
  PyObject *close_args = Py_BuildValue("(O)", PyExc_GeneratorExit);
  if (! close_args)
    return NULL;

  PyObject *result = coroutine_throw(self, close_args);
  Py_DECREF(close_args);

  if (result) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    "coroutine ignored GeneratorExit");
    return NULL;
  }

  if (PyErr_ExceptionMatches(PyExc_StopIteration) ||
      PyErr_ExceptionMatches(PyExc_GeneratorExit)) {
    PyErr_Clear();
    coro->state = KOS_CORO_DONE;
    Py_RETURN_NONE;
  }

  return NULL;
}


/*
   demand() – Run coroutine to completion on the demand path (no asyncio).
   If the coroutine suspends on a Python awaitable, raises RuntimeError.
*/
static PyObject *coroutine_demand(PyObject *self) {

  KosCoroutine *coro = (KosCoroutine *) self;

  for (;;) {
    PyObject *step = coroutine_send(self, Py_None);

    if (! step) {
      if (PyErr_Occurred()) {
        if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
          PyObject *val = NULL;

          (void) PyGen_FetchStopIterationValue(&val);
          if (! val)
            Py_RETURN_NONE;
          return val;
        }
        return NULL;
      }
      return NULL;
    }

    if (coro->state == KOS_CORO_WAITING_PY) {
      Py_DECREF(step);
      PyErr_SetString(PyExc_RuntimeError,
                      "coroutine.demand() cannot complete: coroutine is "
                      "waiting on a Python awaitable; use async await");
      return NULL;
    }

    Py_DECREF(step);
  }
}


static PyMethodDef coroutine_methods[] = {
  { "send", (PyCFunction) coroutine_send, METH_O,
    "Send a value into the coroutine" },

  { "throw", (PyCFunction) coroutine_throw, METH_VARARGS,
    "Throw an exception into the coroutine" },

  { "close", (PyCFunction) coroutine_close, METH_NOARGS,
    "Close the coroutine" },

  { "demand", (PyCFunction) coroutine_demand, METH_NOARGS,
    "Run coroutine to completion from sync context on the demand path "
    "(no asyncio). Raises if the coroutine waits on a Python awaitable." },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods coroutine_async_methods = {
  .am_await = coroutine_await,
};


static PyObject *coroutine_repr(PyObject *self) {

  KosCoroutine *coro = (KosCoroutine *) self;
  const char *state_str;

  switch (coro->state) {
    case KOS_CORO_NEW:
      state_str = "new";
      break;
    case KOS_CORO_RUNNING:
      state_str = "running";
      break;
    case KOS_CORO_WAITING_PY:
      state_str = "waiting";
      break;
    case KOS_CORO_YIELDED:
      state_str = "yielded";
      break;
    case KOS_CORO_DONE:
      state_str = "done";
      break;
    default:
      state_str = "unknown";
  }

  return PyUnicode_FromFormat("<coroutine at %p [%s]>", self, state_str);
}


PyTypeObject KosCoroutineType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.Coroutine",
  .tp_basicsize = sizeof(KosCoroutine),
  .tp_itemsize = 0,
  .tp_dealloc = coroutine_dealloc,
  .tp_repr = coroutine_repr,
  .tp_as_async = &coroutine_async_methods,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Koskari coroutine wrapper",
  .tp_traverse = coroutine_traverse,
  .tp_clear = coroutine_clear,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = coroutine_iternext,
  .tp_methods = coroutine_methods,
};


PyObject *KosCoroutine_New(KosContext *ctx) {

  KosCoroutine *coro = PyObject_GC_New(KosCoroutine, &KosCoroutineType);
  if (! coro) {
    KosContext_Free(ctx);
    return NULL;
  }

  coro->ctx = ctx;
  coro->state = KOS_CORO_NEW;
  coro->pending_await = NULL;
  coro->in_send = 0;

  PyObject_GC_Track((PyObject *) coro);
  return (PyObject *) coro;
}


/* === Unit lifecycle === */


int koskari_coro_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  STR_CONST(coro_str_send, "send");
  STR_CONST(coro_str_throw, "throw");
  STR_CONST(coro_str_close, "close");

  if (PyType_Ready(&KosCoroutineType))
    return -1;

  if (PyDict_SetItemString(dict, "Coroutine",
                           (PyObject *) &KosCoroutineType))
    return -1;

  return 0;
}


int koskari_coro_clear(PyObject *mod) {

  Py_CLEAR(coro_str_send);
  Py_CLEAR(coro_str_throw);
  Py_CLEAR(coro_str_close);

  (void) mod;
  return 0;
}


// The end.
