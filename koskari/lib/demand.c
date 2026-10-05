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

   Demand path and KoskariSyncViolation. demand_call invokes a Koskari
   callable from Python without suspension.

   The demand core is the Python boundary's entry into Koskari: a
   KosCallable of any kind is called through KosEvalState_CallInto from a
   code-less root frame marked KOS_FRAME_DEMAND, on the boundary's own
   context. Any attempt to suspend (KOS_RES_AWAIT or KOS_RES_YIELD) under
   the mark is converted into an in-eval KoskariSyncViolation at the deny
   site so TRY/finally can run; uncaught, the exception surfaces as
   KOS_RES_RAISE.

   Demand applies to a generator's drive, not its construction: a
   yielding callable returns its AsyncGenerator as is. KosPyIter stepping
   from Python forces KOS_FRAME_DEMAND onto the generator's frame at
   each entry, so AWAIT denies in-eval exactly as in demand functions --
   catchable by the generator body for sync fallback; uncaught, it
   unwinds and exits as KOS_RES_RAISE. YIELD is untouched: it surfaces
   as iteration. The demand bit is drive-state: kos_dyn_gen_install
   assigns it from the frame below the splice at every entry, so
   flattened iteration -- over a KosPyIter too -- inherits the
   consumer's demand state and async node drives force it off.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "debug.h"


/* === KoskariSyncViolation === */


static PyObject *KoskariSyncViolation = NULL;



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
   Eagerly step a Python awaitable returned at the demand boundary,
   through kos_await_first_step. On immediate completion, return the
   value. On suspension, throw KoskariSyncViolation into the await
   iterator; the callee may catch it and return a value, or re-raise.

   Steals the reference to `result`. Returns a new reference to the final
   value, or NULL with PyErr set.
*/
static PyObject *demand_step_python_result(PyObject *result) {

  PyObject *await_iter = NULL;
  PyObject *yielded = NULL;
  PyObject *value = kos_await_first_step(result, &await_iter, &yielded);

  Py_DECREF(result);
  if (value)
    return value;
  if (! await_iter)
    return NULL;

  /* Suspended: discard the yield, inject KoskariSyncViolation. */
  Py_DECREF(yielded);

  {
    static const char msg[] = "async suspension is not allowed in demand_sync";
    PyObject *msg_str = PyUnicode_FromString(msg);
    if (! msg_str)
      goto iter_fail;
    PyObject *exc = PyObject_CallOneArg(KoskariSyncViolation, msg_str);
    Py_DECREF(msg_str);
    if (! exc)
      goto iter_fail;
    PySendResult step = kos_iter_throw(await_iter, exc, &value);
    Py_DECREF(exc);
    if (step == PYGEN_ERROR)
      goto iter_fail;
    if (step == PYGEN_RETURN) {
      /* Callee caught the violation and returned a value. */
      Py_DECREF(await_iter);
      return value;
    }
  }

  /* throw() returned without raising: still suspended. */
  Py_DECREF(value);
  kos_demand_set_sync_violation("async suspension is not allowed in demand_sync");

iter_fail:
  Py_DECREF(await_iter);
  return NULL;
}


/**
   Finish a value at the demand boundary, whether a Python callable or
   a Koskari install produced it. A plain value passes through; one
   that genuinely needs awaiting is eagerly stepped, and denied as a
   sync violation if it suspends. KosEval_ResultIsValue is the gate
   rather than demand_step_python_result's own __await__ probe, so a
   Defer or a future -- both exempt from ambient await -- passes
   through as the value it is instead of being stepped.

   Steals value. Returns a new reference, or NULL with PyErr set.
*/
static PyObject *demand_finish_value(PyObject *value) {

  switch (KosEval_ResultIsValue(value)) {
    case 1:
      return value;
    case 0:
      return demand_step_python_result(value);
    default:
      Py_DECREF(value);
      return NULL;
  }
}


/* === Demand core === */


/**
   The resume on a demand code-less frame: completes the frame with the
   called callable's result. A failed delivery propagates.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult demand_resume(
    KosEvalState *es,
    void *state,
    PyObject *value) {

  (void) state;

  if (! value)
    return EVAL_RAISE();

  return KosNativeFrame_ReturnToCache(es, value, NULL);
}


KOS_RESUME_KIND(demand_resume_kind, "demand",
                demand_resume, NULL, kos_resume_clone_shared);


/**
   The drive context for one boundary call: the wrapper's parked host
   when there is one, taken out of the park before anything else runs
   so no other call can see it, and refreshed to the dynamic state in
   force at this call; otherwise a fresh context. host is NULL when
   there is no wrapper to park on. New reference.
*/
static KosContext *demand_acquire_host(KosContext **host) {

  if (host && *host) {
    KosContext *ctx = *host;
    *host = NULL;
    if (KosContext_TakeSnapshot(ctx) < 0) {
      Py_DECREF(ctx);
      return NULL;
    }
    return ctx;
  }
  return KosContext_New(NULL);
}


/**
   Return the drive context after the call: recycled, it stays parked
   or takes the empty park; one the recycle cannot account for, or
   with nowhere to park, is released. Consumes ctx.
*/
static void demand_release_host(KosContext **host, KosContext *ctx) {

  if (! host) {
    Py_DECREF(ctx);
    return;
  }
  if (KosContext_Recycle(ctx) < 0) {
    Py_DECREF(ctx);
    return;
  }
  if (! *host) {
    /* the park holds nothing of the finished call: the next acquire's
       snapshot sets both again */
    Py_CLEAR(KosContext_DOMAIN(ctx));
    Py_CLEAR(KosContext_PARAM_REFS(ctx));
    *host = ctx;
    return;
  }
  Py_DECREF(ctx);
}


/**
   Run a KosCallable to completion from the Python boundary: record the
   call on the drive context (the wrapper's parked host, or a fresh
   one), and koskari_eval installs it at entry through
   KosEvalState_CallInto from a code-less root frame marked
   KOS_FRAME_DEMAND, which is the caller frame every install delivers
   into, then runs it to its return. Any KosCallable is accepted. The
   deny guards convert AWAIT and YIELD under the demand mark into
   KoskariSyncViolation, so koskari_eval answers RETURN, RAISE, or
   FATAL.

   args and kwnames are borrowed. Returns a new reference, or NULL with
   PyErr set.
*/
static PyObject *demand_run_koskari(
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    KosContext **host) {

  KosEntryCall call = {
    callable, args, nargs, kwnames, &demand_resume_kind, KOS_FRAME_DEMAND,
  };
  KosContext *ctx = demand_acquire_host(host);
  KosOpResult r;

  if (! ctx)
    return NULL;

  KosContext_ENTRY_CALL(ctx) = &call;
  r = koskari_eval(ctx, NULL, NULL, NULL);
  KosContext_ENTRY_CALL(ctx) = NULL;
  demand_release_host(host, ctx);

  switch (r.kind) {
    case KOS_RES_RETURN:
      /* The returned value is classified like any other settled
         result: a function that returns an awaitable as data gets the
         same eager-step-or-deny treatment a plain Python callable's
         result gets, instead of leaking an unstepped awaitable out of
         the demand boundary. */
      return demand_finish_value(r.val);

    case KOS_RES_RAISE:
    case KOS_RES_FATAL:
      return NULL;

    case KOS_RES_AWAIT:
    case KOS_RES_YIELD:
      Py_XDECREF(r.val);
      PyErr_SetString(PyExc_RuntimeError,
                      "demand_sync: unexpected suspension "
                      "(missing deny guard)");
      return NULL;

    default:
      PyErr_SetString(PyExc_RuntimeError, "unexpected KosOpResult kind");
      return NULL;
  }
}


/**
   The demand core over a vectorcall-shaped argument vector. A plain
   Python callable is called and its result finished; a KosCallable
   runs through demand_run_koskari. args and kwnames are borrowed.
   Returns a new reference, or NULL with PyErr set.
*/
static PyObject *demand_sync_vector(
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    KosContext **host) {

  SAYF("[demand_sync] callable = %p", callable);

  if (! KosCallable_Check(callable)) {
    PyObject *result = PyObject_Vectorcall(callable, args, (size_t) nargs,
                                           kwnames);
    return result ? demand_finish_value(result) : NULL;
  }

  return demand_run_koskari(callable, args, nargs, kwnames, host);
}


/**
   The demand core over a tuple and a dict. A plain Python callable
   takes them as they are through PyObject_Call; a KosCallable's are
   spread once to a vector for demand_run_koskari. No buffer: this
   frame sits on the C stack of every boundary re-entry, and keyword
   calls here are rare enough to take the heap.
*/
PyObject *Koskari_DemandSync(
    PyObject *callable,
    PyObject *args,
    PyObject *kwargs) {

  KosArgSpread spread;
  PyObject *result;

  SAYF("[demand_sync] callable = %p", callable);

  if (! KosCallable_Check(callable)) {
    PyObject *empty = NULL;

    if (! args) {
      empty = PyTuple_New(0);
      if (! empty)
        return NULL;
      args = empty;
    }
    result = PyObject_Call(callable, args, kwargs);
    Py_XDECREF(empty);
    return result ? demand_finish_value(result) : NULL;
  }

  if (KosArgSpread_Init(&spread, args, kwargs, NULL, 0) < 0)
    return NULL;
  result = demand_run_koskari(callable, spread.argv, spread.nargs,
                              spread.kwnames, NULL);
  KosArgSpread_Clear(&spread);
  return result;
}


int Koskari_DemandSyncVector(
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    PyObject **out_result) {

  *out_result = demand_sync_vector(callable, args, nargs, kwnames, NULL);
  return *out_result ? 0 : -1;
}


/* === KosDemand (wrapper) === */


/**
   The Python vectorcall entry, and the type's tp_call through
   PyVectorcall_Call: the demand core over the vector on the wrapped
   callable. A method-descriptor call from CPython arrives here unbound
   with self first in args.
*/
static PyObject *demand_vectorcall(
    PyObject *self,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  PyObject *wrapped = KosDemand_GetWrapped(self);

  if (! wrapped) {
    PyErr_SetString(PyExc_TypeError, "Demand object is not initialized");
    return NULL;
  }
  if (kwnames && ! PyTuple_GET_SIZE(kwnames))
    kwnames = NULL;
  return demand_sync_vector(wrapped, args, PyVectorcall_NARGS(nargsf),
                            kwnames, &((KosDemandObject *) self)->host);
}



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
  KosCallSlots *slots;

  if (! self)
    return NULL;

  self->wrapped = NULL;
  self->host = NULL;
  self->vectorcall = KosCallable_CheckSlots((PyObject *) self, &slots)
      ? slots->kc_pyvectorcall : NULL;
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
    return PyUnicode_FromString("<koskari demand ?>");

  name = PyObject_GetAttrString(d->wrapped, "__name__");
  if (! name) {
    PyErr_Clear();
    return PyUnicode_FromString("<koskari demand <lambda>>");
  }

  result = PyUnicode_FromFormat("<koskari demand %R>", name);
  Py_DECREF(name);
  return result;
}


static void demand_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  KosDemandObject *d = (KosDemandObject *) self;

  Py_CLEAR(d->wrapped);
  Py_CLEAR(d->host);
  Py_TYPE(self)->tp_free(self);
}


static int demand_traverse(PyObject *self, visitproc visit, void *arg) {
  KosDemandObject *d = (KosDemandObject *) self;

  if (d->wrapped)
    Py_VISIT(d->wrapped);
  Py_VISIT(d->host);
  return 0;
}


static int demand_clear(PyObject *self) {
  KosDemandObject *d = (KosDemandObject *) self;

  Py_CLEAR(d->wrapped);
  Py_CLEAR(d->host);
  return 0;
}


/**
   Evaluator install: call the inner through the call entry, whatever
   it is. A settled value the call completes in hand passes; an
   awaitable settles under a code-less frame marked KOS_FRAME_DEMAND;
   an installed callee's frame is marked the same, so the in-eval deny
   guards fire under either.
*/
static KosOpResult demand_kos_vinstall(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  KosDemandObject *d = (KosDemandObject *) callable;
  PyObject *inner = d->wrapped;
  KosOpResult r;

  if (! inner) {
    PyErr_SetString(PyExc_TypeError, "Demand object is not initialized");
    return EVAL_RAISE();
  }

  r = KosEvalState_Call(
      es, inner, args, nargs, kwnames, tail_call, NULL, NULL);
  switch (r.kind) {
    case KOS_RES_RETURN:
      switch (KosEval_ResultIsValue(r.val)) {
        case 1:
          return r;
        case 0:
          /* an awaitable in hand settles under a demand-marked frame,
             so a suspension is denied in-eval at the guard */
          return KosEvalState_SettleInto(
              es, r.val, &demand_resume_kind, NULL, KOS_FRAME_DEMAND);
        default:
          Py_DECREF(r.val);
          return EVAL_RAISE();
      }

    case KOS_RES_NORMAL:
    case KOS_RES_POLL:
    case KOS_RES_REPROLOGUE: {
      KosDynFrame *frame = KosEvalState_FRAME(es);
      if (frame)
        KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND;
      return r;
    }

    default:
      return r;
  }
}


KosEvaluatedTypeObject KosDemandCallableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.demand",
    .tp_basicsize = sizeof(KosDemandObject),
    .tp_itemsize = 0,
    .tp_dealloc = demand_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC
        | Py_TPFLAGS_HAVE_VECTORCALL | Py_TPFLAGS_METHOD_DESCRIPTOR,
    .tp_vectorcall_offset = KosDemand_VECTORCALL_OFFSET,
    .tp_doc = "Callable adapter that runs its wrapped callable on the demand path",
    .tp_traverse = demand_traverse,
    .tp_clear = demand_clear,
    .tp_members = demand_members,
    .tp_getset = demand_getset,
    .tp_repr = demand_repr,
    .tp_call = PyVectorcall_Call,
    .tp_descr_get = demand_descr_get,
    .tp_new = demand_new,
    .tp_init = demand_init,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = demand_kos_vinstall,
    .kc_pycall = NULL,
    .kc_pyvectorcall = demand_vectorcall,
  },
};


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
  PyObject *result = Koskari_DemandSync(callable, call_args, kwargs);
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
  if (call_kw && ! PyDict_CheckExact(call_kw)) {
    /* a dict subclass that relays its reads, such as a module
       namespace, is copied into a plain dict the callee reads raw */
    PyObject *fixed = PyDict_New();
    if (! fixed || PyDict_Update(fixed, call_kw) < 0) {
      Py_XDECREF(fixed);
      return NULL;
    }
    PyObject *res = Koskari_DemandSync(callable, call_args, fixed);
    Py_DECREF(fixed);
    return res;
  }
  return Koskari_DemandSync(callable, call_args, call_kw);
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

  return 0;
}


int koskari_demand_clear(PyObject *mod) {

  KoskariSyncViolation = NULL;
  (void) mod;
  return 0;
}


// The end.
