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


/**
   Classify a value a koskari install or eval reports as settled. A
   plain value passes through; one that genuinely needs awaiting is
   eagerly stepped (and denied as a sync violation if it suspends)
   through the same machinery a plain Python callable's result already
   takes. KosEval_ResultIsValue is the gate rather than
   demand_step_python_result's own __await__ probe, so a Defer or a
   future -- both exempt from ambient await -- passes through as the
   value it is instead of being stepped.

   Steals value. Returns a new reference, or NULL with PyErr set.
*/
static PyObject *demand_finish_koskari_value(PyObject *value) {

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

  return KosNativeFrame_ReturnPark(es, value, NULL);
}


KOS_RESUME_KIND(demand_resume_kind, "demand",
                demand_resume, NULL, kos_resume_clone_shared);


/**
   Run a KosCallable to completion from the Python boundary: record the
   call on a fresh context, and koskari_eval installs it at entry
   through KosEvalState_CallInto from a code-less root frame marked
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
    PyObject *kwnames) {

  KosEntryCall call = {
    callable, args, nargs, kwnames, &demand_resume_kind, KOS_FRAME_DEMAND,
  };
  KosContext *ctx = KosContext_New(NULL);
  KosOpResult r;

  if (! ctx)
    return NULL;

  KosContext_ENTRY_CALL(ctx) = &call;
  r = koskari_eval(ctx, NULL, NULL, NULL);
  KosContext_ENTRY_CALL(ctx) = NULL;
  Py_DECREF(ctx);

  switch (r.kind) {
    case KOS_RES_RETURN:
      /* The returned value is classified like any other settled
         result: a function that returns an awaitable as data gets the
         same eager-step-or-deny treatment a plain Python callable's
         result gets, instead of leaking an unstepped awaitable out of
         the demand boundary. */
      return demand_finish_koskari_value(r.val);

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
   Python callable is called and an awaitable result eagerly stepped;
   a KosCallable runs through demand_run_koskari. args and kwnames are
   borrowed. Returns a new reference, or NULL with PyErr set.
*/
static PyObject *demand_sync_vector(
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames) {

  SAYF("[demand_sync] callable = %p", callable);

  if (! KosCallable_Check(callable)) {
    PyObject *result = PyObject_Vectorcall(callable, args, (size_t) nargs,
                                           kwnames);
    return result ? demand_step_python_result(result) : NULL;
  }

  return demand_run_koskari(callable, args, nargs, kwnames);
}


/**
   The demand core over a tuple and a dict, spread once to a vector. No
   buffer: this frame sits on the C stack of every boundary re-entry,
   and keyword calls here are rare enough to take the heap.
*/
static PyObject *demand_sync_impl(
    PyObject *callable,
    PyObject *args,
    PyObject *kwargs) {

  KosArgSpread spread;
  PyObject *result;

  if (KosArgSpread_Init(&spread, args, kwargs, NULL, 0) < 0)
    return NULL;

  result = demand_sync_vector(callable, spread.argv, spread.nargs,
                              spread.kwnames);
  KosArgSpread_Clear(&spread);
  return result;
}


int Koskari_DemandSyncEx(
    PyObject *callable,
    PyObject *args,
    PyObject *kwargs,
    PyObject **out_result) {

  *out_result = demand_sync_impl(callable, args, kwargs);
  return *out_result ? 0 : -1;
}


PyObject *Koskari_DemandSync(
    PyObject *callable,
    PyObject *args,
    PyObject *kwargs) {

  return demand_sync_impl(callable, args, kwargs);
}


int Koskari_DemandSyncVector(
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    PyObject **out_result) {

  *out_result = demand_sync_vector(callable, args, nargs, kwnames);
  return *out_result ? 0 : -1;
}


/* === KosDemand (wrapper) === */



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
   Evaluator install: call the inner through the call entry, whatever
   it is. A value the call completes in hand is finished as the demand
   boundary finishes one (a settled value passes, an awaitable is
   stepped and denied if it suspends); an installed callee's frame is
   marked KOS_FRAME_DEMAND so the in-eval deny guards fire under it.
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

  r = KosEvalState_CallResult(es, inner, args, nargs, kwnames, tail_call,
                              NULL, NULL);
  switch (r.kind) {
    case KOS_RES_RETURN: {
      PyObject *value = demand_finish_koskari_value(r.val);
      return value ? EVAL_RETURN(value) : EVAL_RAISE();
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
    .tp_call = KosCallAdapt_CallDemand,
    .tp_descr_get = demand_descr_get,
    .tp_new = demand_new,
    .tp_init = demand_init,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = demand_kos_vinstall,
    .kc_pycall = KosCallAdapt_CallDemand,
    .kc_pyvectorcall = KosCallAdapt_VectorDemand,
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
  Py_CLEAR(str_await);
  Py_CLEAR(str_send);
  Py_CLEAR(str_throw);
  (void) mod;
  return 0;
}


// The end.
