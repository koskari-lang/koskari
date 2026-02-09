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
   koskari/lib/func.c

   Expression and Function native types. Assembled from Code and
   namespace/formals the same way as sibilant-ceval; calling either
   returns a Coroutine.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Auto via Cursor
*/


#include "lib.h"
#include "debug.h"


/* === KosExpression: callable (code, ns) -> coroutine on __call__ === */


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *ns;
} KosExpression;


static int expression_init(KosExpression *self, PyObject *args,
                            PyObject *kwargs) {
  PyObject *code = NULL;
  PyObject *ns = NULL;

  (void) kwargs;
  if (! PyArg_ParseTuple(args, "OO:Expression", &code, &ns))
    return -1;
  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "expected Code, got %s",
                 Py_TYPE(code)->tp_name);
    return -1;
  }
  if (! ns) {
    PyErr_SetString(PyExc_TypeError, "namespace is required");
    return -1;
  }
  Py_INCREF(code);
  Py_INCREF(ns);
  self->code = code;
  self->ns = ns;
  return 0;
}


static void expression_dealloc(PyObject *self) {
  KosExpression *ex = (KosExpression *) self;
  Py_CLEAR(ex->code);
  Py_CLEAR(ex->ns);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *expression_call(PyObject *self, PyObject *args,
                                 PyObject *kwargs) {
  KosExpression *ex = (KosExpression *) self;

  (void) kwargs;
  if (PyTuple_GET_SIZE(args) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression() takes no arguments");
    return NULL;
  }
  return kos_run_code_as_coroutine(ex->code, ex->ns);
}


static PyMemberDef expression_members[] = {
  { "code", T_OBJECT, offsetof(KosExpression, code), READONLY, NULL },
  { "ns", T_OBJECT, offsetof(KosExpression, ns), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


PyTypeObject KosExpressionType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Expression",
  .tp_basicsize = sizeof(KosExpression),
  .tp_dealloc = expression_dealloc,
  .tp_repr = (reprfunc) NULL,
  .tp_call = expression_call,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Callable(Code, namespace). Calling with () returns a Coroutine.",
  .tp_init = (initproc) expression_init,
  .tp_new = PyType_GenericNew,
  .tp_members = expression_members,
};


/* === KosFunction: callable (code, base_ns, formals_spec) or (code, formals, base_ns) -> coroutine on __call__ === */


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *base_ns;
  PyObject *formals_spec;   /* set when created via Function(code, base_ns, formals_spec) */
  PyObject *formals;        /* set when created via Code.function(formals, parent_ns) */
} KosFunction;


static int function_init(KosFunction *self, PyObject *args,
                         PyObject *kwargs) {
  PyObject *code = NULL;
  PyObject *base_ns = NULL;
  PyObject *formals_spec = NULL;

  (void) kwargs;
  if (! PyArg_ParseTuple(args, "OOO:Function", &code, &base_ns, &formals_spec))
    return -1;
  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "expected Code, got %s",
                 Py_TYPE(code)->tp_name);
    return -1;
  }
  if (! KosFormalsSpec_CheckExact(formals_spec)) {
    PyErr_Format(PyExc_TypeError, "expected FormalsSpec, got %s",
                 Py_TYPE(formals_spec)->tp_name);
    return -1;
  }
  if (! base_ns) {
    PyErr_SetString(PyExc_TypeError, "base namespace is required");
    return -1;
  }
  Py_INCREF(code);
  Py_INCREF(base_ns);
  Py_INCREF(formals_spec);
  self->code = code;
  self->base_ns = base_ns;
  self->formals_spec = formals_spec;
  self->formals = NULL;
  return 0;
}


static void function_dealloc(PyObject *self) {
  KosFunction *fn = (KosFunction *) self;
  Py_CLEAR(fn->code);
  Py_CLEAR(fn->base_ns);
  Py_CLEAR(fn->formals_spec);
  Py_CLEAR(fn->formals);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *function_call(PyObject *self, PyObject *args,
                               PyObject *kwargs) {
  KosFunction *fn = (KosFunction *) self;
  PyObject *formals = NULL;
  PyObject *nspec = NULL;
  PyObject *frame_ns = NULL;
  PyObject *coro = NULL;

  if (! kwargs)
    kwargs = PyDict_New();
  else
    Py_INCREF(kwargs);

  if (fn->formals) {
    /* Assembled via Code.function(formals, parent_ns) */
    formals = fn->formals;
    Py_INCREF(formals);
    nspec = KosFormals_NamespaceSpec(formals, NULL);
    if (! nspec)
      goto fail;
    frame_ns = PyObject_CallMethod(nspec, "get_namespace", "O", fn->base_ns);
    Py_DECREF(nspec);
    nspec = NULL;
    if (! frame_ns)
      goto fail;
    if (KosFormals_Apply(formals, frame_ns, args, kwargs) < 0)
      goto fail;
    Py_DECREF(formals);
    formals = NULL;
  } else {
    /* Assembled via Function(code, base_ns, formals_spec) */
    formals = PyObject_CallObject(fn->formals_spec, NULL);
    if (! formals)
      goto fail;
    nspec = KosFormalsSpec_NamespaceSpec(fn->formals_spec, NULL);
    if (! nspec)
      goto fail;
    frame_ns = PyObject_CallMethod(nspec, "get_namespace", "O", fn->base_ns);
    Py_DECREF(nspec);
    nspec = NULL;
    if (! frame_ns)
      goto fail;
    if (KosFormals_Apply(formals, frame_ns, args, kwargs) < 0)
      goto fail;
    Py_DECREF(formals);
    formals = NULL;
  }

  Py_DECREF(kwargs);
  kwargs = NULL;
  coro = kos_run_code_as_coroutine(fn->code, frame_ns);
  Py_DECREF(frame_ns);
  return coro;

 fail:
  Py_XDECREF(formals);
  Py_XDECREF(nspec);
  Py_XDECREF(frame_ns);
  Py_XDECREF(kwargs);
  return NULL;
}


static PyMemberDef function_members[] = {
  { "code", T_OBJECT, offsetof(KosFunction, code), READONLY, NULL },
  { "base_ns", T_OBJECT, offsetof(KosFunction, base_ns), READONLY, NULL },
  { "formals_spec", T_OBJECT, offsetof(KosFunction, formals_spec), READONLY, NULL },
  { "formals", T_OBJECT, offsetof(KosFunction, formals), READONLY, NULL },
  { NULL, 0, 0, 0, NULL },
};


PyTypeObject KosFunctionType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Function",
  .tp_basicsize = sizeof(KosFunction),
  .tp_dealloc = function_dealloc,
  .tp_repr = (reprfunc) NULL,
  .tp_call = function_call,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "Callable(Code, base_namespace, FormalsSpec). Calling with "
             "(*args, **kwargs) binds formals and returns a Coroutine.",
  .tp_init = (initproc) function_init,
  .tp_new = PyType_GenericNew,
  .tp_members = function_members,
};


PyObject *KosExpression_New(PyObject *code, PyObject *ns) {

  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "Expression code must be Code, not %R", code);
    return NULL;
  }
  if (! ns) {
    PyErr_SetString(PyExc_TypeError, "Expression namespace is required");
    return NULL;
  }
  if (! KosNamespace_Check(ns)) {
    PyErr_Format(PyExc_TypeError, "Expression namespace must be Namespace,"
                 " not %R", ns);
    return NULL;
  }

  PyObject *self = PyType_GenericNew(&KosExpressionType, NULL, NULL);
  if (! self)
    return NULL;

  KosExpression *ex = (KosExpression *) self;
  Py_INCREF(code);
  Py_INCREF(ns);
  ex->code = code;
  ex->ns = ns;
  return self;
}


PyObject *KosFunction_NewFromFormals(PyObject *code,
                                      PyObject *formals,
                                      PyObject *parent_ns) {

  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "Function code must be Code, not %R", code);
    return NULL;
  }
  if (! KosFormals_Check(formals)) {
    PyErr_Format(PyExc_TypeError, "Function formals must be Formals, not %R",
                 formals);
    return NULL;
  }
  if (! parent_ns) {
    PyErr_SetString(PyExc_TypeError, "Function parent namespace is required");
    return NULL;
  }
  if (! KosNamespace_Check(parent_ns)) {
    PyErr_Format(PyExc_TypeError, "Function parent namespace must be"
                 " Namespace, not %R", parent_ns);
    return NULL;
  }

  PyObject *self = PyType_GenericNew(&KosFunctionType, NULL, NULL);
  if (! self)
    return NULL;

  KosFunction *fn = (KosFunction *) self;
  Py_INCREF(code);
  Py_INCREF(formals);
  Py_INCREF(parent_ns);
  fn->code = code;
  fn->base_ns = parent_ns;
  fn->formals_spec = NULL;
  fn->formals = formals;
  return self;
}


/* === Run code as coroutine === */


PyObject *kos_run_code_as_coroutine(PyObject *code, PyObject *ns) {

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
  KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code, ns, NULL);
  if (! frame)
    return NULL;

  KosContext *ctx = kos_context_new(frame);
  if (! ctx) {
    kos_dynframe_free(frame);
    return NULL;
  }

  return KosCoroutine_New(ctx);
}


/* === KosCoroutine type === */


static PyObject *coro_str_send = NULL;
static PyObject *coro_str_throw = NULL;
static PyObject *coro_str_close = NULL;


static void coroutine_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosCoroutine *coro = (KosCoroutine *) self;

  if (coro->ctx)
    kos_context_free(coro->ctx);

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
    return kos_context_traverse(coro->ctx, visit, arg);
  }

  return 0;
}


static int coroutine_clear(PyObject *self) {

  KosCoroutine *coro = (KosCoroutine *) self;

  Py_CLEAR(coro->pending_await);

  if (coro->ctx) {
    kos_context_free(coro->ctx);
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

  /* If waiting on Python awaitable, forward to it */
  if (coro->state == KOS_CORO_WAITING_PY && coro->ctx->py_await_iter) {
    if (! coro_str_send) {
      coro_str_send = PyUnicode_InternFromString("send");
      if (! coro_str_send)
        return NULL;
    }

    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  coro_str_send, value, NULL);

    if (result) {
      /* Still yielding */
      return result;
    }

    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      /* Python awaitable completed - inject result and drive again */
      PyObject *await_result = NULL;
      PyGen_FetchStopIterationValue(&await_result);
      if (! await_result)
        await_result = Py_None;

      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_RUNNING;
      Py_XDECREF(coro->ctx->py_await_inject);
      coro->ctx->py_await_inject = await_result;
      /* fall through to drive and handle ev */
    } else {
      /* Exception from Python awaitable */
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_DONE;
      return NULL;
    }
  }

  coro->in_send = 1;
  coro->state = KOS_CORO_RUNNING;

  for (;;) {
    KosEvent ev = {0};
    int res = koskari_eval(coro->ctx, &ev);

    coro->in_send = 0;

    if (res < 0) {
      coro->state = KOS_CORO_DONE;
      return NULL;
    }

    switch (ev.kind) {
      case KOS_EV_RETURN:
        coro->state = KOS_CORO_DONE;
        PyErr_SetObject(PyExc_StopIteration, ev.payload);
        Py_XDECREF(ev.payload);
        return NULL;

      case KOS_EV_RAISE:
        coro->state = KOS_CORO_DONE;
        if (ev.payload) {
          PyErr_SetObject((PyObject *) Py_TYPE(ev.payload), ev.payload);
          Py_DECREF(ev.payload);
        }
        return NULL;

      case KOS_EV_YIELD:
        /* For coroutines, yield is like await - return the value */
        return ev.payload;

      case KOS_EV_AWAIT: {
        SAYF("[eval] coroutine_send KOS_EV_AWAIT branch (payload=%d)\n",
             ev.payload ? 1 : 0);
        if (! ev.payload) {
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
        return ev.payload;  /* transfer ownership to caller (asyncio) */
      }
    }

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

  /* If waiting on Python awaitable, forward throw */
  if (coro->state == KOS_CORO_WAITING_PY && coro->ctx->py_await_iter) {
    if (! coro_str_throw) {
      coro_str_throw = PyUnicode_InternFromString("throw");
      if (! coro_str_throw)
        return NULL;
    }

    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  coro_str_throw, typ, val, tb,
                                                  NULL);

    if (result) {
      return result;
    }

    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      PyObject *await_result = NULL;
      PyGen_FetchStopIterationValue(&await_result);
      if (! await_result)
        await_result = Py_None;
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_RUNNING;
      Py_XDECREF(coro->ctx->py_await_inject);
      coro->ctx->py_await_inject = await_result;
      return coroutine_send(self, Py_None);
    } else {
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_DONE;
      return NULL;
    }
  }

  /* Normalize exception */
  PyErr_NormalizeException(&typ, &val, &tb);

  coro->ctx->pending_throw = 1;
  Py_XINCREF(val);
  Py_XDECREF(coro->ctx->pending_exc);
  coro->ctx->pending_exc = val;

  return coroutine_send(self, Py_None);
}


static PyObject *coroutine_close(PyObject *self) {

  KosCoroutine *coro = (KosCoroutine *) self;

  if (coro->state == KOS_CORO_DONE) {
    Py_RETURN_NONE;
  }

  /* Close any pending Python awaitable */
  if (coro->ctx->py_await_iter) {
    if (! coro_str_close) {
      coro_str_close = PyUnicode_InternFromString("close");
      if (! coro_str_close)
        return NULL;
    }

    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  coro_str_close, NULL);
    Py_XDECREF(result);
    PyErr_Clear();
    Py_CLEAR(coro->ctx->py_await_iter);
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


static PyMethodDef coroutine_methods[] = {
  { "send", (PyCFunction) coroutine_send, METH_O,
    "Send a value into the coroutine" },

  { "throw", (PyCFunction) coroutine_throw, METH_VARARGS,
    "Throw an exception into the coroutine" },

  { "close", (PyCFunction) coroutine_close, METH_NOARGS,
    "Close the coroutine" },

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
    kos_context_free(ctx);
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


int koskari_func_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosExpressionType))
    return -1;

  if (PyType_Ready(&KosFunctionType))
    return -1;

  if (PyType_Ready(&KosCoroutineType))
    return -1;

  if (PyDict_SetItemString(dict, "Expression",
                           (PyObject *) &KosExpressionType))
    return -1;

  if (PyDict_SetItemString(dict, "Function",
                           (PyObject *) &KosFunctionType))
    return -1;

  if (PyDict_SetItemString(dict, "Coroutine",
                           (PyObject *) &KosCoroutineType))
    return -1;

  return 0;
}


int koskari_func_clear(PyObject *mod) {

  Py_CLEAR(coro_str_send);
  Py_CLEAR(coro_str_throw);
  Py_CLEAR(coro_str_close);

  (void) mod;
  return 0;
}


// The end.
