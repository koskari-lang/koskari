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
   koskari/lib/iterate.c

   Python-surface adapters over the KosIter suite: the shared async
   stepper (anext/asend/athrow/aclose awaitable) and the shared sync
   DemandGenerator, generic over any KosIter provider.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


static PyObject *iterate_str_send = NULL;
static PyObject *iterate_str_name = NULL;


/* === Shared helpers === */


/**
   Parse and normalize throw-method arguments (typ[, val[, tb]]) into an
   owned (type, exception-instance) pair shaped for the evaluator's
   throw ingress. Accepts an exception class with optional value, or a
   bare exception instance. format carries the method name for error
   messages, e.g. "O|OO:athrow".
*/
static int normalize_throw_args(
    PyObject *args,
    const char *format,
    PyObject **typ_out,
    PyObject **exc_out) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;

  if (! PyArg_ParseTuple(args, format, &typ, &val, &tb))
    return -1;

  if (PyExceptionClass_Check(typ)) {
    Py_INCREF(typ);
    Py_XINCREF(val);
    Py_XINCREF(tb);
    PyErr_NormalizeException(&typ, &val, &tb);
    Py_XDECREF(tb);
    if (! typ)
      return -1;
    if (! val) {
      val = typ;
      Py_INCREF(val);
    }
    *typ_out = typ;
    *exc_out = val;
    return 0;
  }

  if (PyExceptionInstance_Check(typ)) {
    if (val && val != Py_None) {
      PyErr_SetString(PyExc_TypeError,
                      "instance exception may not have a separate value");
      return -1;
    }
    Py_INCREF(typ);
    *exc_out = typ;
    *typ_out = (PyObject *) Py_TYPE(typ);
    Py_INCREF(*typ_out);
    return 0;
  }

  PyErr_SetString(PyExc_TypeError,
                  "exceptions must be classes or instances deriving "
                  "from BaseException");
  return -1;
}


/**
   Provider display name for reprs: its __name__ when it has one, else
   NULL (no error left pending)
*/
static PyObject *iterate_display_name(PyObject *provider) {

  PyObject *name = PyObject_GetAttr(provider, iterate_str_name);
  if (! name) {
    PyErr_Clear();
    return NULL;
  }
  if (! PyUnicode_Check(name)) {
    Py_DECREF(name);
    return NULL;
  }
  return name;
}


/**
   Acquire the drive context: the provider's durable ki_pyctx when it
   has one (context-level state persists across steps there), else a
   lazily created adapter-owned host. *owned reports which, so release
   paths free only adapter-owned contexts.
*/
static int iterate_acquire_host(
    KosIterSlots *islots,
    PyObject *provider,
    KosContext **host,
    int *owned) {

  if (*host)
    return 0;

  if (islots->ki_pyctx) {
    KosContext *ctx = islots->ki_pyctx(provider);
    if (ctx) {
      *host = ctx;
      *owned = 0;
      return 0;
    }
  }

  *host = KosContext_New(NULL);
  if (! *host)
    return -1;
  *owned = 1;
  return 0;
}


static KosIterStepResult iterate_suite_step(
    KosContext **host,
    int *host_owned,
    KosIterSlots *islots,
    PyObject *provider,
    PyObject *send_val,
    PyObject **out) {

  if (iterate_acquire_host(islots, provider, host, host_owned) < 0)
    return KOS_ITER_ERROR;

  {
    KosEvalState es;
    KosIterStepResult r;

    KosEvalState_Init(&es, *host);
    KosEvalState_ThawFromContext(&es);
    r = islots->ki_step(&es, provider, send_val, out);
    KosEvalState_FreezeToContext(&es);
    return r;
  }
}


/* === KosIterStepper === */


/**
   The awaitable returned by the anext/asend/athrow/aclose adapters.
   Each drive steps the provider through its suite, hosting grafted
   frames on the stepper's own context and rendering outcomes into the
   Python coroutine protocol.
*/
typedef struct {
  PyObject_HEAD
  PyObject *provider;     /* KosIter provider; NULL once spent */
  KosContext *host;       /* drive context (see iterate_acquire_host) */
  int host_owned;         /* host is adapter-owned; free on release */
  PyObject *send_value;   /* value to send into the provider */
  int is_throw;           /* first drive delivers the throw pair */
  int closing;            /* aclose result mapping at terminal outcomes */
  PyObject *throw_typ;
  PyObject *throw_exc;
  int awaiting;           /* mid-step await in flight: this stepper owns
                             the provider's busy mark and resumes by
                             driving host->py_await_iter */
  int parked;             /* park-flavor provider awaiting: resume
                             re-enters ki_step with the awaited value */
  PyObject *park_iter;    /* __await__ iterator of the parked awaitable */
} KosIterStepper;


static PyTypeObject KosIterStepperType;


/**
   Mid-await abandonment: the live chain sits on the stepper's host, so
   the provider's own close cannot reach it. Drive GeneratorExit through
   the host so finally/with cleanup runs, exception-fenced (callable
   from dealloc mid-unwind); every outcome of the drive is swallowed.
   Marks the provider finished-exhausted and clears the awaiting mark.
*/
static void stepper_abandon_drive(KosIterStepper *st) {

  PyObject *fence_typ, *fence_val, *fence_tb;
  KosIterSlots *islots;

  PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);

  {
    PyObject *it = KosContext_PY_AWAIT_ITER(st->host);
    if (it) {
      PyObject *r = PyObject_CallMethod(it, "close", NULL);
      Py_XDECREF(r);
      PyErr_Clear();
      Py_CLEAR(KosContext_PY_AWAIT_ITER(st->host));
    }
  }

  {
    PyObject *exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
    if (exc) {
      KosOpResult res = koskari_eval(st->host, NULL, PyExc_GeneratorExit,
                                     exc);
      Py_DECREF(exc);
      Py_XDECREF(res.val);
    }
    PyErr_Clear();
  }

  if (st->provider && KosIter_CheckSlots(st->provider, &islots))
    islots->ki_finish(st->provider, 1);
  st->awaiting = 0;

  PyErr_Restore(fence_typ, fence_val, fence_tb);
}


static void stepper_release_state(KosIterStepper *st) {

  if (st->awaiting && st->host)
    stepper_abandon_drive(st);

  if (st->parked && st->provider) {
    /* Abandoned provider park: close the in-flight awaitable and
       release the provider's busy mark. */
    KosIterSlots *islots;
    PyObject *ftyp, *fval, *ftb;
    PyErr_Fetch(&ftyp, &fval, &ftb);
    if (st->park_iter) {
      PyObject *r = PyObject_CallMethod(st->park_iter, "close", NULL);
      Py_XDECREF(r);
      PyErr_Clear();
    }
    if (KosIter_CheckSlots(st->provider, &islots))
      islots->ki_finish(st->provider, 0);
    st->parked = 0;
    PyErr_Restore(ftyp, fval, ftb);
  }

  if (st->host) {
    if (st->host_owned)
      KosContext_Free(st->host);
    st->host = NULL;
    st->host_owned = 0;
  }

  Py_CLEAR(st->provider);
  Py_CLEAR(st->send_value);
  Py_CLEAR(st->throw_typ);
  Py_CLEAR(st->throw_exc);
  Py_CLEAR(st->park_iter);
  st->parked = 0;
}


static void stepper_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);
  stepper_release_state((KosIterStepper *) self);
  Py_TYPE(self)->tp_free(self);
}


static int stepper_traverse(PyObject *self, visitproc visit, void *arg) {

  KosIterStepper *st = (KosIterStepper *) self;

  Py_VISIT(st->provider);
  Py_VISIT(st->send_value);
  Py_VISIT(st->throw_typ);
  Py_VISIT(st->throw_exc);
  Py_VISIT(st->park_iter);

  if (st->host && st->host_owned)
    return KosContext_Traverse(st->host, visit, arg);

  return 0;
}


static int stepper_clear(PyObject *self) {

  stepper_release_state((KosIterStepper *) self);
  return 0;
}


static PyObject *stepper_await(PyObject *self) {

  Py_INCREF(self);
  return self;
}


/**
   Terminal result mapping shared by the eval-driven and step-shortcut
   completions: exhaustion becomes StopIteration for throw/close
   flavored drives and StopAsyncIteration (with payload rules) for
   send-flavored ones. payload is consumed.
*/
static PyObject *stepper_finish_exhausted(
    KosIterStepper *st,
    PyObject *payload,
    int quiet_throw) {

  if (st->closing || quiet_throw || ! payload || payload == Py_None) {
    Py_XDECREF(payload);
    if (st->closing || quiet_throw)
      PyErr_SetNone(PyExc_StopIteration);
    else
      PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  {
    PyObject *exc = koskari_StopAsyncIteration_FromValue(payload);
    Py_DECREF(payload);
    if (exc) {
      PyErr_SetObject((PyObject *) Py_TYPE(exc), exc);
      Py_DECREF(exc);
    } else if (! PyErr_Occurred()) {
      PyErr_SetNone(PyExc_StopAsyncIteration);
    }
  }
  return NULL;
}


/**
   Complete an __anext__ step: the yielded element wraps in a 1-tuple
   StopIteration so PyErr_SetObject does not unpack args. val consumed.
*/
static PyObject *stepper_finish_yield(PyObject *val) {

  PyObject *args = PyTuple_Pack(1, val);
  Py_XDECREF(val);
  if (! args)
    return NULL;
  PyErr_SetObject(PyExc_StopIteration, args);
  Py_DECREF(args);
  return NULL;
}


/**
   Drive a park-flavor provider: ki_step/ki_throw complete immediately
   or park on an awaitable; the stepper eager-steps each awaitable,
   feeding completions back into ki_step until a terminal outcome or a
   true suspension (the yielded item goes to the event loop and the
   next send resumes the parked iterator). send_val, throw_typ, and
   throw_exc are owned and consumed.
*/
static PyObject *stepper_park_drive(
    KosIterStepper *st,
    KosIterSlots *islots,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  PyObject *resume_val = NULL;   /* owned; value feeding into ki_step */
  int have_resume = 0;

  /* Resume a parked awaitable with the sent value or the thrown
     exception. */
  if (st->parked && st->park_iter) {
    PyObject *step;

    if (throw_exc) {
      PyObject *throw_meth = PyObject_GetAttrString(st->park_iter, "throw");
      if (throw_meth) {
        step = PyObject_CallOneArg(throw_meth, throw_exc);
        Py_DECREF(throw_meth);
      } else {
        PyErr_Clear();
        step = NULL;
        PyErr_SetObject(throw_typ, throw_exc);
      }
    } else {
      step = PyObject_CallMethodObjArgs(
          st->park_iter, iterate_str_send,
          send_val ? send_val : Py_None, NULL);
    }
    Py_XDECREF(send_val);
    send_val = NULL;

    if (step) {
      /* still awaiting */
      Py_XDECREF(throw_typ);
      Py_XDECREF(throw_exc);
      return step;
    }

    Py_CLEAR(st->park_iter);
    st->parked = 0;

    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      if (PyGen_FetchStopIterationValue(&resume_val) < 0) {
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        islots->ki_finish(st->provider, 0);
        return NULL;
      }
      if (! resume_val) {
        resume_val = Py_None;
        Py_INCREF(resume_val);
      }
      have_resume = 1;
      Py_CLEAR(throw_typ);
      Py_CLEAR(throw_exc);
      PyErr_Clear();
    } else {
      /* The awaitable failed: deliver the exception through ki_throw
         below. */
      PyObject *tb = NULL;
      Py_XDECREF(throw_typ);
      Py_XDECREF(throw_exc);
      PyErr_Fetch(&throw_typ, &throw_exc, &tb);
      PyErr_NormalizeException(&throw_typ, &throw_exc, &tb);
      if (throw_exc && tb)
        PyException_SetTraceback(throw_exc, tb);
      Py_XDECREF(tb);
    }
  }

  for (;;) {
    PyObject *out = NULL;
    KosIterStepResult r;

    if (throw_exc) {
      r = islots->ki_throw(NULL, st->provider, throw_typ, throw_exc, &out);
      Py_CLEAR(throw_typ);
      Py_CLEAR(throw_exc);
    } else if (have_resume) {
      if (! islots->ki_resume) {
        Py_CLEAR(resume_val);
        PyErr_SetString(PyExc_SystemError,
                        "provider has no resume entry");
        return NULL;
      }
      r = islots->ki_resume(NULL, st->provider, resume_val, &out);
      Py_CLEAR(resume_val);
      have_resume = 0;
    } else {
      r = islots->ki_step(NULL, st->provider, send_val, &out);
      Py_CLEAR(send_val);
    }

    switch (r) {
      case KOS_ITER_VALUE:
        return stepper_finish_yield(out);

      case KOS_ITER_EXHAUSTED:
        return stepper_finish_exhausted(st, out, st->is_throw);

      case KOS_ITER_ERROR:
        return NULL;

      case KOS_ITER_AWAIT: {
        /* Eager-step the awaitable: immediate completion loops back
           into ki_step; a true suspension parks. */
        PyObject *it = PyObject_GetAwaitable(out);
        Py_DECREF(out);
        if (! it) {
          islots->ki_finish(st->provider, 0);
          return NULL;
        }
        {
          PyObject *step = PyObject_CallMethodObjArgs(
              it, iterate_str_send, Py_None, NULL);
          if (step) {
            /* suspended: park and hand the item to the loop */
            st->park_iter = it;
            st->parked = 1;
            return step;
          }
          if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
            if (PyGen_FetchStopIterationValue(&resume_val) < 0) {
              Py_DECREF(it);
              islots->ki_finish(st->provider, 0);
              return NULL;
            }
            if (! resume_val) {
              resume_val = Py_None;
              Py_INCREF(resume_val);
            }
            have_resume = 1;
            Py_DECREF(it);
            PyErr_Clear();
            continue;
          }
          /* the awaitable failed immediately: mediate through ki_throw */
          {
            PyObject *tb = NULL;
            Py_DECREF(it);
            PyErr_Fetch(&throw_typ, &throw_exc, &tb);
            PyErr_NormalizeException(&throw_typ, &throw_exc, &tb);
            if (throw_exc && tb)
              PyException_SetTraceback(throw_exc, tb);
            Py_XDECREF(tb);
            continue;
          }
        }
      }

      case KOS_ITER_INSTALLED:
      default:
        Py_XDECREF(out);
        PyErr_SetString(PyExc_SystemError,
                        "park-flavor provider returned INSTALLED");
        return NULL;
    }
  }
}


static PyObject *stepper_iternext(PyObject *self) {

  KosIterStepper *st = (KosIterStepper *) self;
  KosIterSlots *islots;

  if (! st->provider) {
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot reuse already awaited __anext__()/asend()");
    return NULL;
  }

  if (! KosIter_CheckSlots(st->provider, &islots)) {
    PyErr_SetString(PyExc_TypeError, "stepper provider lost its iter suite");
    return NULL;
  }

  PyObject *send_val = st->send_value;
  PyObject *throw_typ = st->throw_typ;
  PyObject *throw_exc = st->throw_exc;
  st->send_value = NULL;
  st->throw_typ = NULL;
  st->throw_exc = NULL;

  /* Park-flavor providers (no durable ki_pyctx: the step parks on
     awaitables instead of grafting frames) run the re-entry contract
     here: drive the parked __await__ iterator, feed completions back
     into ki_step, and eager-step fresh awaitables the same way. */
  if (! islots->ki_pyctx || ! islots->ki_pyctx(st->provider))
    return stepper_park_drive(st, islots, send_val, throw_typ, throw_exc);

  /* Resume-from-await: this stepper owns driving the in-flight
     __await__ iterator (the coroutine_send WAITING_PY contract). The
     step stays parked until the awaitable completes; only then is eval
     re-entered, with the result as send_val. An exception from the
     awaitable falls through as a primed throw so it is delivered at
     the await point inside eval. Either way the chain is live on the
     host mid-await: eval is re-entered directly, never through
     ki_step. */
  int resume_await = st->awaiting;

  if (st->awaiting && ! throw_typ) {
    PyObject *it = st->host ? KosContext_PY_AWAIT_ITER(st->host) : NULL;
    if (it) {
      PyObject *step = PyObject_CallMethodObjArgs(
          it, iterate_str_send, send_val ? send_val : Py_None, NULL);
      Py_XDECREF(send_val);
      send_val = NULL;

      if (step)
        /* still awaiting: hand the next item to the loop */
        return step;

      if (! PyErr_ExceptionMatches(PyExc_StopIteration)) {
        /* the awaitable failed: deliver the exception into the
           provider so its handlers see it (raise-at-await shape) */
        PyObject *tb = NULL;
        PyErr_Fetch(&throw_typ, &throw_exc, &tb);
        PyErr_NormalizeException(&throw_typ, &throw_exc, &tb);
        if (throw_exc && tb)
          PyException_SetTraceback(throw_exc, tb);
        Py_XDECREF(tb);
        Py_CLEAR(KosContext_PY_AWAIT_ITER(st->host));
        st->awaiting = 0;
      } else {
        /* awaitable completed: its value becomes the await result */
        PyObject *await_result = NULL;
        if (PyGen_FetchStopIterationValue(&await_result) < 0) {
          islots->ki_finish(st->provider, 0);
          st->awaiting = 0;
          return NULL;
        }
        if (! await_result) {
          await_result = Py_None;
          Py_INCREF(await_result);
        }
        Py_CLEAR(KosContext_PY_AWAIT_ITER(st->host));
        st->awaiting = 0;
        send_val = await_result;
      }
    } else {
      /* stale flag with no iterator in flight */
      st->awaiting = 0;
    }
  }

  /* Attach the provider's work onto the host. AWAIT is unreachable
     until a non-frame provider exists; the map lands the re-entry
     machinery with it. */
  if (! resume_await) {
    PyObject *out = NULL;

    switch (iterate_suite_step(&st->host, &st->host_owned, islots,
                               st->provider, send_val, &out)) {
      case KOS_ITER_INSTALLED:
        break;

      case KOS_ITER_VALUE:
        Py_XDECREF(send_val);
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        return stepper_finish_yield(out);

      case KOS_ITER_EXHAUSTED:
        /* Already-finished shortcut: a throw-flavored drive completes
           quietly (CPython parity), a send-flavored one raises
           StopAsyncIteration. */
        Py_XDECREF(send_val);
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        return stepper_finish_exhausted(st, out, st->is_throw);

      case KOS_ITER_AWAIT:
        Py_XDECREF(out);
        Py_XDECREF(send_val);
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        PyErr_SetString(PyExc_SystemError,
                        "stepper: unsupported KosIter AWAIT result");
        return NULL;

      case KOS_ITER_ERROR:
      default:
        Py_XDECREF(send_val);
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        return NULL;
    }
  }

  /* Drive the host; send/throw deliver through the entry ingress. */
  KosOpResult res = koskari_eval(st->host, send_val, throw_typ, throw_exc);

  Py_XDECREF(send_val);
  Py_XDECREF(throw_typ);
  Py_XDECREF(throw_exc);

  if (res.kind == KOS_RES_AWAIT) {
    /* Mid-step await: the provider stays busy; this stepper resumes it
       by driving the host's await iterator above. */
    st->awaiting = 1;
    return res.val;  /* transfer ownership to caller */
  }

  st->awaiting = 0;

  switch (res.kind) {
    case KOS_RES_YIELD:
      if (st->closing) {
        /* The body caught GeneratorExit and yielded */
        islots->ki_finish(st->provider, 1);
        Py_XDECREF(res.val);
        PyErr_SetString(PyExc_RuntimeError,
                        "async generator ignored GeneratorExit");
        return NULL;
      }
      islots->ki_finish(st->provider, 0);
      return stepper_finish_yield(res.val);

    case KOS_RES_RETURN:
      islots->ki_finish(st->provider, 1);
      return stepper_finish_exhausted(st, res.val, 0);

    case KOS_RES_RAISE:
    case KOS_RES_FATAL:
      {
        PyObject *ftyp, *fval, *ftb;
        PyErr_Fetch(&ftyp, &fval, &ftb);
        islots->ki_finish(st->provider, 1);
        PyErr_Restore(ftyp, fval, ftb);
      }
      /* Closing drive: GeneratorExit escaping the body is the success
         shape -- awaiting aclose yields None. Anything else
         propagates. */
      if (st->closing && PyErr_Occurred()
          && PyErr_ExceptionMatches(PyExc_GeneratorExit)) {
        PyErr_Clear();
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
      }
      if (! PyErr_Occurred())
        PyErr_SetNone(PyExc_StopAsyncIteration);
      return NULL;

    default:
      islots->ki_finish(st->provider, 0);
      PyErr_SetString(PyExc_RuntimeError, "unexpected KosOpResult kind");
      return NULL;
  }
}


static PyObject *stepper_send(PyObject *self, PyObject *value) {

  KosIterStepper *st = (KosIterStepper *) self;

  Py_XINCREF(value);
  Py_CLEAR(st->send_value);
  st->send_value = value;
  st->is_throw = 0;
  Py_CLEAR(st->throw_typ);
  Py_CLEAR(st->throw_exc);

  return stepper_iternext(self);
}


static PyObject *stepper_throw(PyObject *self, PyObject *args) {

  KosIterStepper *st = (KosIterStepper *) self;

  PyObject *typ = NULL, *exc = NULL;
  if (normalize_throw_args(args, "O|OO:throw", &typ, &exc) < 0)
    return NULL;

  st->is_throw = 1;
  Py_CLEAR(st->send_value);
  Py_CLEAR(st->throw_typ);
  Py_CLEAR(st->throw_exc);
  st->throw_typ = typ;
  st->throw_exc = exc;

  return stepper_iternext(self);
}


static PyObject *stepper_close(PyObject *self) {

  KosIterStepper *st = (KosIterStepper *) self;

  /* Close cascades are terminal (loop shutdown, task destruction, GC
     of an abandoned chain), so closing a step's awaitable closes the
     provider too -- CPython 3.13+ semantics, applied uniformly. A
     stepper abandoned mid-await drives GeneratorExit through its host
     (the live chain sits there); otherwise the provider's own close
     runs its teardown (a provider another stepper is actively driving
     declines internally). */
  if (st->awaiting && st->host) {
    stepper_abandon_drive(st);

  } else if (st->provider) {
    KosIterSlots *islots;
    if (KosIter_CheckSlots(st->provider, &islots)) {
      PyObject *ftyp, *fval, *ftb;
      PyErr_Fetch(&ftyp, &fval, &ftb);
      (void) islots->ki_close(st->provider);
      PyErr_Clear();
      PyErr_Restore(ftyp, fval, ftb);
    }
  }

  stepper_release_state(st);
  Py_RETURN_NONE;
}


static PyMethodDef stepper_methods[] = {
  { "send", (PyCFunction) stepper_send, METH_O,
    "Send a value" },

  { "throw", (PyCFunction) stepper_throw, METH_VARARGS,
    "Throw an exception" },

  { "close", (PyCFunction) stepper_close, METH_NOARGS,
    "Close" },

  { NULL, NULL, 0, NULL },
};


static PyAsyncMethods stepper_async_methods = {
  .am_await = stepper_await,
};


static PyObject *stepper_repr(PyObject *self) {

  return PyUnicode_FromFormat("<async_generator_asend at %p>", self);
}


static PyTypeObject KosIterStepperType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.async_generator_asend",
  .tp_basicsize = sizeof(KosIterStepper),
  .tp_itemsize = 0,
  .tp_dealloc = stepper_dealloc,
  .tp_repr = stepper_repr,
  .tp_as_async = &stepper_async_methods,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "KosIter provider step awaitable",
  .tp_traverse = stepper_traverse,
  .tp_clear = stepper_clear,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = stepper_iternext,
  .tp_methods = stepper_methods,
};


/**
   Build a stepper primed to send or throw on its first drive.
   throw_typ / throw_exc are optional borrowed references; when given,
   is_throw is set and the first drive delivers them through the
   evaluator's throw ingress. closing selects the aclose result mapping
   at terminal outcomes.
*/
static PyObject *stepper_new_ex(
    PyObject *provider,
    PyObject *send_value,
    PyObject *throw_typ,
    PyObject *throw_exc,
    int closing) {

  KosIterStepper *st = PyObject_GC_New(KosIterStepper, &KosIterStepperType);
  if (! st)
    return NULL;

  Py_INCREF(provider);
  st->provider = provider;
  st->host = NULL;
  st->host_owned = 0;

  Py_XINCREF(send_value);
  st->send_value = send_value;

  st->is_throw = (throw_typ != NULL);
  st->closing = closing;
  Py_XINCREF(throw_typ);
  st->throw_typ = throw_typ;
  Py_XINCREF(throw_exc);
  st->throw_exc = throw_exc;

  st->awaiting = 0;
  st->parked = 0;
  st->park_iter = NULL;

  PyObject_GC_Track((PyObject *) st);
  return (PyObject *) st;
}


/* === KosIterDemandGen === */


/**
   The sync generator surface over a KosIter provider: tp_iter fills
   for demand-path for-loops. Every drive forces KOS_FRAME_DEMAND onto
   the grafted frame, so real suspension denies in-eval as
   KoskariSyncViolation; YIELD surfaces as iteration.
*/
typedef struct {
  PyObject_HEAD
  PyObject *provider;      /* KosIter provider; NULL once closed */
  KosContext *host;        /* drive context (see iterate_acquire_host) */
  int host_owned;          /* host is adapter-owned; free on release */
  PyObject *display_name;  /* provider __name__ for repr; may be NULL */
} KosIterDemandGen;


static PyTypeObject KosIterDemandGenType;


/**
   Drop the view without closing the provider: dealloc and GC-clear of
   a DemandGenerator abandon this view, but the underlying provider may
   still be referenced and iterable elsewhere (e.g. iter(agen) used
   once, agen driven on afterward).
*/
static void demandgen_drop_state(KosIterDemandGen *gen) {

  Py_CLEAR(gen->provider);

  if (gen->host) {
    if (gen->host_owned)
      KosContext_Free(gen->host);
    gen->host = NULL;
    gen->host_owned = 0;
  }
}


static void demandgen_release_state(KosIterDemandGen *gen) {

  if (gen->provider) {
    KosIterSlots *islots;
    if (KosIter_CheckSlots(gen->provider, &islots)) {
      /* Runs from dealloc paths where an exception may be pending
         (e.g. inside a CPython slot that just raised); fence it so the
         close's own failures are swallowed without touching it. */
      PyObject *ftyp, *fval, *ftb;
      PyErr_Fetch(&ftyp, &fval, &ftb);
      (void) islots->ki_close(gen->provider);
      PyErr_Clear();
      PyErr_Restore(ftyp, fval, ftb);
    }
    Py_CLEAR(gen->provider);
  }

  if (gen->host) {
    if (gen->host_owned)
      KosContext_Free(gen->host);
    gen->host = NULL;
    gen->host_owned = 0;
  }
}


static void demandgen_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosIterDemandGen *gen = (KosIterDemandGen *) self;

  demandgen_drop_state(gen);
  Py_CLEAR(gen->display_name);

  Py_TYPE(self)->tp_free(self);
}


static int demandgen_traverse(PyObject *self, visitproc visit, void *arg) {

  KosIterDemandGen *gen = (KosIterDemandGen *) self;

  Py_VISIT(gen->provider);
  Py_VISIT(gen->display_name);

  if (gen->host && gen->host_owned)
    return KosContext_Traverse(gen->host, visit, arg);

  return 0;
}


static int demandgen_clear(PyObject *self) {

  KosIterDemandGen *gen = (KosIterDemandGen *) self;

  demandgen_drop_state(gen);
  Py_CLEAR(gen->display_name);

  return 0;
}


/**
   Demand rendering of a park awaitable: eager-step it; on immediate
   completion store the value. On suspension inject KoskariSyncViolation
   via throw -- the awaitable may catch it and complete -- and deny if
   it stays suspended. awaitable consumed.

   :return: 0 with *out_value set (new ref), -1 with PyErr set
*/
static int demand_eager_await(PyObject *awaitable, PyObject **out_value) {

  PyObject *it = PyObject_GetAwaitable(awaitable);
  Py_DECREF(awaitable);
  if (! it)
    return -1;

  PyObject *step = PyObject_CallMethodObjArgs(
      it, iterate_str_send, Py_None, NULL);

  if (! step) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration)) {
      Py_DECREF(it);
      return -1;
    }
    PyObject *value = NULL;
    if (PyGen_FetchStopIterationValue(&value) < 0) {
      Py_DECREF(it);
      return -1;
    }
    Py_DECREF(it);
    *out_value = value ? value : (Py_INCREF(Py_None), Py_None);
    return 0;
  }

  /* Suspended: inject the violation; the callee may catch and return. */
  Py_DECREF(step);
  {
    PyObject *exc;
    kos_demand_set_sync_violation(
        "async suspension is not allowed in demand generator");
    PyErr_Fetch(&exc, &step, NULL);  /* reuse step slot for value */
    PyObject *vio = step ? step : exc;
    PyObject *throw_meth = PyObject_GetAttrString(it, "throw");
    PyObject *res = NULL;
    if (throw_meth) {
      res = PyObject_CallOneArg(throw_meth, vio);
      Py_DECREF(throw_meth);
    }
    Py_XDECREF(exc);
    Py_XDECREF(step);

    if (! res) {
      Py_DECREF(it);
      if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
        PyObject *value = NULL;
        if (PyGen_FetchStopIterationValue(&value) < 0)
          return -1;
        *out_value = value ? value : (Py_INCREF(Py_None), Py_None);
        return 0;
      }
      return -1;
    }

    /* still suspended after the violation: deny */
    Py_DECREF(res);
    {
      PyObject *r = PyObject_CallMethod(it, "close", NULL);
      Py_XDECREF(r);
      PyErr_Clear();
    }
    Py_DECREF(it);
    kos_demand_set_sync_violation(
        "async suspension is not allowed in demand generator");
    return -1;
  }
}


static PyObject *demandgen_step(
    KosIterDemandGen *gen,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosIterSlots *islots;

  if (! gen->provider) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  if (! KosIter_CheckSlots(gen->provider, &islots)) {
    PyErr_SetString(PyExc_TypeError,
                    "DemandGenerator provider lost its iter suite");
    return NULL;
  }

  /* Park-flavor provider: complete every park by demand-eager
     stepping, feeding results back into ki_step. */
  if (! islots->ki_pyctx || ! islots->ki_pyctx(gen->provider)) {
    PyObject *resume_val = NULL;
    int have_resume = 0;

    /* Uniform ownership: the caller's borrowed throw pair is held for
       the loop; fetched pairs from failed awaitables replace it. */
    Py_XINCREF(throw_typ);
    Py_XINCREF(throw_exc);

    for (;;) {
      PyObject *out = NULL;
      KosIterStepResult r;

      if (throw_exc) {
        r = islots->ki_throw(NULL, gen->provider, throw_typ, throw_exc,
                             &out);
        Py_CLEAR(throw_typ);
        Py_CLEAR(throw_exc);
      } else if (have_resume) {
        if (! islots->ki_resume) {
          Py_CLEAR(resume_val);
          PyErr_SetString(PyExc_SystemError,
                          "provider has no resume entry");
          return NULL;
        }
        r = islots->ki_resume(NULL, gen->provider, resume_val, &out);
        Py_CLEAR(resume_val);
        have_resume = 0;
      } else {
        r = islots->ki_step(NULL, gen->provider, send_val, &out);
        send_val = NULL;
      }

      switch (r) {
        case KOS_ITER_VALUE:
          return out;

        case KOS_ITER_EXHAUSTED: {
          demandgen_release_state(gen);
          PyObject *err = PyObject_CallOneArg(
              PyExc_StopIteration, out ? out : Py_None);
          Py_XDECREF(out);
          if (err) {
            PyErr_SetObject(PyExc_StopIteration, err);
            Py_DECREF(err);
          } else {
            PyErr_SetNone(PyExc_StopIteration);
          }
          return NULL;
        }

        case KOS_ITER_AWAIT:
          if (demand_eager_await(out, &resume_val) < 0) {
            /* The awaitable failed: mediate through ki_throw so the
               provider converts its own protocol exceptions (a
               mid-fetch StopAsyncIteration becomes map exhaustion). */
            PyObject *tb = NULL;
            PyErr_Fetch(&throw_typ, &throw_exc, &tb);
            PyErr_NormalizeException(&throw_typ, &throw_exc, &tb);
            if (throw_exc && tb)
              PyException_SetTraceback(throw_exc, tb);
            Py_XDECREF(tb);
            continue;
          }
          have_resume = 1;
          continue;

        case KOS_ITER_ERROR:
          return NULL;

        case KOS_ITER_INSTALLED:
        default:
          Py_XDECREF(out);
          PyErr_SetString(PyExc_SystemError,
                          "park-flavor provider returned INSTALLED");
          return NULL;
      }
    }
  }

  {
    PyObject *out = NULL;

    switch (iterate_suite_step(&gen->host, &gen->host_owned, islots,
                               gen->provider, send_val, &out)) {
      case KOS_ITER_INSTALLED:
        break;

      case KOS_ITER_VALUE:
        return out;

      case KOS_ITER_EXHAUSTED: {
        demandgen_release_state(gen);
        PyObject *err = PyObject_CallOneArg(
            PyExc_StopIteration, out ? out : Py_None);
        Py_XDECREF(out);
        if (err) {
          PyErr_SetObject(PyExc_StopIteration, err);
          Py_DECREF(err);
        } else {
          PyErr_SetNone(PyExc_StopIteration);
        }
        return NULL;
      }

      case KOS_ITER_AWAIT:
        Py_XDECREF(out);
        kos_demand_set_sync_violation(
            "async suspension is not allowed in demand generator");
        return NULL;

      case KOS_ITER_ERROR:
      default:
        return NULL;
    }
  }

  /* Demand intent, embodied: force KOS_FRAME_DEMAND onto the grafted
     frame, so AWAIT denies in-eval as KoskariSyncViolation --
     catchable by the body for sync fallback; uncaught, it unwinds and
     exits as KOS_RES_RAISE. YIELD is untouched: it surfaces as
     iteration. */
  {
    KosDynFrame *frame = KosContext_FRAME_TOP(gen->host);
    if (frame)
      KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND;
  }

  {
    KosOpResult ev = koskari_eval(gen->host, send_val, throw_typ, throw_exc);
    KosIterSlots *fin;

    switch (ev.kind) {
      case KOS_RES_YIELD:
        if (KosIter_CheckSlots(gen->provider, &fin))
          fin->ki_finish(gen->provider, 0);
        return ev.val;

      case KOS_RES_RETURN: {
        if (KosIter_CheckSlots(gen->provider, &fin))
          fin->ki_finish(gen->provider, 1);
        demandgen_release_state(gen);
        PyObject *payload = ev.val;
        PyObject *err = PyObject_CallOneArg(
            PyExc_StopIteration, payload ? payload : Py_None);
        Py_XDECREF(payload);
        if (err) {
          PyErr_SetObject(PyExc_StopIteration, err);
          Py_DECREF(err);
        } else {
          PyErr_SetNone(PyExc_StopIteration);
        }
        return NULL;
      }

      case KOS_RES_RAISE:
      case KOS_RES_FATAL: {
        /* The teardown and host free below run Python code; fence the
           surfaced exception across them. */
        PyObject *ftyp, *fval, *ftb;
        PyErr_Fetch(&ftyp, &fval, &ftb);
        if (KosIter_CheckSlots(gen->provider, &fin))
          fin->ki_finish(gen->provider, 1);
        demandgen_release_state(gen);
        PyErr_Restore(ftyp, fval, ftb);
        return NULL;
      }

      case KOS_RES_AWAIT:
        /* Unreachable: the frame carries KOS_FRAME_DEMAND (forced
           above), so eval_event_await converts AWAIT to an in-eval
           KoskariSyncViolation before it can surface here. Kept
           defensively. */
        if (KosIter_CheckSlots(gen->provider, &fin))
          fin->ki_finish(gen->provider, 1);
        demandgen_release_state(gen);
        Py_XDECREF(ev.val);
        kos_demand_set_sync_violation(
            "async suspension is not allowed in demand generator");
        return NULL;

      default:
        demandgen_release_state(gen);
        PyErr_SetString(PyExc_RuntimeError, "unexpected KosOpResult kind");
        return NULL;
    }
  }
}


static PyObject *demandgen_iternext(PyObject *self) {

  return demandgen_step((KosIterDemandGen *) self, NULL, NULL, NULL);
}


static PyObject *demandgen_send(PyObject *self, PyObject *value) {

  if (! value)
    value = Py_None;
  Py_INCREF(value);
  PyObject *result = demandgen_step(
      (KosIterDemandGen *) self, value, NULL, NULL);
  Py_DECREF(value);
  return result;
}


static PyObject *demandgen_throw(PyObject *self, PyObject *args) {

  KosIterDemandGen *gen = (KosIterDemandGen *) self;

  PyObject *typ = NULL, *exc = NULL;
  if (normalize_throw_args(args, "O|OO:throw", &typ, &exc) < 0)
    return NULL;

  if (! gen->provider) {
    Py_DECREF(typ);
    Py_DECREF(exc);
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  PyObject *result = demandgen_step(gen, NULL, typ, exc);
  Py_DECREF(typ);
  Py_DECREF(exc);
  return result;
}


static PyObject *demandgen_close(PyObject *self, PyObject *ignored) {

  KosIterDemandGen *gen = (KosIterDemandGen *) self;

  (void) ignored;

  if (! gen->provider)
    Py_RETURN_NONE;

  /* Generator close semantics: throw GeneratorExit through the drive.
     A body that yields in response has ignored the close; a return or
     an escaping GeneratorExit/StopIteration is the success shape. */
  {
    PyObject *exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
    if (! exc)
      return NULL;

    PyObject *step = demandgen_step(gen, NULL, PyExc_GeneratorExit, exc);
    Py_DECREF(exc);

    if (step) {
      Py_DECREF(step);
      demandgen_release_state(gen);
      PyErr_SetString(PyExc_RuntimeError,
                      "generator ignored GeneratorExit");
      return NULL;
    }

    if (PyErr_ExceptionMatches(PyExc_GeneratorExit)
        || PyErr_ExceptionMatches(PyExc_StopIteration))
      PyErr_Clear();
    else if (PyErr_Occurred()) {
      demandgen_release_state(gen);
      return NULL;
    }
  }

  demandgen_release_state(gen);
  Py_RETURN_NONE;
}


static PyObject *demandgen_iter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *demandgen_repr(PyObject *self) {

  KosIterDemandGen *gen = (KosIterDemandGen *) self;

  if (gen->display_name)
    return PyUnicode_FromFormat(
        "<generator object %U at %p>", gen->display_name, (void *) self);
  return PyUnicode_FromFormat("<generator object at %p>", (void *) self);
}


static PyMethodDef demandgen_methods[] = {
  { "send", (PyCFunction) demandgen_send, METH_O,
    "Send a value into the generator" },

  { "throw", (PyCFunction) demandgen_throw, METH_VARARGS,
    "Throw an exception into the generator" },

  { "close", (PyCFunction) demandgen_close, METH_NOARGS,
    "Close the generator" },

  { NULL, NULL, 0, NULL },
};


static PyTypeObject KosIterDemandGenType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.DemandGenerator",
  .tp_basicsize = sizeof(KosIterDemandGen),
  .tp_itemsize = 0,
  .tp_dealloc = demandgen_dealloc,
  .tp_repr = demandgen_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Sync generator surface over a KosIter provider on the "
            "demand path",
  .tp_traverse = demandgen_traverse,
  .tp_clear = demandgen_clear,
  .tp_iter = demandgen_iter,
  .tp_iternext = demandgen_iternext,
  .tp_methods = demandgen_methods,
};


/* === Adapters === */


PyObject *KosIterAdapt_Anext(PyObject *self) {

  return stepper_new_ex(self, NULL, NULL, NULL, 0);
}


PyObject *KosIterAdapt_Asend(PyObject *self, PyObject *value) {

  return stepper_new_ex(self, value, NULL, NULL, 0);
}


PyObject *KosIterAdapt_Athrow(PyObject *self, PyObject *args) {

  PyObject *typ = NULL, *exc = NULL;

  if (normalize_throw_args(args, "O|OO:athrow", &typ, &exc) < 0)
    return NULL;

  PyObject *st = stepper_new_ex(self, NULL, typ, exc, 0);
  Py_DECREF(typ);
  Py_DECREF(exc);
  return st;
}


PyObject *KosIterAdapt_Aclose(PyObject *self, PyObject *ignored) {

  (void) ignored;

  PyObject *exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
  if (! exc)
    return NULL;

  PyObject *st = stepper_new_ex(self, NULL, PyExc_GeneratorExit, exc, 1);
  Py_DECREF(exc);
  return st;
}


PyObject *KosIterAdapt_DemandIter(PyObject *self) {

  KosIterSlots *islots;

  if (! KosIter_CheckSlots(self, &islots)) {
    PyErr_SetString(PyExc_TypeError, "not a KosIter provider");
    return NULL;
  }

  KosIterDemandGen *gen = PyObject_GC_New(
      KosIterDemandGen, &KosIterDemandGenType);
  if (! gen)
    return NULL;

  Py_INCREF(self);
  gen->provider = self;
  gen->host = NULL;
  gen->host_owned = 0;
  gen->display_name = iterate_display_name(self);

  PyObject_GC_Track((PyObject *) gen);
  return (PyObject *) gen;
}


/* === Unit lifecycle === */


int koskari_iterate_init(PyObject *mod) {

  (void) mod;

  STR_CONST(iterate_str_send, "send");
  STR_CONST(iterate_str_name, "__name__");

  if (PyType_Ready(&KosIterStepperType) ||
      PyType_Ready(&KosIterDemandGenType))
    return -1;

  return 0;
}


int koskari_iterate_clear(PyObject *mod) {

  Py_CLEAR(iterate_str_send);
  Py_CLEAR(iterate_str_name);

  (void) mod;
  return 0;
}


// The end.
