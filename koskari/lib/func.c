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


PyObject *KosExpression_Eval(PyObject *self) {

  KosExpression *ex = (KosExpression *) self;
  return KosCoroutine_NewFromCode(ex->code, ex->ns);
}


PyObject *KosExpression_GetCode(PyObject *self) {

  KosExpression *ex = (KosExpression *) self;
  return ex->code;
}


PyObject *KosExpression_GetNs(PyObject *self) {

  KosExpression *ex = (KosExpression *) self;
  return ex->ns;
}


static PyObject *expression_call(PyObject *self, PyObject *args,
                                 PyObject *kwargs) {

  if (args && PyTuple_GET_SIZE(args) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression() takes no arguments");
    return NULL;
  }
  if (kwargs && PyDict_Size(kwargs) != 0) {
    PyErr_SetString(PyExc_TypeError, "Expression takes no named arguments");
    return NULL;
  }
  return KosExpression_Eval(self);
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


/* === KosFunctionSpec: (name, formals_spec, code) for BUILD_FUNCTION === */


static PyMemberDef function_spec_members[] = {
  { "name", T_OBJECT,
    offsetof(KosFunctionSpec, name), READONLY,
    "name for function" },

  { "formals_spec", T_OBJECT,
    offsetof(KosFunctionSpec, formals_spec), READONLY,
    "formals spec for function" },

  { "code", T_OBJECT,
    offsetof(KosFunctionSpec, code), READONLY,
    "KosCode for function" },

  { NULL, },
};


static PyObject *function_spec_call(PyObject *self,
                                    PyObject *args, PyObject *kwds) {

  if (kwds) {
    PyErr_SetString(PyExc_TypeError, "FunctionSpec does not accept keyword"
                    " arguments");
    return NULL;
  }

  PyObject *defaults = NULL;
  PyObject *parent = NULL;

  if (! PyArg_ParseTuple(args, "OO:FunctionSpec", &defaults, &parent))
    return NULL;

  return KosFunction_New(self, defaults, parent);
}


static PyObject *function_spec_new(PyTypeObject *type,
                                   PyObject *args, PyObject *kwds) {

  if (kwds) {
    PyErr_SetString(PyExc_TypeError, "FunctionSpec does not accept keyword"
                    " arguments");
    return NULL;
  }

  PyObject *name = NULL;
  PyObject *fspec = NULL;
  PyObject *code = NULL;

  if (! PyArg_ParseTuple(args, "OOO:FunctionSpec", &name, &fspec, &code))
    return NULL;

  return KosFunctionSpec_New(name, fspec, code);
}


static void function_spec_dealloc(PyObject *self) {
  Py_CLEAR(KosFunctionSpec_NAME(self));
  Py_CLEAR(KosFunctionSpec_FORMALS_SPEC(self));
  Py_CLEAR(KosFunctionSpec_CODE(self));
  Py_TYPE(self)->tp_free(self);
}


static int function_spec_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosFunctionSpec_NAME(self));
  Py_VISIT(KosFunctionSpec_FORMALS_SPEC(self));
  Py_VISIT(KosFunctionSpec_CODE(self));
  return 0;
}


static int function_spec_clear(PyObject *self) {
  Py_CLEAR(KosFunctionSpec_NAME(self));
  Py_CLEAR(KosFunctionSpec_FORMALS_SPEC(self));
  Py_CLEAR(KosFunctionSpec_CODE(self));
  return 0;
}


PyTypeObject KosFunctionSpecType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.FunctionSpec",
  .tp_basicsize = sizeof(KosFunctionSpec),
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_members = function_spec_members,
  .tp_new = function_spec_new,
  .tp_call = function_spec_call,
  .tp_dealloc = function_spec_dealloc,
  .tp_traverse = function_spec_traverse,
  .tp_clear = function_spec_clear,
};


PyObject *KosFunctionSpec_New(PyObject *name, PyObject *formals_spec,
                              PyObject *code) {

  if (! KosCode_Check(code)) {
    PyErr_Format(PyExc_TypeError, "function spec code must be Code,"
                 " not %R", code);
    return NULL;
  }

  if (name == Py_None)
    name = NULL;

  if (! KosFormalsSpec_CheckExact(formals_spec)) {
    PyErr_Format(PyExc_TypeError, "function spec formals must be FormalsSpec,"
                 " not %R", formals_spec);
    return NULL;
  }

  PyObject *self = PyType_GenericNew(&KosFunctionSpecType, NULL, NULL);
  if (! self)
    return NULL;

  KosFunctionSpec *spec = (KosFunctionSpec *) self;
  Py_XINCREF(name);
  spec->name = name;

  Py_INCREF(formals_spec);
  spec->formals_spec = formals_spec;

  Py_INCREF(code);
  spec->code = code;

  return self;
}


/* === KosFunction: callable (code, formals, base_ns) -> coroutine on __call__ === */


typedef struct {
  PyObject_HEAD
  PyObject *code;
  PyObject *base_ns;
  PyObject *formals;
} KosFunction;


static int function_init(KosFunction *self, PyObject *args,
                         PyObject *kwargs) {
  PyObject *code = NULL;
  PyObject *base_ns = NULL;
  PyObject *formals_spec = NULL;
  PyObject *formals = NULL;

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
  formals = KosFormals_New(formals_spec, NULL);
  if (! formals)
    return -1;
  Py_INCREF(code);
  Py_INCREF(base_ns);
  self->code = code;
  self->base_ns = base_ns;
  self->formals = formals;
  return 0;
}


static void function_dealloc(PyObject *self) {
  KosFunction *fn = (KosFunction *) self;
  Py_CLEAR(fn->code);
  Py_CLEAR(fn->base_ns);
  Py_CLEAR(fn->formals);
  Py_TYPE(self)->tp_free(self);
}


/**
   Obtain the call namespace for a function: formals' namespace spec
   applied to base_ns.  Returns new reference or NULL on error.
*/
static PyObject *func_obtain_call_namespace(KosFunction *fn) {
  PyObject *nspec = KosFormals_NamespaceSpec(fn->formals, NULL);
  if (! nspec)
    return NULL;
  PyObject *frame_ns = KosNamespace_FromSpec(nspec, fn->base_ns);
  Py_DECREF(nspec);
  return frame_ns;
}


PyObject *KosFunction_Call(PyObject *self,
                           PyObject *args, PyObject *kwds) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = NULL;
  PyObject *coro = NULL;

  frame_ns = func_obtain_call_namespace(fn);
  if (! frame_ns)
    goto fail;

  if (KosFormals_Apply(fn->formals, frame_ns, args, kwds) < 0)
    goto fail;

  coro = KosCoroutine_NewFromCode(fn->code, frame_ns);
  Py_DECREF(frame_ns);
  return coro;

 fail:
  Py_XDECREF(frame_ns);
  return NULL;
}


PyObject *KosFunction_PrepareCallNamespace(PyObject *self,
                                            PyObject *args,
                                            PyObject *kwds) {

  KosFunction *fn = (KosFunction *) self;
  PyObject *frame_ns = func_obtain_call_namespace(fn);
  if (! frame_ns)
    return NULL;
  if (KosFormals_Apply(fn->formals, frame_ns, args, kwds) < 0) {
    Py_DECREF(frame_ns);
    return NULL;
  }
  return frame_ns;
}


PyObject *KosFunction_GetCode(PyObject *self) {

  KosFunction *fn = (KosFunction *) self;
  return fn->code;
}


PyObject *KosFunction_CallNoArgs(PyObject *self) {

  return KosFunction_Call(self, NULL, NULL);
}


static PyObject *function_call(PyObject *self,
                               PyObject *args, PyObject *kwargs) {

  return KosFunction_Call(self, args, kwargs);
}


static PyMemberDef function_members[] = {
  { "code", T_OBJECT, offsetof(KosFunction, code), READONLY, NULL },
  { "base_ns", T_OBJECT, offsetof(KosFunction, base_ns), READONLY, NULL },
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


PyObject *KosFunction_NewFromFormals(
    PyObject *code,
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
  fn->formals = formals;
  return self;
}


PyObject *KosFunction_New(PyObject *func_spec,
                         PyObject *kw_defaults,
                         PyObject *parent_namespace) {

  if (! KosFunctionSpec_CheckExact(func_spec)) {
    PyErr_Format(PyExc_TypeError, "KosFunction_New requires FunctionSpec,"
                 " not %R", func_spec);
    return NULL;
  }

  PyObject *formals_spec = KosFunctionSpec_FORMALS_SPEC(func_spec);
  PyObject *code = KosFunctionSpec_CODE(func_spec);

  PyObject *formals = KosFormals_New(formals_spec, kw_defaults);
  if (! formals)
    return NULL;

  PyObject *fn = KosFunction_NewFromFormals(code, formals, parent_namespace);
  Py_DECREF(formals);
  return fn;
}


/* === Run code as coroutine === */


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
  KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code, ns, NULL);
  if (! frame)
    return NULL;

  KosContext *ctx = kos_context_new(frame);
  if (! ctx) {
    kos_dynframe_free(frame);
    return NULL;
  }

  if (KosCode_YIELDS((KosCode *) code))
    return KosPyAsyncGen_New(ctx);
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

  /* If suspended on yield, set inject and run */
  if (coro->state == KOS_CORO_YIELDED) {
    PyObject *inject = (value != NULL) ? value : Py_None;
    Py_INCREF(inject);
    Py_XDECREF(KosContext_PY_RESUME_INJECT(coro->ctx));
    KosContext_PY_RESUME_INJECT(coro->ctx) = inject;
    KosContext_PY_RESUME_KIND(coro->ctx) = KOS_RESUME_YIELD;
    coro->state = KOS_CORO_RUNNING;
  }

  /* If waiting on Python awaitable, forward to it */
  if (coro->state == KOS_CORO_WAITING_PY && coro->ctx->py_await_iter) {
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
      Py_XDECREF(KosContext_PY_RESUME_INJECT(coro->ctx));
      KosContext_PY_RESUME_INJECT(coro->ctx) = await_result;
      KosContext_PY_RESUME_KIND(coro->ctx) = KOS_RESUME_AWAIT;
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

  KosEvent ev = {.kind = KOS_EV_RETURN, .payload = NULL};
  int res = koskari_eval(coro->ctx, &ev);

  coro->in_send = 0;

  SAYF("[coroutine_send] koskari_eval -> res=%d ev.kind=%d, payload=%R\n",
        res, ev.kind, ev.payload);

  if (res < 0) {
    coro->state = KOS_CORO_DONE;
    return NULL;
  }

  switch (ev.kind) {
    case KOS_EV_RETURN:
      coro->state = KOS_CORO_DONE;

      // we do this manually to bypass the problem with tuple payloads.
      // There is an annoying extra behavior in Python where if you set an
      // exception with a tuple payload, it unpacks it as the args for the
      // exception. So we'll create the exception instance ourselves to
      // avoid that.
      PyObject *err = PyObject_CallOneArg(PyExc_StopIteration, ev.payload);
      PyErr_SetObject(PyExc_StopIteration, err);
      Py_DECREF(err);

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
      coro->state = KOS_CORO_YIELDED;
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
  if (coro->state == KOS_CORO_WAITING_PY && coro->ctx->py_await_iter) {
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
      Py_XDECREF(KosContext_PY_RESUME_INJECT(coro->ctx));
      KosContext_PY_RESUME_INJECT(coro->ctx) = await_result;
      KosContext_PY_RESUME_KIND(coro->ctx) = KOS_RESUME_AWAIT;
      return coroutine_send(self, Py_None);
    } else {
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_DONE;
      return NULL;
    }
  }

  /* Normalize exception */
  PyErr_NormalizeException(&typ, &val, &tb);

  KosContext_INJECT_MODE(coro->ctx) = KOS_INJECT_THROW;
  Py_XINCREF(val);
  Py_XDECREF(KosContext_INJECT_VALUE(coro->ctx));
  KosContext_INJECT_VALUE(coro->ctx) = val;

  return coroutine_send(self, Py_None);
}


static PyObject *coroutine_close(PyObject *self) {

  KosCoroutine *coro = (KosCoroutine *) self;

  if (coro->state == KOS_CORO_DONE) {
    Py_RETURN_NONE;
  }

  /* Close any pending Python awaitable */
  if (coro->ctx->py_await_iter) {
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


/* === KosPyDelimitedContinuation type === */


/**
   KosPyDelimitedContinuation wraps a KosContinuation captured by shift.

   DelimitedContinuation cannot be called from Python; application is only
   permitted via CALL inside koskari_eval.
*/

static void delimited_continuation_dealloc(PyObject *self) {

  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;

  kos_continuation_free_exclude(cont->captured, self);

  Py_TYPE(self)->tp_free(self);
}


static int delimited_continuation_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;

  if (cont->captured) {
    return kos_continuation_traverse(cont->captured, visit, arg);
  }

  return 0;
}


static int delimited_continuation_clear(PyObject *self) {

  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;

  kos_continuation_free(cont->captured);
  cont->captured = NULL;

  return 0;
}


static PyObject *delimited_continuation_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  (void) args;
  (void) kwds;
  (void) self;

  PyErr_SetString(PyExc_TypeError,
                  "DelimitedContinuation cannot be called from Python");
  return NULL;
}


static PyObject *delimited_continuation_repr(PyObject *self) {

  return PyUnicode_FromFormat("<DelimitedContinuation at %p>", self);
}


PyTypeObject KosPyDelimitedContinuationType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.DelimitedContinuation",
  .tp_basicsize = sizeof(KosPyDelimitedContinuation),
  .tp_itemsize = 0,
  .tp_dealloc = delimited_continuation_dealloc,
  .tp_repr = delimited_continuation_repr,
  .tp_call = delimited_continuation_call,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Captured delimited continuation (multi-shot, from shift)",
  .tp_traverse = delimited_continuation_traverse,
  .tp_clear = delimited_continuation_clear,
};


PyObject *KosPyDelimitedContinuation_New(KosContinuation *captured) {

  KosPyDelimitedContinuation *cont = PyObject_GC_New(KosPyDelimitedContinuation,
                                            &KosPyDelimitedContinuationType);
  if (! cont)
    return NULL;

  cont->captured = captured;

  PyObject_GC_Track((PyObject *) cont);
  return (PyObject *) cont;
}


/* === KosPyContinuation type (undelimited, for future call/cc) === */


/**
   KosPyContinuation wraps an undelimited KosContinuation (from call/cc).

   Python-callable: raises KoskariContinuationDead or KoskariContinuationApply.
   Not yet instantiated (call/cc capture not implemented).
*/

static void continuation_dealloc(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  kos_continuation_free_exclude(cont->captured, self);

  Py_TYPE(self)->tp_free(self);
}


static int continuation_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  if (cont->captured) {
    return kos_continuation_traverse(cont->captured, visit, arg);
  }

  return 0;
}


static int continuation_clear(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  kos_continuation_free(cont->captured);
  cont->captured = NULL;

  return 0;
}


static PyObject *continuation_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  KosPyContinuation *cont = (KosPyContinuation *) self;
  (void) kwds;

  if (! cont->captured) {
    PyErr_SetString(PyExc_RuntimeError,
                    "continuation has no captured state");
    return NULL;
  }

  if (! KoskariContinuationApply) {
    PyErr_SetString(PyExc_RuntimeError,
                    "KoskariContinuationApply not initialized");
    return NULL;
  }

  PyObject *resume_val = Py_None;
  if (args && PyTuple_GET_SIZE(args) > 0)
    resume_val = PyTuple_GET_ITEM(args, 0);
  Py_INCREF(resume_val);

  PyObject *exc = PyObject_CallNoArgs(KoskariContinuationApply);
  if (! exc) {
    Py_DECREF(resume_val);
    return NULL;
  }
  PyObject_SetAttrString(exc, "target", cont->captured->token_ref);
  PyObject_SetAttrString(exc, "continuation", self);
  PyObject *args_tup = PyTuple_Pack(1, resume_val);
  Py_DECREF(resume_val);
  if (! args_tup) {
    Py_DECREF(exc);
    return NULL;
  }
  PyObject_SetAttrString(exc, "args", args_tup);
  Py_DECREF(args_tup);
  PyErr_SetObject(KoskariContinuationApply, exc);
  Py_DECREF(exc);
  return NULL;
}


static PyObject *continuation_repr(PyObject *self) {

  return PyUnicode_FromFormat("<Continuation at %p>", self);
}


PyTypeObject KosPyContinuationType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.Continuation",
  .tp_basicsize = sizeof(KosPyContinuation),
  .tp_itemsize = 0,
  .tp_dealloc = continuation_dealloc,
  .tp_repr = continuation_repr,
  .tp_call = continuation_call,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Captured undelimited continuation (multi-shot, from call/cc)",
  .tp_traverse = continuation_traverse,
  .tp_clear = continuation_clear,
};


PyObject *KosPyContinuation_New(KosContinuation *captured) {

  KosPyContinuation *cont = PyObject_GC_New(KosPyContinuation,
                                            &KosPyContinuationType);
  if (! cont)
    return NULL;

  cont->captured = captured;

  PyObject_GC_Track((PyObject *) cont);
  return (PyObject *) cont;
}


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
  int res = koskari_eval(agen->ctx, &ev);

  agen->in_progress = 0;

  if (res < 0) {
    agen->closed = 1;
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  switch (ev.kind) {
    case KOS_EV_RETURN:
      /* Generator completed; pass return value as StopAsyncIteration value */
      agen->closed = 1;
      if (ev.payload) {
        PyErr_SetObject(PyExc_StopAsyncIteration, ev.payload);
        Py_DECREF(ev.payload);
      } else {
        PyErr_SetNone(PyExc_StopAsyncIteration);
      }
      return NULL;

    case KOS_EV_RAISE:
      agen->closed = 1;
      if (ev.payload) {
        PyErr_SetObject((PyObject *) Py_TYPE(ev.payload), ev.payload);
        Py_DECREF(ev.payload);
      } else {
        PyErr_SetNone(PyExc_StopAsyncIteration);
      }
      return NULL;

    case KOS_EV_YIELD: {
      /* Yielded a value - wrap in StopIteration to signal completion
         of this __anext__ awaitable.  Wrap in 1-tuple so PyErr_SetObject
         does not unpack a tuple payload. */
      PyObject *args = PyTuple_Pack(1, ev.payload);
      Py_XDECREF(ev.payload);
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
      return ev.payload;
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


int koskari_func_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  STR_CONST(coro_str_send, "send");
  STR_CONST(coro_str_throw, "throw");
  STR_CONST(coro_str_close, "close");

  if (PyType_Ready(&KosExpressionType) ||
      PyType_Ready(&KosFunctionSpecType) ||
      PyType_Ready(&KosFunctionType) ||
      PyType_Ready(&KosCoroutineType) ||
      PyType_Ready(&KosPyDelimitedContinuationType) ||
      PyType_Ready(&KosPyContinuationType) ||
      PyType_Ready(&KosPyAsyncGenType) ||
      PyType_Ready(&KosPyAsyncGenASendType))
    return -1;

  if (PyDict_SetItemString(dict, "Expression",
                           (PyObject *) &KosExpressionType))
    return -1;

  if (PyDict_SetItemString(dict, "FunctionSpec",
                           (PyObject *) &KosFunctionSpecType))
    return -1;

  if (PyDict_SetItemString(dict, "Function",
                           (PyObject *) &KosFunctionType))
    return -1;

  if (PyDict_SetItemString(dict, "Coroutine",
                           (PyObject *) &KosCoroutineType))
    return -1;

  if (PyDict_SetItemString(dict, "DelimitedContinuation",
                           (PyObject *) &KosPyDelimitedContinuationType))
    return -1;

  if (PyDict_SetItemString(dict, "Continuation",
                           (PyObject *) &KosPyContinuationType))
    return -1;

  if (PyDict_SetItemString(dict, "AsyncGenerator",
                           (PyObject *) &KosPyAsyncGenType))
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
