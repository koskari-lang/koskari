/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   koskari/lib/sync.c

   Async-to-sync primitives. Imports koskari._sync and exposes
   run_sync and iter_sync for C callers and the module dict.
   demand_sync: invoke Koskari callable without suspension (callee as root).
   Yielding KosFunctions return DemandSyncGenerator instead of raising.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "debug.h"


/* === KoskariSyncViolation === */


static PyObject *KoskariSyncViolation = NULL;


static int register_koskari_sync_exception(PyObject *mod,
    const char *full_name,
    const char *attr_name,
    PyObject *base,
    PyObject **out_exc) {

  PyObject *bases = PyTuple_Pack(1, base);
  if (! bases)
    return -1;
  *out_exc = PyErr_NewException(full_name, bases, NULL);
  Py_DECREF(bases);
  if (! *out_exc)
    return -1;
  return PyModule_AddObject(mod, attr_name, *out_exc);
}


/* === KosDemandSyncGenerator === */


typedef struct {
  PyObject_HEAD
  KosContext *ctx;
  int closed;    /* 0=running, 1=done or broken (ctx freed) */
  int in_send;   /* re-entrancy guard */
} KosDemandSyncGenerator;


static PyTypeObject KosDemandSyncGeneratorType;


static void demand_gen_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosDemandSyncGenerator *gen = (KosDemandSyncGenerator *) self;

  if (gen->ctx) {
    kos_context_free(gen->ctx);
    gen->ctx = NULL;
  }

  Py_TYPE(self)->tp_free(self);
}


static int demand_gen_traverse(PyObject *self, visitproc visit, void *arg) {

  KosDemandSyncGenerator *gen = (KosDemandSyncGenerator *) self;

  if (gen->ctx)
    return kos_context_traverse(gen->ctx, visit, arg);

  return 0;
}


static int demand_gen_clear(PyObject *self) {

  KosDemandSyncGenerator *gen = (KosDemandSyncGenerator *) self;

  if (gen->ctx) {
    kos_context_free(gen->ctx);
    gen->ctx = NULL;
  }

  return 0;
}


static PyObject *demand_gen_iter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static void demand_gen_close_ctx(KosDemandSyncGenerator *gen) {

  if (gen->ctx) {
    kos_context_free(gen->ctx);
    gen->ctx = NULL;
  }
  gen->closed = 1;
}


static PyObject *demand_gen_step(
    KosDemandSyncGenerator *gen,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  if (gen->closed) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  if (gen->in_send) {
    PyErr_SetString(PyExc_RuntimeError,
                    "demand generator already being iterated");
    return NULL;
  }

  gen->in_send = 1;

  KosEvent ev = { 0 };
  int res = koskari_eval(gen->ctx, &ev, send_val, throw_typ, throw_exc);

  gen->in_send = 0;

  if (res < 0) {
    demand_gen_close_ctx(gen);
    return NULL;
  }

  switch (KosEvent_KIND(&ev)) {
    case KOS_EV_YIELD: {
      PyObject *payload = KosEvent_PAYLOAD(&ev);
      KosEvent_PAYLOAD(&ev) = NULL;
      return payload;
    }

    case KOS_EV_RETURN: {
      demand_gen_close_ctx(gen);
      PyObject *payload = KosEvent_PAYLOAD(&ev);
      KosEvent_PAYLOAD(&ev) = NULL;
      PyObject *err =
          PyObject_CallOneArg(PyExc_StopIteration, payload ? payload : Py_None);
      Py_XDECREF(payload);
      if (err) {
        PyErr_SetObject(PyExc_StopIteration, err);
        Py_DECREF(err);
      }
      else {
        PyErr_SetNone(PyExc_StopIteration);
      }
      return NULL;
    }

    case KOS_EV_RAISE: {
      demand_gen_close_ctx(gen);
      if (KosEvent_EXC_TYPE(&ev)) {
        if (KosEvent_PAYLOAD(&ev)) {
          PyErr_SetObject(KosEvent_EXC_TYPE(&ev), KosEvent_PAYLOAD(&ev));
          Py_DECREF(KosEvent_PAYLOAD(&ev));
        }
        else {
          PyErr_SetNone(KosEvent_EXC_TYPE(&ev));
        }
        Py_DECREF(KosEvent_EXC_TYPE(&ev));
      }
      else if (KosEvent_PAYLOAD(&ev)) {
        PyErr_SetObject(
            (PyObject *) Py_TYPE(KosEvent_PAYLOAD(&ev)),
            KosEvent_PAYLOAD(&ev));
        Py_DECREF(KosEvent_PAYLOAD(&ev));
      }
      else {
        PyErr_SetNone(PyExc_StopIteration);
      }
      return NULL;
    }

    case KOS_EV_AWAIT: {
      demand_gen_close_ctx(gen);
      if (KosEvent_PAYLOAD(&ev))
        Py_DECREF(KosEvent_PAYLOAD(&ev));
      PyErr_Format(KoskariSyncViolation,
                   "async suspension is not allowed in demand generator");
      return NULL;
    }

    default:
      demand_gen_close_ctx(gen);
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      return NULL;
  }
}


static PyObject *demand_gen_iternext(PyObject *self) {

  return demand_gen_step((KosDemandSyncGenerator *) self, NULL, NULL, NULL);
}


static PyObject *demand_gen_send(PyObject *self, PyObject *value) {

  if (! value)
    value = Py_None;
  Py_INCREF(value);
  PyObject *result =
      demand_gen_step((KosDemandSyncGenerator *) self, value, NULL, NULL);
  Py_DECREF(value);
  return result;
}


static PyObject *demand_gen_throw(PyObject *self, PyObject *args) {

  KosDemandSyncGenerator *gen = (KosDemandSyncGenerator *) self;

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  if (! PyArg_ParseTuple(args, "O|OO:throw", &typ, &val, &tb))
    return NULL;

  if (gen->closed) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  PyErr_NormalizeException(&typ, &val, &tb);
  PyObject *exc = val ? val : typ;
  Py_INCREF(exc);

  PyObject *result =
      demand_gen_step(gen, NULL, typ, exc);
  Py_DECREF(exc);
  return result;
}


static PyMethodDef demand_gen_methods[] = {
  { "send", (PyCFunction) demand_gen_send, METH_O,
    "Send a value into the generator" },
  { "throw", (PyCFunction) demand_gen_throw, METH_VARARGS,
    "Raise an exception in the generator" },
  { NULL, NULL, 0, NULL },
};


static PyObject *demand_gen_new(PyTypeObject *type, PyObject *args, PyObject *kw) {

  PyErr_SetString(PyExc_TypeError,
                  "DemandSyncGenerator cannot be created directly");
  return NULL;
}


static PyTypeObject KosDemandSyncGeneratorType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.DemandSyncGenerator",
  .tp_basicsize = sizeof(KosDemandSyncGenerator),
  .tp_itemsize = 0,
  .tp_dealloc = demand_gen_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Sync generator for yielding Koskari functions in demand_sync context",
  .tp_traverse = demand_gen_traverse,
  .tp_clear = demand_gen_clear,
  .tp_iter = demand_gen_iter,
  .tp_iternext = demand_gen_iternext,
  .tp_methods = demand_gen_methods,
  .tp_new = demand_gen_new,
};


static PyObject *KosDemandSyncGenerator_New(KosContext *ctx) {

  KosDemandSyncGenerator *gen =
      (KosDemandSyncGenerator *)
      PyObject_GC_New(KosDemandSyncGenerator, &KosDemandSyncGeneratorType);
  if (! gen) {
    kos_context_free(ctx);
    return NULL;
  }

  gen->ctx = ctx;
  gen->closed = 0;
  gen->in_send = 0;

  PyObject_GC_Track((PyObject *) gen);
  return (PyObject *) gen;
}


/* === demand_sync === */


/**
   Base implementation: returns 0 (success), 1 (suspension, no exception),
   or -1 (error, exception set). Never sets exception for suspension.
 */
static int demand_sync_ex(PyObject *callable, PyObject *args,
                          PyObject *kwargs, PyObject **out_result) {

  PyObject *frame_ns = NULL;
  KosDynFrame *frame = NULL;
  KosContext *ctx = NULL;
  KosEvent ev = { 0 };
  int r = 0;
  int suspended = 0;
  PyObject *result = NULL;

  PyObject *call_args = NULL;

  // if (! args) {
  //   args = PyTuple_New(0);
  //   if (! args)
  //     goto ret_fail;
  // }

  SAYF("[demand_sync] callable = %R", callable);

  /* Plain Python (not KosCallable): direct call, reject awaitable */
  if (! KosCallable_Check(callable)) {
    SAYF("[demand_sync] Plain Python (not KosCallable): callable = %R", callable);
    SAYF("[demand_sync] args = %R", args);
    SAYF("[demand_sync] kwargs = %R", kwargs);

    result = PyObject_Call(callable, args, kwargs);
    if (! result)
      goto ret_fail;

    *out_result = result;
    return 0;
  }
  /* KosBoundMethod: unwrap and prepend self to args */
  if (KosBoundMethod_CheckExact(callable)) {
    PyObject *fn = KosBoundMethod_FUNC(callable);
    PyObject *self = KosBoundMethod_SELF(callable);
    PyObject *tmp = PyTuple_Pack(1, self);
    if (! tmp)
      goto ret_fail;

    call_args = PySequence_Concat(tmp, args);
    Py_DECREF(tmp);
    if (! call_args)
      goto ret_fail;
    callable = fn;

  } else {
    call_args = args;
    Py_INCREF(call_args);
  }

  /* KosExpression */
  if (KosExpression_CheckExact(callable)) {
    if (PyTuple_GET_SIZE(call_args) != 0
        || (kwargs && PyDict_Size(kwargs) != 0)) {
      PyErr_SetString(PyExc_TypeError,
                      "Expression takes no arguments");
      goto ret_fail;
    }
    PyObject *code = KosExpression_CODE(callable);
    PyObject *ns = KosExpression_NS(callable);
    Py_INCREF(ns);
    if (KosCode_YIELDS((KosCode *) code)) {
      KosDynGen *gen_node = kos_dyngen_new(NULL);
      if (! gen_node) {
        Py_DECREF(ns);
        goto ret_fail;
      }
      frame = kos_dynframe_new(
          (KosDynNode *) gen_node, (KosCode *) code, ns, NULL);
      if (! frame) {
        kos_dyngen_free(gen_node);
        Py_DECREF(ns);
        goto ret_fail;
      }
      KosDynGen_OWNER(gen_node) = NULL;  /* demand_sync: no owner; yield = violation */
    }
    else {
      frame = kos_dynframe_new(NULL, (KosCode *) code, ns, NULL);
      if (! frame) {
        Py_DECREF(ns);
        goto ret_fail;
      }
    }
    goto run_eval;
  }

  /* KosFunction */
  if (KosFunction_CheckExact(callable)) {
    frame_ns = KosFunction_PrepareCallNamespace(callable, call_args, kwargs);
    if (! frame_ns)
      goto ret_fail;
    SAYF("[demand_sync] frame_ns from PrepareCallNamespace: ptr=%p type=%s "
         "repr=%R is_namespace=%d",
         (void *) frame_ns,
         frame_ns ? Py_TYPE(frame_ns)->tp_name : "NULL",
         frame_ns,
         frame_ns ? KosNamespace_Check(frame_ns) : 0);

    PyObject *code = KosFunction_CODE(callable);

    if (KosCode_YIELDS((KosCode *) code)) {
      KosDynGen *gen_node = kos_dyngen_new(NULL);
      if (! gen_node)
        goto ret_fail;
      frame = kos_dynframe_new(
          (KosDynNode *) gen_node, (KosCode *) code, frame_ns, NULL);
      if (! frame) {
        kos_dyngen_free(gen_node);
        goto ret_fail;
      }
      ctx = kos_context_new(frame);
      frame = NULL;
      if (! ctx) {
        if (frame)
          kos_dynframe_free(frame);
        goto ret_fail;
      }
      PyObject *gen = KosDemandSyncGenerator_New(ctx);
      if (! gen) {
        kos_context_free(ctx);
        goto ret_fail;
      }
      KosDynGen_OWNER(gen_node) = gen;
      // Py_DECREF(args);
      *out_result = gen;
      return 0;
    }

    frame = kos_dynframe_new(NULL, (KosCode *) code, frame_ns, NULL);
    if (! frame)
      goto ret_fail;
    SAYF("[demand_sync] frame=%p KosDynFrame_NS(frame): ptr=%p type=%s "
         "repr=%R is_namespace=%d",
         (void *) frame,
         (void *) KosDynFrame_NS(frame),
         KosDynFrame_NS(frame) ? Py_TYPE(KosDynFrame_NS(frame))->tp_name : "NULL",
         KosDynFrame_NS(frame),
         KosDynFrame_NS(frame) ? KosNamespace_Check(KosDynFrame_NS(frame)) : 0);

    goto run_eval;
  }

  /* Other KosCallable (e.g. KosType) - not supported for demand_sync yet */
  PyErr_Format(PyExc_TypeError,
               "demand_sync does not support %s",
               Py_TYPE(callable)->tp_name);
  goto ret_fail;

run_eval:
  ctx = kos_context_new(frame);
  frame = NULL;
  if (! ctx) {
    if (frame)
      kos_dynframe_free(frame);
    goto ret_fail;
  }

  r = koskari_eval(ctx, &ev, NULL, NULL, NULL);
  kos_context_free(ctx);
  ctx = NULL;

  if (r < 0)
    goto ret_fail;

  switch (KosEvent_KIND(&ev)) {
    case KOS_EV_RETURN:
      result = KosEvent_PAYLOAD(&ev);
      KosEvent_PAYLOAD(&ev) = NULL;
      break;

    case KOS_EV_RAISE:
      if (KosEvent_EXC_TYPE(&ev)) {
        if (KosEvent_PAYLOAD(&ev)) {
          PyErr_SetObject(KosEvent_EXC_TYPE(&ev), KosEvent_PAYLOAD(&ev));
          Py_DECREF(KosEvent_PAYLOAD(&ev));
        }
        else {
          PyErr_SetNone(KosEvent_EXC_TYPE(&ev));
        }
        Py_DECREF(KosEvent_EXC_TYPE(&ev));
      }
      else if (KosEvent_PAYLOAD(&ev)) {
        PyErr_SetObject((PyObject *) Py_TYPE(KosEvent_PAYLOAD(&ev)),
                        KosEvent_PAYLOAD(&ev));
        Py_DECREF(KosEvent_PAYLOAD(&ev));
      }
      break;

    case KOS_EV_AWAIT:
    case KOS_EV_YIELD:
      if (KosEvent_PAYLOAD(&ev))
        Py_DECREF(KosEvent_PAYLOAD(&ev));
      suspended = 1;
      break;

    default:
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      break;
  }
  goto done;

ret_fail:
  Py_DECREF(call_args);
  *out_result = NULL;
  return -1;

done:
  Py_DECREF(call_args);
  *out_result = result;
  return result ? 0 : suspended ? 1 : -1;
}


int Koskari_DemandSyncEx(PyObject *callable, PyObject *args,
                         PyObject *kwargs, PyObject **out_result) {

  return demand_sync_ex(callable, args, kwargs, out_result);
}


static PyObject *demand_sync_impl(PyObject *callable, PyObject *args,
                                  PyObject *kwargs) {

  PyObject *result = NULL;
  int r = demand_sync_ex(callable, args, kwargs, &result);

  if (r == 1) {
    PyErr_Format(KoskariSyncViolation,
                 "async suspension is not allowed in demand_sync");
    // fall-through to return result
  }
  return result;
}


static PyObject *demand_sync_kw(PyObject *self, PyObject *args,
                                PyObject *kwargs) {

  (void) self;
  if (! PyTuple_GET_SIZE(args)) {
    PyErr_SetString(PyExc_TypeError,
                    "demand_sync() requires at least 1 argument (callable)");
    return NULL;
  }
  PyObject *callable = PyTuple_GET_ITEM(args, 0);
  Py_INCREF(callable);
  PyObject *call_args = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
  if (! call_args) {
    Py_DECREF(callable);
    return NULL;
  }
  PyObject *result = demand_sync_impl(callable, call_args, kwargs);
  Py_DECREF(call_args);
  Py_DECREF(callable);
  return result;
}


PyObject *Koskari_DemandSync(PyObject *callable, PyObject *args,
                             PyObject *kwargs) {

  return demand_sync_impl(callable, args, kwargs);
}


static PyMethodDef demand_sync_methoddef = {
  "demand_sync",
  (PyCFunction) demand_sync_kw,
  METH_VARARGS | METH_KEYWORDS,
  "Invoke Koskari callable synchronously without suspension"
};


static PyObject *as_coroutine_func = NULL;
static PyObject *run_sync_func = NULL;
static PyObject *iter_sync_func = NULL;


PyObject *Koskari_RunSync(PyObject *awaitable) {

  if (! run_sync_func) {
    PyErr_SetString(PyExc_RuntimeError,
                    "run_sync not available (koskari.lib init failed?)");
    return NULL;
  }
  return PyObject_CallOneArg(run_sync_func, awaitable);
}


PyObject *Koskari_IterSync(PyObject *async_iterable) {

  if (! iter_sync_func) {
    PyErr_SetString(PyExc_RuntimeError,
                    "iter_sync not available (koskari.lib init failed?)");
    return NULL;
  }
  return PyObject_CallOneArg(iter_sync_func, async_iterable);
}


int koskari_sync_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  PyObject *sync_mod = NULL;
  PyObject *demand_sync_func = NULL;

  if (! dict)
    return -1;

  if (register_koskari_sync_exception(mod,
          "koskari.lib.KoskariSyncViolation",
          "KoskariSyncViolation",
          (PyObject *) PyExc_TypeError,
          &KoskariSyncViolation) < 0)
    return -1;

  if (PyType_Ready(&KosDemandSyncGeneratorType) < 0)
    return -1;

  demand_sync_func = PyCFunction_New(&demand_sync_methoddef, mod);
  if (! demand_sync_func)
    return -1;
  if (PyDict_SetItemString(dict, "demand_sync", demand_sync_func) < 0) {
    Py_DECREF(demand_sync_func);
    return -1;
  }
  Py_DECREF(demand_sync_func);

  /* Native test harness uses _koskari_test; no koskari package available */
  {
    PyObject *mod_name = PyModule_GetNameObject(mod);
    int is_harness = (mod_name &&
        PyUnicode_CompareWithASCIIString(mod_name, "_koskari_test") == 0);
    Py_XDECREF(mod_name);
    if (is_harness)
      return 0;
  }

  sync_mod = PyImport_ImportModule("koskari._sync");
  if (! sync_mod)
    return -1;

  run_sync_func = PyObject_GetAttrString(sync_mod, "run_sync");
  iter_sync_func = PyObject_GetAttrString(sync_mod, "iter_sync");
  Py_DECREF(sync_mod);

  if (! run_sync_func || ! iter_sync_func) {
    Py_XDECREF(run_sync_func);
    Py_XDECREF(iter_sync_func);
    run_sync_func = NULL;
    iter_sync_func = NULL;
    return -1;
  }

  if (PyDict_SetItemString(dict, "run_sync", run_sync_func))
    return -1;
  if (PyDict_SetItemString(dict, "iter_sync", iter_sync_func))
    return -1;

  return 0;
}


int koskari_sync_clear(PyObject *mod) {

  KoskariSyncViolation = NULL;
  Py_CLEAR(as_coroutine_func);
  Py_CLEAR(run_sync_func);
  Py_CLEAR(iter_sync_func);
  (void) mod;
  return 0;
}


// The end.
