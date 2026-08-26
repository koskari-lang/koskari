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

   Demand path and KoskariSyncViolation. demand_call invokes a Koskari callable
   without suspension (callee as root).

   For KosCallable run_eval: the root frame is marked KOS_FRAME_DEMAND; any
   attempt to suspend (KOS_RES_AWAIT or KOS_RES_YIELD) is converted into an
   in-eval KoskariSyncViolation at the deny site so TRY/finally can run;
   uncaught, the exception surfaces as KOS_RES_RAISE.

   Yielding KosFunctions return DemandGenerator instead of raising.
   DemandGenerator stepping forces KOS_FRAME_DEMAND onto the generator's
   frame at each entry (demand intent, embodied by the boundary), so
   AWAIT denies in-eval exactly as in demand functions -- catchable by
   the generator body for sync fallback; uncaught, it unwinds and exits
   as KOS_RES_RAISE. YIELD is untouched: it surfaces as iteration. The
   demand bit is drive-state: kos_dyn_gen_install assigns it from the
   frame below the splice at every entry, so flattened iteration
   inherits the consumer's demand state and async boundary drives force
   it off.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "debug.h"


/* === KoskariSyncViolation === */


static PyObject *KoskariSyncViolation = NULL;

static PyObject *str_await = NULL;
static PyObject *str_send = NULL;
static PyObject *str_throw = NULL;


void kos_demand_set_sync_violation(const char *msg) {
  PyObject *exc = PyObject_CallFunction(KoskariSyncViolation, "s", msg);
  if (! exc)
    return;
  PyErr_SetObject(KoskariSyncViolation, exc);
  Py_DECREF(exc);
}


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


/* === Eager-await for Python awaitables at the demand boundary === */


/**
   Eagerly step a Python awaitable returned at the demand boundary. On
   immediate completion (StopIteration), return the value. On suspension
   (the awaitable yields before completing), inject KoskariSyncViolation
   via .throw(); the callee may catch it and return a value, or re-raise.

   Steals the reference to `result`. Returns a new reference to the final
   value, or NULL with PyErr set. If `result` is not awaitable (no
   __await__), it is returned unchanged (still stolen).
*/
static PyObject *demand_step_python_result(PyObject *result) {

  PyObject *await_iter = NULL;
  PyObject *step = NULL;

  /* Try __await__; if absent, not an awaitable -- return as-is. */
  PyObject *await_meth = PyObject_GetAttr(result, str_await);
  if (! await_meth) {
    if (! PyErr_ExceptionMatches(PyExc_AttributeError))
      goto steal_fail;
    PyErr_Clear();
    return result;
  }
  await_iter = PyObject_CallNoArgs(await_meth);
  Py_DECREF(await_meth);
  Py_DECREF(result);
  result = NULL;
  if (! await_iter)
    return NULL;

  /* First step: send(None). */
  step = PyObject_CallMethodObjArgs(await_iter, str_send, Py_None, NULL);

  if (! step) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration))
      goto iter_fail;
    /* Completed immediately. */
    PyObject *value = NULL;
    if (PyGen_FetchStopIterationValue(&value) < 0)
      goto iter_fail;
    Py_DECREF(await_iter);
    return value ? value : (Py_INCREF(Py_None), Py_None);
  }

  /* Suspended: discard yielded value, inject KoskariSyncViolation. */
  Py_DECREF(step);
  step = NULL;

  {
    static const char msg[] = "async suspension is not allowed in demand_sync";
    PyObject *msg_str = PyUnicode_FromString(msg);
    if (! msg_str)
      goto iter_fail;
    PyObject *exc = PyObject_CallOneArg(KoskariSyncViolation, msg_str);
    Py_DECREF(msg_str);
    if (! exc)
      goto iter_fail;
    step = PyObject_CallMethodObjArgs(await_iter, str_throw, exc, NULL);
    Py_DECREF(exc);
  }

  if (! step) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration))
      goto iter_fail;
    /* Callee caught the violation and returned a value. */
    PyObject *value = NULL;
    if (PyGen_FetchStopIterationValue(&value) < 0)
      goto iter_fail;
    Py_DECREF(await_iter);
    return value ? value : (Py_INCREF(Py_None), Py_None);
  }

  /* throw() returned without raising: still suspended. */
  Py_DECREF(step);
  kos_demand_set_sync_violation("async suspension is not allowed in demand_sync");

iter_fail:
  Py_DECREF(await_iter);
  return NULL;

steal_fail:
  Py_DECREF(result);
  return NULL;
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

  For KosCallable run_eval: the root frame is marked KOS_FRAME_DEMAND; in-eval
  deny guards convert AWAIT/YIELD into KoskariSyncViolation before koskari_eval
  returns, so KOS_RES_AWAIT and KOS_RES_YIELD are unreachable here.
  DemandGenerator stepping is unchanged (see demand.c).

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

  /* KosDemand wrapper (or a subtype such as Procedure): unwrap and recurse
     on the inner callable directly. Must come before KosCallable_Check
     since Demand is now a KosCallable. */
  if (KosDemand_Check(callable)) {
    PyObject *inner = KosDemand_GetWrapped(callable);
    if (! inner) {
      PyErr_SetString(PyExc_TypeError, "Demand object is not initialized");
      return -1;
    }
    return demand_sync_ex(inner, args, kwargs, out_result);
  }

  /* Plain Python (not KosCallable): call, then eagerly step any awaitable. */
  if (! KosCallable_Check(callable)) {
    SAYF("[demand_sync] Plain Python (not KosCallable): callable = %p", callable);
    SAYF("[demand_sync] args = %R", args);
    SAYF("[demand_sync] kwargs = %R", kwargs);

    if (! args && ! kwargs) {
      result = PyObject_CallNoArgs(callable);
    } else {
      result = PyObject_Call(callable, args, kwargs);
    }

    if (result)
      result = demand_step_python_result(result);

    *out_result = result;
    return result ? 0 : -1;
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
    Py_XINCREF(call_args);
  }

  /* BoundMethod may unwrap to Demand (or a subtype): recurse with bound
     args. */
  if (KosDemand_Check(callable)) {
    PyObject *inner = KosDemand_GetWrapped(callable);
    if (! inner) {
      Py_DECREF(call_args);
      PyErr_SetString(PyExc_TypeError, "Demand object is not initialized");
      return -1;
    }
    int r = demand_sync_ex(inner, call_args, kwargs, out_result);
    Py_DECREF(call_args);
    return r;
  }

  /* BoundMethod may also unwrap to other non-KosCallable */
  if (! KosCallable_Check(callable)) {
    if (! call_args && ! kwargs) {
      result = PyObject_CallNoArgs(callable);
    } else {
      result = PyObject_Call(callable, call_args, kwargs);
    }
    Py_DECREF(call_args);
    if (result)
      result = demand_step_python_result(result);
    *out_result = result;
    return result ? 0 : -1;
  }

  /* KosExpression */
  if (KosExpression_CheckExact(callable)) {
    if ((call_args && PyTuple_GET_SIZE(call_args) != 0)
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
      PyObject *agen_obj;
      KosPyAsyncGen *agen;

      if (! gen_node) {
        Py_DECREF(ns);
        goto ret_fail;
      }
      frame = kos_dynframe_new(
          (KosDynNode *) gen_node, (KosCode *) code, ns);
      if (! frame) {
        kos_dyngen_free(gen_node);
        Py_DECREF(ns);
        goto ret_fail;
      }
      ctx = KosContext_New(frame);
      if (! ctx) {
        kos_dynframe_free(frame);
        frame = NULL;
        kos_dyngen_free(gen_node);
        goto ret_fail;
      }
      frame = NULL;
      agen_obj = KosPyAsyncGen_New(ctx);
      if (! agen_obj) {
        KosContext_Free(ctx);
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
    else {
      frame = kos_dynframe_new(NULL, (KosCode *) code, ns);
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
          (KosDynNode *) gen_node, (KosCode *) code, frame_ns);
      if (! frame) {
        kos_dyngen_free(gen_node);
        goto ret_fail;
      }
      ctx = KosContext_New(frame);
      if (! ctx) {
        kos_dynframe_free(frame);
        frame = NULL;
        kos_dyngen_free(gen_node);
        goto ret_fail;
      }
      frame = NULL;
      agen_obj = KosPyAsyncGen_New(ctx);
      if (! agen_obj) {
        KosContext_Free(ctx);
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

    frame = kos_dynframe_new(NULL, (KosCode *) code, frame_ns);
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

  /* Other KosCallable - not supported for demand_sync yet */
  PyErr_Format(PyExc_TypeError,
               "demand_sync does not support %s",
               Py_TYPE(callable)->tp_name);
  goto ret_fail;

run_eval:
  /* Mark this frame as a demand root; in-eval deny guards will detect it
     when walking ancestors and convert any AWAIT/YIELD into an in-eval
     KoskariSyncViolation before koskari_eval returns. */
  KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND;

  ctx = KosContext_New(frame);
  if (! ctx) {
    kos_dynframe_free(frame);
    frame = NULL;
    goto ret_fail;
  }
  frame = NULL;

  {
    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    KosContext_Free(ctx);
    ctx = NULL;

    switch (ev.kind) {
      case KOS_RES_RETURN:
        result = ev.val;
        break;

      case KOS_RES_RAISE:
      case KOS_RES_FATAL:
        /* PyErr already set */
        break;

      case KOS_RES_AWAIT:
      case KOS_RES_YIELD:
        /* Both should be intercepted before reaching here — this is a bug. */
        Py_XDECREF(ev.val);
        PyErr_SetString(PyExc_RuntimeError,
                        "demand_sync: unexpected suspension (missing deny guard)");
        break;

      default:
        PyErr_SetString(PyExc_RuntimeError, "unexpected KosOpResult kind");
        break;
    }
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
  demand_sync_ex(callable, args, kwargs, &result);
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
    PyObject *args,
    PyObject *kwds) {

  PyObject *wrapped = NULL;

  if (PyTuple_GET_SIZE(args) == 1 && (! kwds || ! PyDict_Size(kwds))) {
    wrapped = PyTuple_GET_ITEM(args, 0);
    if (PyObject_TypeCheck(wrapped, type)) {
      Py_INCREF(wrapped);
      return wrapped;
    }

  } else if (kwds && PyDict_Size(kwds) == 1 && PyTuple_GET_SIZE(args) == 0) {
    wrapped = PyDict_GetItemString(kwds, "wrapped");
    if (wrapped && PyObject_TypeCheck(wrapped, type)) {
      Py_INCREF(wrapped);
      return wrapped;
    }
  }

  KosDemandObject *self = (KosDemandObject *) type->tp_alloc(type, 0);

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

  } else {
    if (! PyArg_ParseTuple(args, "O", &wrapped))
      return -1;
  }

  if (self == wrapped)
    return 0;

  Py_CLEAR(d->wrapped);
  Py_INCREF(wrapped);
  d->wrapped = wrapped;
  return 0;
}


PyObject *KosDemand_GetWrapped(PyObject *obj) {

  KosDemandObject *const d = (KosDemandObject *) obj;

  return d->wrapped;
}


PyObject *KosDemand_New(PyObject *wrapped) {

  return PyObject_CallOneArg(
      (PyObject *) KOS_EVALUATED_PYTYPE(KosDemandCallableType), wrapped);
}


static PyObject *demand_descr_get(
    PyObject *self,
    PyObject *obj,
    PyObject *owner) {

  KosDemandObject *const d = (KosDemandObject *) self;

  (void) owner;

  if (! d->wrapped) {
    PyErr_SetString(PyExc_TypeError, "Demand object is not initialized");
    return NULL;
  }

  if (obj == NULL) {
    Py_INCREF(self);
    return self;
  }

  return KosBoundMethod_New(self, obj);
}


static PyObject *demand_get_wrapped_attr(PyObject *self, const void *closure) {

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
  {"__isabstractmethod__", (getter) demand_get_wrapped_attr, NULL, NULL,
   (void *) "__isabstractmethod__"},
  {NULL},
};




static PyObject *demand_repr(PyObject *self) {
  KosDemandObject *d = (KosDemandObject *) self;
  PyObject *name = NULL;
  PyObject *result = NULL;

  if (! d->wrapped)
    return PyUnicode_FromString("<Demand ?>");

  name = PyObject_GetAttrString(d->wrapped, "__name__");
  if (! name) {
    PyErr_Clear();
    return PyUnicode_FromString("<Demand <lambda>>");
  }

  result = PyUnicode_FromFormat("<Demand %R>", name);
  Py_DECREF(name);
  return result;
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


/**
  Return 1 if inner is a non-yielding KosFunction or KosExpression that can be
  installed directly into the current frame. Returning 0 routes to the
  demand_sync_ex fallback path (Python callable, yielding function, nested
  Demand, or any other KosCallable type).
*/
static int demand_inner_is_flattenable(PyObject *inner) {

  if (KosDemand_CheckExact(inner))
    return 0;

  if (! KosCallable_Check(inner))
    return 0;

  return KosCallable_WantsFrame(inner);
}


static int demand_wants_frame(PyObject *callable) {

  KosDemandObject *d = (KosDemandObject *) callable;
  return demand_inner_is_flattenable(d->wrapped);
}


static KosOpResult demand_kos_install(
    KosEvalState *es,
    PyObject *callable,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    int tail_call) {

  KosDemandObject *d = (KosDemandObject *) callable;
  PyObject *inner = d->wrapped;

  if (! inner) {
    PyErr_SetString(PyExc_TypeError, "Demand object is not initialized");
    return EVAL_RAISE();
  }

  if (demand_inner_is_flattenable(inner)) {
    /* Install inner directly into the frame prepared by do_frame_prep, then
       mark that frame as the demand root so in-eval deny guards fire. */
    KosOpResult r = KosCallable_CallArgs(es, inner, args_tuple, kwds_dict,
                                        tail_call);
    if (r.kind == KOS_RES_NORMAL) {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      if (frame)
        KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND;
    }
    return r;
  }

  /* Fallback: Python callable, yielding function, nested Demand, or
     unsupported KosCallable.  demand_sync_ex creates its own context and
     returns a concrete result (or DemandGenerator for yielding functions).
     Push that result on the current frame's stack and return NORMAL so that
     KosEvalState_Call handles tail-call conversion the same way
     kos_install_function does for yielding KosFunctions. */
  PyObject *result = NULL;
  if (demand_sync_ex(inner, args_tuple, kwds_dict, &result) < 0)
    return EVAL_RAISE();

  if (KosEvalState_StackPUSH(es, result) < 0)
    return EVAL_RAISE();  /* PUSH steals reference; already decremented on failure */

  return EVAL_NORMAL();
}


static KosOpResult demand_kos_vinstall(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  KosDemandObject *d = (KosDemandObject *) callable;
  PyObject *inner = d->wrapped;

  if (! inner) {
    PyErr_SetString(PyExc_TypeError, "Demand object is not initialized");
    return EVAL_RAISE();
  }

  if (demand_inner_is_flattenable(inner)) {
    /* Install inner directly into the frame prepared by do_frame_prep
       -- on the vector path, so the inner KosFunction gets its vector
       (and simple-call) prep.  Then mark the frame as the demand root
       so in-eval deny guards fire. */
    KosOpResult r = KosCallable_Call(es, inner, args, nargs, kwnames,
                                     tail_call);
    if (r.kind == KOS_RES_NORMAL) {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      if (frame)
        KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND;
    }
    return r;
  }

  /* Cold fallback: demand_sync_ex needs the tuple + dict shapes. */
  {
    Py_ssize_t nkw = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
    PyObject *tup = PyTuple_New(nargs);
    PyObject *kwargs = NULL;
    PyObject *result = NULL;
    int sr;

    if (! tup)
      return EVAL_RAISE();
    for (Py_ssize_t i = 0; i < nargs; i++) {
      PyObject *v = args[i];
      Py_INCREF(v);
      PyTuple_SET_ITEM(tup, i, v);
    }
    if (nkw) {
      kwargs = PyDict_New();
      if (! kwargs) {
        Py_DECREF(tup);
        return EVAL_RAISE();
      }
      for (Py_ssize_t i = 0; i < nkw; i++) {
        if (PyDict_SetItem(kwargs, PyTuple_GET_ITEM(kwnames, i),
                           args[nargs + i]) < 0) {
          Py_DECREF(tup);
          Py_DECREF(kwargs);
          return EVAL_RAISE();
        }
      }
    }

    sr = demand_sync_ex(inner, tup, kwargs, &result);
    Py_DECREF(tup);
    Py_XDECREF(kwargs);
    if (sr < 0)
      return EVAL_RAISE();

    if (KosEvalState_StackPUSH(es, result) < 0)
      return EVAL_RAISE();
    return EVAL_NORMAL();
  }
}


static KosCallSlots demand_call_slots = {
  .ki_install = demand_kos_install,
  .ki_vinstall = demand_kos_vinstall,
  .ki_wants_frame = 0,
  .ki_wants_frame_fn = demand_wants_frame,
};


KosEvaluatedTypeObject KosDemandCallableType = {
  .type = {
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
    .tp_call = KosCallAdapt_CallDemand,
    .tp_descr_get = demand_descr_get,
    .tp_new = demand_new,
    .tp_init = demand_init,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .tp_kos_call = &demand_call_slots,
};


/* In-eval suspend violation (second koskari_eval throw) is not applied here;
   only demand_sync_ex run_eval; see demand_gen_step KOS_RES_AWAIT. */


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

  if (agen) {
    KosPyAsyncGen_Teardown(agen);
    agen->closed = 1;
  }
  Py_CLEAR(gen->async_agen);
}


static int install_frozen_gen_for_resume(
  KosContext *eval_ctx,
  KosPyAsyncGen *agen) {

  KosDynGen *g;

  if (! eval_ctx || ! agen)
    return 0;
  g = (KosDynGen *) agen->gen;
  {
    KosEvalState es;
    int r;

    KosEvalState_Init(&es, eval_ctx);
    KosEvalState_ThawFromContext(&es);
    if (! g || ! kos_dyn_gen_frozen_needs_install_es(&es, g)) {
      KosEvalState_FreezeToContext(&es);
      return 0;
    }
    r = kos_dyn_gen_install(&es, g);
    KosEvalState_FreezeToContext(&es);
    return r;
  }
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

  /* Demand intent, embodied: every DemandGenerator drive passes here,
     so force KOS_FRAME_DEMAND onto the generator's frame post-install
     (install assigned the invoker bit, 0 at this boundary). AWAIT then
     denies in-eval as KoskariSyncViolation -- catchable by the body for
     sync fallback; uncaught, it unwinds and exits via KOS_RES_RAISE. */
  {
    KosDynFrame *frame = KosContext_FRAME_TOP(agen->ctx);
    if (frame)
      KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND;
  }

  {
    agen->started = 1;
    KosOpResult ev = koskari_eval(agen->ctx, send_val, throw_typ, throw_exc);

    agen->in_progress = 0;

    switch (ev.kind) {
    case KOS_RES_YIELD:
      return ev.val;

    case KOS_RES_RETURN: {
      demand_gen_close_ctx(gen);
      PyObject *payload = ev.val;
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

    case KOS_RES_RAISE:
    case KOS_RES_FATAL:
      demand_gen_close_ctx(gen);
      /* PyErr already set */
      return NULL;

    case KOS_RES_AWAIT:
      /* Unreachable: the frame carries KOS_FRAME_DEMAND (forced above),
         so eval_event_await converts AWAIT to an in-eval
         KoskariSyncViolation raise before it can surface here. Kept
         defensively, mirroring demand_sync_ex. */
      demand_gen_close_ctx(gen);
      Py_XDECREF(ev.val);
      PyErr_Format(KoskariSyncViolation,
                   "async suspension is not allowed in demand generator");
      return NULL;

    default:
      demand_gen_close_ctx(gen);
      PyErr_SetString(PyExc_RuntimeError, "unexpected KosOpResult kind");
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
    KosPyAsyncGen *const wa = (KosPyAsyncGen *) gen->async_agen;
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


/**
   Kin to coroutine_close (coro.c), not shared with it: throw
   GeneratorExit through the body so finally blocks run, then classify
   the outcome. demand_gen_step already tears the context down on every
   terminal path, and demand_gen_throw's closed guard makes a second
   close a quiet None.
*/
static PyObject *demand_gen_close(PyObject *self) {

  PyObject *close_args = Py_BuildValue("(O)", PyExc_GeneratorExit);
  if (! close_args)
    return NULL;

  PyObject *result = demand_gen_throw(self, close_args);
  Py_DECREF(close_args);

  if (result) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_RuntimeError,
                    "generator ignored GeneratorExit");
    return NULL;
  }

  if (PyErr_ExceptionMatches(PyExc_StopIteration) ||
      PyErr_ExceptionMatches(PyExc_GeneratorExit)) {
    PyErr_Clear();
    Py_RETURN_NONE;
  }

  return NULL;
}


static PyMethodDef demand_gen_methods[] = {
  { "send", (PyCFunction) demand_gen_send, METH_O,
    "Send a value into the generator" },
  { "throw", (PyCFunction) demand_gen_throw, METH_VARARGS,
    "Raise an exception in the generator" },
  { "close", (PyCFunction) demand_gen_close, METH_NOARGS,
    "Close the generator" },
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
  KosDynFrame *name_frame = NULL;

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

  {
    KosDynNode *node;

    node = (KosDynNode *) KosDynGen_ACTIVE(dgen);
    if (node) {
      for (; node && node != (KosDynNode *) dgen;
           node = KosDynNode_PARENT(node)) {
        if (KosDynNode_IS_FRAME(node)) {
          name_frame = (KosDynFrame *) node;
          break;
        }
      }
    } else {
      name_frame = KosContext_FRAME_TOP(agen->ctx);
      if (! name_frame) {
        for (node = KosContext_DYN_TOP(agen->ctx); node;
             node = KosDynNode_PARENT(node)) {
          if (KosDynNode_IS_FRAME(node)) {
            name_frame = (KosDynFrame *) node;
            break;
          }
        }
      }
    }
  }

  if (name_frame) {
    KosCode *code = KosDynFrame_CODE(name_frame);

    if (code) {
      Py_XINCREF(KosCode_NAME(code));
      code_name = KosCode_NAME(code);
    }
  }

  /* Thin wrapper only: do not install or otherwise mutate the dyn chain here;
     demand_gen_step installs via kos_dyn_gen_frozen_needs_install_es when stepping. */

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

  STR_CONST(str_await, "__await__");
  STR_CONST(str_send, "send");
  STR_CONST(str_throw, "throw");

  if (register_koskari_sync_exception(mod,
          "koskari.lib.KoskariSyncViolation",
          "KoskariSyncViolation",
          (PyObject *) PyExc_TypeError,
          &KoskariSyncViolation) < 0)
    return -1;

  if (PyType_Ready(&KosDemandGeneratorType) < 0)
    return -1;

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosDemandCallableType)) < 0)
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

  if (PyDict_SetItemString(dict, "demand",
                          (PyObject *) KOS_EVALUATED_PYTYPE(KosDemandCallableType)) < 0)
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
  Py_CLEAR(str_await);
  Py_CLEAR(str_send);
  Py_CLEAR(str_throw);
  (void) mod;
  return 0;
}


// The end.
