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
   koskari/lib/demand.c

   Demand path and KoskariSyncViolation. demand_call invokes Koskari callable
   without suspension (callee as root).
   Yielding KosFunctions return DemandGenerator instead of raising.
   AWAIT/YIELD inject (in-eval KoskariSyncViolation) is implemented only in
   demand_sync_ex run_eval; DemandGenerator (demand_gen_step) still raises
   at the iterator boundary on AWAIT.

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


/* === KosDemandGenerator (struct + type forward; methods follow Demand core) === */


typedef struct {
  PyObject_HEAD
  PyObject *code_name;  /* strong ref; KosCode name for repr (sync with agen->code_name) */
  PyObject *async_agen; /* KosPyAsyncGen: sole liveness / closed / ctx / gen */
} KosDemandGenerator;


static PyTypeObject KosDemandGeneratorType;


/* === Demand core (demand_sync_ex) === */


/**
  Base implementation: returns 0 (success with *out_result) or -1 (error,
  PyErr set).

  For KosCallable run_eval: on KOS_EV_AWAIT or KOS_EV_YIELD, injects
  KoskariSyncViolation via a second koskari_eval (throw into suspended context)
  so TRY/finally can run; uncaught, surfaces as KOS_EV_RAISE (-1, PyErr set).
  demand_sync_impl keeps defensive r==1 handling though run_eval no longer
  returns 1. DemandGenerator stepping is unchanged (see demand.c).

  callable, args, kwargs are borrowed. Never decref them.
 */
static int demand_sync_ex(
    PyObject *callable,
    PyObject *args,
    PyObject *kwargs,
    PyObject **out_result) {

  PyObject *frame_ns = NULL;
  KosDynFrame *frame = NULL;
  KosContext *ctx = NULL;
  KosEvent ev = {0};
  int r = 0;
  PyObject *result = NULL;

  PyObject *call_args = NULL;

  // if (! args) {
  //   args = PyTuple_New(0);
  //   if (! args)
  //     goto ret_fail;
  // }

  // DO NOT USE %R HERE. It will infinitely recurse into the demand_sync_ex
  // for the __repr__
  SAYF("[demand_sync] callable = %p", callable);

  /* Plain Python (not KosCallable): direct call, reject awaitable */
  if (! KosCallable_Check(callable)) {
    SAYF("[demand_sync] Plain Python (not KosCallable): callable = %p", callable);
    SAYF("[demand_sync] args = %R", args);
    SAYF("[demand_sync] kwargs = %R", kwargs);

    result = PyObject_Call(callable, args, kwargs);
    *out_result = result;
    return result? 0: -1;
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
      PyObject *agen_obj;
      KosPyAsyncGen *agen;

      if (! gen_node)
        goto ret_fail;
      frame = kos_dynframe_new(
          (KosDynNode *) gen_node, (KosCode *) code, frame_ns, NULL);
      if (! frame) {
        kos_dyngen_free(gen_node);
        goto ret_fail;
      }
      KosDynGen_ACTIVE(gen_node) = (struct KosDynNode *) frame;
      ctx = kos_context_new(frame);
      frame = NULL;
      if (! ctx)
        goto ret_fail;
      agen_obj = KosPyAsyncGen_New(ctx);
      if (! agen_obj) {
        kos_context_free(ctx);
        goto ret_fail;
      }
      agen = (KosPyAsyncGen *) agen_obj;
      KosDynGen_OWNER(gen_node) = (PyObject *) agen;
      agen->gen = (void *) gen_node;
      kos_dyngen_acquire(gen_node);
      Py_XINCREF(KosCode_NAME((KosCode *) code));
      agen->code_name = KosCode_NAME((KosCode *) code);

      {
        /* KosDemandGenerator holds no KosContext; eval state is only on agen->ctx. */
        KosDemandGenerator *dsg =
            (KosDemandGenerator *)
            PyObject_GC_New(KosDemandGenerator, &KosDemandGeneratorType);
        PyObject *gen_out;

        if (! dsg) {
          Py_DECREF(agen_obj);
          goto ret_fail;
        }
        dsg->code_name = NULL;
        if (agen->code_name) {
          Py_INCREF(agen->code_name);
          dsg->code_name = agen->code_name;
        }
        Py_INCREF(agen);
        dsg->async_agen = (PyObject *) agen;

        PyObject_GC_Track((PyObject *) dsg);
        gen_out = (PyObject *) dsg;
        Py_DECREF(agen_obj);
        Py_XDECREF(call_args);
        *out_result = gen_out;
      }
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

  {
    unsigned inject_iter = 0;
    const unsigned demand_sync_inject_cap = 1024;

    r = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    for (;;) {
      if (r < 0) {
        kos_context_free(ctx);
        ctx = NULL;
        goto ret_fail;
      }

      if (KosEvent_KIND(&ev) != KOS_EV_AWAIT
          && KosEvent_KIND(&ev) != KOS_EV_YIELD)
        break;

      if (++inject_iter > demand_sync_inject_cap) {
        if (KosEvent_PAYLOAD(&ev))
          Py_DECREF(KosEvent_PAYLOAD(&ev));
        KosEvent_PAYLOAD(&ev) = NULL;
        PyErr_SetString(PyExc_RuntimeError,
                        "demand_sync: suspend inject cap exceeded");
        kos_context_free(ctx);
        ctx = NULL;
        goto ret_fail;
      }

      SAYF("[demand_sync] inject iter=%u kind=%d\n",
           (unsigned) inject_iter, (int) KosEvent_KIND(&ev));

      if (KosEvent_PAYLOAD(&ev)) {
        Py_DECREF(KosEvent_PAYLOAD(&ev));
        KosEvent_PAYLOAD(&ev) = NULL;
      }

      {
        PyObject *msg = PyUnicode_FromString(
            "async suspension is not allowed in demand_sync");
        PyObject *exc_val;

        if (! msg) {
          kos_context_free(ctx);
          ctx = NULL;
          goto ret_fail;
        }
        exc_val = PyObject_CallOneArg(KoskariSyncViolation, msg);
        Py_DECREF(msg);
        if (! exc_val) {
          kos_context_free(ctx);
          ctx = NULL;
          goto ret_fail;
        }
        r = koskari_eval(ctx, &ev, NULL, KoskariSyncViolation, exc_val);
        Py_DECREF(exc_val);
        SAYF("[demand_sync] inject post_eval r=%d kind=%d\n",
             r, (int) KosEvent_KIND(&ev));
      }
    }
  }

  kos_context_free(ctx);
  ctx = NULL;

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
      PyErr_SetString(PyExc_RuntimeError,
                      "demand_sync: unexpected suspension after inject");
      break;

    case KOS_EV_NONE:
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      break;

    default:
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      break;
  }
  goto done;

ret_fail:
  Py_XDECREF(call_args);
  *out_result = NULL;
  return -1;

done:
  Py_XDECREF(call_args);
  *out_result = result;
  return result ? 0 : -1;
}


int Koskari_DemandSyncEx(
    PyObject *callable,
    PyObject *args,
    PyObject *kwargs,
    PyObject **out_result) {

  return demand_sync_ex(callable, args, kwargs, out_result);
}


static PyObject *demand_sync_impl(
    PyObject *callable,
    PyObject *args,
    PyObject *kwargs) {

  PyObject *result = NULL;
  int r = demand_sync_ex(callable, args, kwargs, &result);

  /* Defensive: run_eval no longer returns 1 (suspend -> inject). Plain
     Python and other paths do not use r==1 either. */
  if (r == 1) {
    PyErr_Format(KoskariSyncViolation,
                 "async suspension is not allowed in demand_call");
  }
  return result;
}


PyObject *Koskari_DemandSync(
    PyObject *callable,
    PyObject *args,
    PyObject *kwargs) {

  return demand_sync_impl(callable, args, kwargs);
}


/* === KosDemand (wrapper) === */


typedef struct {
  PyObject_HEAD
  PyObject *wrapped;
} KosDemandObject;


static PyObject *demand_new(
    PyTypeObject *type,
    PyObject *args,PyObject *kwds) {

  KosDemandObject *self = (KosDemandObject *) type->tp_alloc(type, 0);

  (void) args;
  (void) kwds;
  if (! self)
    return NULL;
  self->wrapped = NULL;
  return (PyObject *) self;
}


static int demand_init(PyObject *self, PyObject *args, PyObject *kwds) {

  KosDemandObject *d = (KosDemandObject *) self;
  PyObject *wrapped = NULL;
  static char *kwlist[] = {"wrapped", NULL};

  if (kwds && PyDict_Size(kwds)) {
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "O:Demand", kwlist, &wrapped))
      return -1;
  }
  else {
    if (! PyArg_ParseTuple(args, "O", &wrapped))
      return -1;
  }
  Py_CLEAR(d->wrapped);
  Py_INCREF(wrapped);
  d->wrapped = wrapped;
  return 0;
}


static PyObject *demand_get_wrapped_attr(PyObject *self, void *closure) {

  const char *name = (const char *) closure;
  KosDemandObject *d = (KosDemandObject *) self;

  if (! d->wrapped) {
    PyErr_Format(PyExc_AttributeError,
                 "'Demand' object has no attribute '%s'", name);
    return NULL;
  }
  return PyObject_GetAttrString(d->wrapped, name);
}


static PyMemberDef demand_members[] = {
  {"__wrapped__", T_OBJECT, offsetof(KosDemandObject, wrapped), READONLY, NULL},
  {NULL},
};


static PyGetSetDef demand_getset[] = {
  {"__doc__", (getter) demand_get_wrapped_attr, NULL, NULL,
   (void *) "__doc__"},
  {"__name__", (getter) demand_get_wrapped_attr, NULL, NULL,
   (void *) "__name__"},
  {"__qualname__", (getter) demand_get_wrapped_attr, NULL, NULL,
   (void *) "__qualname__"},
  {"__module__", (getter) demand_get_wrapped_attr, NULL, NULL,
   (void *) "__module__"},
  {"__annotations__", (getter) demand_get_wrapped_attr, NULL, NULL,
   (void *) "__annotations__"},
  {NULL},
};


static PyObject *demand_tp_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  KosDemandObject *d = (KosDemandObject *) self;

  if (! d->wrapped) {
    PyErr_SetString(PyExc_TypeError, "Demand object is not initialized");
    return NULL;
  }
  return Koskari_DemandSync(d->wrapped, args, kwargs);
}


static PyObject *demand_repr(PyObject *self) {
  KosDemandObject *d = (KosDemandObject *) self;
  return PyUnicode_FromFormat("Demand(%p)", (void *) d->wrapped);
}


static void demand_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  KosDemandObject *d = (KosDemandObject *) self;

  Py_CLEAR(d->wrapped);
  Py_TYPE(self)->tp_free(self);
}


static int demand_traverse(PyObject *self, visitproc visit, void *arg) {
  KosDemandObject *d = (KosDemandObject *) self;

  if (d->wrapped)
    Py_VISIT(d->wrapped);
  return 0;
}


static int demand_clear(PyObject *self) {
  KosDemandObject *d = (KosDemandObject *) self;

  Py_CLEAR(d->wrapped);
  return 0;
}


PyTypeObject KosDemandType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.Demand",
  .tp_basicsize = sizeof(KosDemandObject),
  .tp_itemsize = 0,
  .tp_dealloc = demand_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Callable adapter that runs its wrapped callable on the demand path",
  .tp_traverse = demand_traverse,
  .tp_clear = demand_clear,
  .tp_members = demand_members,
  .tp_getset = demand_getset,
  .tp_repr = demand_repr,
  .tp_call = demand_tp_call,
  .tp_new = demand_new,
  .tp_init = demand_init,
};


/* In-eval suspend violation (second koskari_eval throw) is not applied here;
   only demand_sync_ex run_eval; see demand_gen_step KOS_EV_AWAIT. */


static void demand_gen_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);

  KosDemandGenerator *gen = (KosDemandGenerator *) self;

  Py_CLEAR(gen->async_agen);
  Py_CLEAR(gen->code_name);

  Py_TYPE(self)->tp_free(self);
}


static int demand_gen_traverse(PyObject *self, visitproc visit, void *arg) {

  KosDemandGenerator *gen = (KosDemandGenerator *) self;

  Py_VISIT(gen->code_name);
  Py_VISIT(gen->async_agen);

  return 0;
}


static int demand_gen_clear(PyObject *self) {

  KosDemandGenerator *gen = (KosDemandGenerator *) self;

  Py_CLEAR(gen->code_name);
  Py_CLEAR(gen->async_agen);

  return 0;
}


static PyObject *demand_gen_iter(PyObject *self) {
  Py_INCREF(self);
  return self;
}


static void demand_gen_close_ctx(KosDemandGenerator *gen) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) gen->async_agen;

  if (agen->ctx) {
    kos_context_free(agen->ctx);
    agen->ctx = NULL;
  }
  agen->gen = NULL;
  agen->closed = 1;
}


static int install_frozen_gen_for_resume(
  KosContext *eval_ctx,
  KosPyAsyncGen *agen) {

  KosDynGen *g;

  if (! eval_ctx || ! agen)
    return 0;
  g = (KosDynGen *) agen->gen;
  if (! g || ! kos_dyn_gen_frozen_needs_install(eval_ctx, g))
    return 0;
  return kos_dyn_gen_install(eval_ctx, g);
}


static PyObject *demand_gen_step(
    KosDemandGenerator *gen,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosPyAsyncGen *agen = (KosPyAsyncGen *) gen->async_agen;

  if (! agen || ! agen->ctx || agen->closed) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  if (agen->in_progress) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator already being iterated");
    return NULL;
  }

  agen->in_progress = 1;

  if (install_frozen_gen_for_resume(agen->ctx, agen) < 0) {
    agen->in_progress = 0;
    return NULL;
  }

  KosEvent ev = {0};
  {
    int res = koskari_eval(agen->ctx, &ev, send_val, throw_typ, throw_exc);

    agen->in_progress = 0;

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

    case KOS_EV_NONE:
      demand_gen_close_ctx(gen);
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      return NULL;

    default:
      demand_gen_close_ctx(gen);
      PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
      return NULL;
    }
  }
}


static PyObject *demand_gen_iternext(PyObject *self) {
  return demand_gen_step((KosDemandGenerator *) self, NULL, NULL, NULL);
}


static PyObject *demand_gen_send(PyObject *self, PyObject *value) {

  if (! value)
    value = Py_None;
  Py_INCREF(value);
  PyObject *result =
      demand_gen_step((KosDemandGenerator *) self, value, NULL, NULL);
  Py_DECREF(value);
  return result;
}


static PyObject *demand_gen_throw(PyObject *self, PyObject *args) {

  KosDemandGenerator *gen = (KosDemandGenerator *) self;

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  if (! PyArg_ParseTuple(args, "O|OO:throw", &typ, &val, &tb))
    return NULL;

  {
    KosPyAsyncGen *wa = (KosPyAsyncGen *) gen->async_agen;
    if (! wa || wa->closed) {
      PyErr_SetNone(PyExc_StopIteration);
      return NULL;
    }
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
                  "DemandGenerator cannot be created directly");
  return NULL;
}


static PyObject *demand_gen_repr(PyObject *self) {

  KosDemandGenerator *gen = (KosDemandGenerator *) self;

  if (gen->code_name && PyUnicode_Check(gen->code_name))
    return PyUnicode_FromFormat(
        "<generator object %U at %p>", gen->code_name, (void *) self);
  return PyUnicode_FromFormat("<generator object at %p>", (void *) self);
}


static PyTypeObject KosDemandGeneratorType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.DemandGenerator",
  .tp_basicsize = sizeof(KosDemandGenerator),
  .tp_itemsize = 0,
  .tp_dealloc = demand_gen_dealloc,
  .tp_repr = demand_gen_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Sync generator for yielding Koskari functions in demand_call context",
  .tp_traverse = demand_gen_traverse,
  .tp_clear = demand_gen_clear,
  .tp_iter = demand_gen_iter,
  .tp_iternext = demand_gen_iternext,
  .tp_methods = demand_gen_methods,
  .tp_new = demand_gen_new,
};


PyObject *KosDemandGenerator_FromAsyncGen(KosPyAsyncGen *agen) {

  KosDynGen *dgen;
  KosDemandGenerator *dsg;
  PyObject *code_name = NULL;

  if (! agen->ctx || agen->closed) {
    PyErr_SetString(PyExc_RuntimeError, "async generator is closed");
    return NULL;
  }

  if (agen->in_progress) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator already being iterated");
    return NULL;
  }

  dgen = (KosDynGen *) agen->gen;
  if (! dgen) {
    PyErr_SetString(PyExc_RuntimeError, "async generator is closed");
    return NULL;
  }

  if (! KosDynGen_ACTIVE(dgen)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator has no active frame");
    return NULL;
  }

  {
    KosDynFrame *name_frame = (KosDynFrame *) KosDynGen_ACTIVE(dgen);
    KosCode *code;

    if (name_frame && (code = KosDynFrame_CODE(name_frame))) {
      Py_XINCREF(KosCode_NAME(code));
      code_name = KosCode_NAME(code);
    }
  }

  /* Thin wrapper only: do not install or otherwise mutate the dyn chain here;
     demand_gen_step installs via kos_dyn_gen_frozen_needs_install when stepping. */

  dsg = (KosDemandGenerator *)
      PyObject_GC_New(KosDemandGenerator, &KosDemandGeneratorType);
  if (! dsg) {
    Py_XDECREF(code_name);
    return NULL;
  }

  dsg->code_name = code_name;
  Py_INCREF(agen);
  dsg->async_agen = (PyObject *) agen;

  PyObject_GC_Track((PyObject *) dsg);
  return (PyObject *) dsg;
}


static PyObject *demand_call_kw(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  (void) self;
  if (! PyTuple_GET_SIZE(args)) {
    PyErr_SetString(PyExc_TypeError,
                    "demand_call() requires at least 1 argument (callable)");
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


static PyObject *demand_apply_kw(
    PyObject *self,
    PyObject *args,
    PyObject *kwargs) {

  PyObject *callable = NULL;
  PyObject *call_args = NULL;
  PyObject *call_kw = NULL;
  static char *kwlist[] = {"callable", "args", "kwargs", NULL};

  (void) self;
  if (! PyArg_ParseTupleAndKeywords(args, kwargs, "OO|O:demand_apply",
                                    kwlist, &callable, &call_args, &call_kw))
    return NULL;
  if (! PyTuple_Check(call_args)) {
    PyErr_SetString(PyExc_TypeError, "demand_apply: args must be a tuple");
    return NULL;
  }
  if (call_kw == Py_None)
    call_kw = NULL;
  else if (call_kw && ! PyDict_Check(call_kw)) {
    PyErr_SetString(PyExc_TypeError,
                    "demand_apply: kwargs must be a dict or None");
    return NULL;
  }
  return demand_sync_impl(callable, call_args, call_kw);
}


static PyMethodDef demand_call_methoddef = {
  "demand_call",
  (PyCFunction) demand_call_kw,
  METH_VARARGS | METH_KEYWORDS,
  "Invoke Koskari callable synchronously without suspension"
};


static PyMethodDef demand_apply_methoddef = {
  "demand_apply",
  (PyCFunction) demand_apply_kw,
  METH_VARARGS | METH_KEYWORDS,
  "Invoke Koskari callable on the demand path with tuple and optional dict args"
};


int koskari_demand_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  PyObject *demand_call_func = NULL;
  PyObject *demand_apply_func = NULL;

  if (! dict)
    return -1;

  if (register_koskari_sync_exception(mod,
          "koskari.lib.KoskariSyncViolation",
          "KoskariSyncViolation",
          (PyObject *) PyExc_TypeError,
          &KoskariSyncViolation) < 0)
    return -1;

  if (PyType_Ready(&KosDemandGeneratorType) < 0)
    return -1;

  if (PyType_Ready(&KosDemandType) < 0)
    return -1;

  demand_call_func = PyCFunction_New(&demand_call_methoddef, mod);
  if (! demand_call_func)
    return -1;
  if (PyDict_SetItemString(dict, "demand_call", demand_call_func) < 0) {
    Py_DECREF(demand_call_func);
    return -1;
  }
  Py_DECREF(demand_call_func);

  demand_apply_func = PyCFunction_New(&demand_apply_methoddef, mod);
  if (! demand_apply_func)
    return -1;
  if (PyDict_SetItemString(dict, "demand_apply", demand_apply_func) < 0) {
    Py_DECREF(demand_apply_func);
    return -1;
  }
  Py_DECREF(demand_apply_func);

  if (PyDict_SetItemString(dict, "demand", (PyObject *) &KosDemandType) < 0)
    return -1;

  /* Native test harness uses _koskari_test; no koskari package available */
  {
    PyObject *mod_name = PyModule_GetNameObject(mod);
    int is_harness = (mod_name &&
        PyUnicode_CompareWithASCIIString(mod_name, "_koskari_test") == 0);
    Py_XDECREF(mod_name);
    if (is_harness)
      return 0;
  }

  return 0;
}


int koskari_demand_clear(PyObject *mod) {

  KoskariSyncViolation = NULL;
  (void) mod;
  return 0;
}


// The end.
