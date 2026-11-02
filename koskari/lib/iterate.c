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
   stepper (anext/asend/athrow/aclose awaitable) and KosPyIter, the
   CPython iterator, generic over any KosIter provider.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


static PyObject *iterate_str_name = NULL;


/* === Shared helpers === */


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
   Acquire the context the step runs on: the provider's durable
   ki_pyctx when its suite has the slot (context-level state persists
   across steps there), else a lazily created adapter-owned context.
   A suite with the slot is stepped on that context or not at all: a
   NULL return is the provider's failure, left set, and the step fails
   with it. Either way the adapter holds a reference of its own, so a
   provider cleared out from under a mid-step adapter cannot take the
   context with it. *owned reports which kind was taken: only an
   adapter-owned context is recycled or dropped between steps; a
   durable one keeps the provider's lifecycle.

   :return: 0, or -1 with the error set
*/
static int iterate_acquire_context(
    KosIterSlots *islots,
    PyObject *provider,
    KosContext **ctx_slot,
    uint8_t *owned) {

  if (*ctx_slot)
    return 0;

  if (islots->ki_pyctx) {
    KosContext *ctx = islots->ki_pyctx(provider);
    if (! ctx)
      return -1;
    Py_INCREF(ctx);
    *ctx_slot = ctx;
    *owned = 0;
    return 0;
  }

  *ctx_slot = KosContext_New(NULL, kos_tls_current());
  if (! *ctx_slot)
    return -1;
  *owned = 1;
  return 0;
}


/**
   Step the provider under a drive on the adapter's context. The
   provider's busy check (ki_busy) runs before the drive begins: the
   context may be the provider's durable one, shared with a drive in
   flight through another adapter, and beginning a drive on it would
   replace that drive's breaker, recursion limit and terminal flag,
   and ending one would flush registers into its frame and move its
   staged delivery.
*/
static KosIterStepResult iterate_suite_step(
    KosContext **ctx_slot,
    uint8_t *ctx_owned,
    KosIterSlots *islots,
    PyObject *provider,
    PyObject *send_val,
    PyObject **out) {

  if (iterate_acquire_context(islots, provider, ctx_slot, ctx_owned) < 0)
    return KOS_ITER_ERROR;

  if (islots->ki_busy && islots->ki_busy(provider))
    return KOS_ITER_ERROR;

  {
    KosIterStepResult r;

    KosContext_EvalEnter(*ctx_slot, NULL);
    r = islots->ki_step(*ctx_slot, provider, send_val, out);
    KosContext_EvalExit(*ctx_slot);
    return r;
  }
}


/**
   Throw into the provider under a drive on the adapter's context, with
   the busy check ahead of the drive for the reason iterate_suite_step
   gives
*/
static KosIterStepResult iterate_suite_throw(
    KosContext **ctx_slot,
    uint8_t *ctx_owned,
    KosIterSlots *islots,
    PyObject *provider,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  if (iterate_acquire_context(islots, provider, ctx_slot, ctx_owned) < 0)
    return KOS_ITER_ERROR;

  if (islots->ki_busy && islots->ki_busy(provider))
    return KOS_ITER_ERROR;

  {
    KosIterStepResult r;

    KosContext_EvalEnter(*ctx_slot, NULL);
    r = islots->ki_throw(*ctx_slot, provider, throw_typ, throw_exc, out);
    KosContext_EvalExit(*ctx_slot);
    return r;
  }
}


/**
   Make the drive context reusable after a completion at its root
   (KosContext_Recycle), so the next step reuses the same context
   instead of freeing and reallocating one per element. An owned context
   the recycle cannot account for is freed instead. A durable provider
   context is recycled only when its suite declares it stateless
   between steps (ki_pyctx_recyclable); otherwise it is left alone --
   its lifecycle belongs to the provider.
*/
static void iterate_reset_context(
    KosIterSlots *islots,
    KosContext **ctx_slot,
    uint8_t *owned) {

  KosContext *ctx = *ctx_slot;

  if (! ctx)
    return;

  if (! *owned) {
    if (islots->ki_pyctx_recyclable)
      (void) KosContext_Recycle(ctx);
    return;
  }

  if (KosContext_Recycle(ctx) < 0) {
    Py_CLEAR(*ctx_slot);
    *owned = 0;
  }
}


/* === KosIterStepper === */


/**
   The awaitable returned by the anext/asend/athrow/aclose adapters.
   Each drive steps the provider through its suite, running grafted
   frames on the stepper's own context and rendering outcomes into the
   Python coroutine protocol.
*/
typedef struct {
  PyObject_HEAD
  PyObject *provider;     /* KosIter provider; NULL once spent */
  KosIterSlots *islots;   /* provider's suite; resolved once (static) */
  KosContext *ctx;        /* drive context, a reference of our own
                             (see iterate_acquire_context) */
  PyObject *send_value;   /* value to send into the provider */
  PyObject *throw_typ;
  PyObject *throw_exc;

  /* the yes/no state, a byte each, after the pointers: the object
     stays within one cache line. TODO: is_throw, closing, awaiting,
     running and started describe one lifecycle (primed, running,
     awaiting, spent) and belong in a state enumeration or a flags
     byte; ctx_owned and drive_mode are the two independent bits. */
  uint8_t ctx_owned;      /* ctx is adapter-owned: recycled between
                             steps, not the provider's durable one */
  uint8_t is_throw;       /* first drive delivers the throw pair */
  uint8_t closing;        /* aclose result mapping at terminal outcomes */
  uint8_t awaiting;       /* mid-step await in flight: this stepper owns
                             the provider's busy mark and resumes by
                             driving ctx->py_await_iter */
  uint8_t drive_mode;     /* in-flight work on ctx: 0 provider-flavor
                             (yield is the element, return is
                             exhaustion), 1 value graft (the frame
                             return is the element) */
  uint8_t running;        /* a drive is on the C stack: a re-entrant
                             drive or close from the body is refused */
  uint8_t started;        /* the first drive has run: a send now feeds
                             the step in flight, not the primed state */
  PyObject **ret;         /* an am_send caller's result slot while its
                             drive runs, else NULL: a yielded element
                             is stored there instead of being wrapped
                             in a StopIteration */
} KosIterStepper;


static PyTypeObject KosIterStepperType;


/**
   Mid-await abandonment: the live chain sits on the stepper's context,
   so the provider's own close cannot reach it. Drive GeneratorExit
   through that context so finally/with cleanup runs, exception-fenced
   (callable from dealloc mid-unwind); the drive's own outcome is
   swallowed, and an error from closing the pending await iterator is
   reported as unraisable.
   Marks the provider finished-exhausted and clears the awaiting mark.
*/
static void stepper_abandon_drive(KosIterStepper *st) {

  PyObject *fence_typ, *fence_val, *fence_tb;
  KosIterSlots *islots;

  PyErr_Fetch(&fence_typ, &fence_val, &fence_tb);

  {
    PyObject *it = KosContext_PY_AWAIT_ITER(st->ctx);
    if (it) {
      kos_iter_close(it);
      Py_CLEAR(KosContext_PY_AWAIT_ITER(st->ctx));
    }
  }

  /* A context whose chain is gone has nothing to close: its clear or
     recycle released the chain already, and the stepper is being
     dropped from inside that release. */
  if (KosContext_DYN_TOP(st->ctx)) {
    PyObject *exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
    if (exc) {
      KosOpResult res = koskari_eval(st->ctx, NULL, NULL, PyExc_GeneratorExit,
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

  if (st->awaiting && st->ctx)
    stepper_abandon_drive(st);

  Py_CLEAR(st->ctx);
  st->ctx_owned = 0;

  Py_CLEAR(st->provider);
  Py_CLEAR(st->send_value);
  Py_CLEAR(st->throw_typ);
  Py_CLEAR(st->throw_exc);
  st->drive_mode = 0;
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
  Py_VISIT(st->ctx);

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
   Complete an __anext__ step. An am_send caller takes the yielded
   element through its result slot, reported as PYGEN_RETURN with no
   exception. For a __next__ caller the element, None included, is
   carried by a StopIteration instance built here and raised by
   kos_raise_stop_instance, as CPython's async_gen_unwrap_value raises
   every yielded element through _PyGen_SetStopIterationValue. val
   consumed.

   Passing a bare value instead, to let CPython defer building the
   exception instance, was measured and does not help: the await
   machinery normalizes it downstream, so the instance is built either
   way.
*/
static PyObject *stepper_finish_yield(KosIterStepper *st, PyObject *val) {

  if (st->ret) {
    *st->ret = val;
    return NULL;
  }

  {
    PyObject *exc = PyObject_CallOneArg(PyExc_StopIteration,
                                        val ? val : Py_None);
    Py_XDECREF(val);
    if (exc)
      kos_raise_stop_instance(exc);
    return NULL;
  }
}


/**
   Resume the context's in-flight __await__ iterator with the sent value.
   Outcomes: 1 with *still set -- the awaitable yielded again (hand
   *still to the event loop); 0 -- completed or failed, awaiting
   cleared, with the completion in *hsend or the failure in the
   httyp/htexc pair for the eval ingress; -1 -- hard failure (PyErr
   set, provider finished). send_val is consumed.
*/
static int stepper_resume_await(
    KosIterStepper *st,
    PyObject *send_val,
    PyObject **still,
    PyObject **hsend,
    PyObject **httyp,
    PyObject **htexc) {

  PyObject *it = KosContext_PY_AWAIT_ITER(st->ctx);
  PyObject *step;

  if (! it) {
    /* stale flag with no iterator in flight */
    st->awaiting = 0;
    Py_XDECREF(send_val);
    return 0;
  }

  {
    PySendResult r;

    /* the awaitable runs on the provider's behalf: its context is the
       active one for the step */
    kos_domain_push_scope(st->ctx);
    r = kos_iter_send(it, send_val ? send_val : Py_None, &step);
    kos_domain_pop_scope(st->ctx);
    Py_XDECREF(send_val);

    if (r == PYGEN_NEXT) {
      *still = step;
      return 1;
    }

    if (r == PYGEN_ERROR) {
      /* the awaitable failed: deliver the exception into the provider
         at the await point (raise-at-await shape) */
      PyObject *tb = NULL;
      PyErr_Fetch(httyp, htexc, &tb);
      PyErr_NormalizeException(httyp, htexc, &tb);
      if (*htexc && tb)
        PyException_SetTraceback(*htexc, tb);
      Py_XDECREF(tb);
      Py_CLEAR(KosContext_PY_AWAIT_ITER(st->ctx));
      st->awaiting = 0;
      return 0;
    }

    /* PYGEN_RETURN: the completion, owned, never NULL */
    Py_CLEAR(KosContext_PY_AWAIT_ITER(st->ctx));
    st->awaiting = 0;
    *hsend = step;
    return 0;
  }
}


/**
   Drive one step of the provider and render the outcome into the
   Python coroutine protocol. Suite entries run against the provider's
   durable context when its suite has one, else the adapter-owned context;
   installed work runs there via koskari_eval, and mid-work awaits are
   resumed by driving that context's in-flight await iterator.

   The two provider arrangements differ only at the marked points: a
   durable-context provider's suspended work receives sends and throws
   through the eval entry ingress and treats every root return as
   exhaustion, while a fresh-step provider receives throws through
   ki_throw, marks a value graft's root return as the element
   (drive_mode 1), and converts a root StopIteration to exhaustion.
   send_val, throw_typ, and throw_exc are owned and consumed.
*/
static PyObject *stepper_drive(
    KosIterStepper *st,
    KosIterSlots *islots,
    int durable,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc);


/**
   A mapped function's StopIteration truncates THIS drive -- never a
   fabricated None element through the coroutine surface -- but does
   not close the provider: builtins.map resumes from the next source
   element, and so does the sync surface, where the StopIteration
   rides the iteration protocol untouched. A thrown StopIteration
   (athrow) is not a truncation and propagates.

   :return: 1 when the pending error was a truncation, with *out set
            to the drive's result; 0 to leave the error to the caller
*/
static int stepper_take_truncation(
    KosIterStepper *st,
    KosIterSlots *islots,
    int durable,
    PyObject **out) {

  if (durable || st->is_throw || ! PyErr_Occurred()
      || ! PyErr_ExceptionMatches(PyExc_StopIteration))
    return 0;

  PyErr_Clear();
  islots->ki_finish(st->provider, 0);
  iterate_reset_context(islots, &st->ctx, &st->ctx_owned);
  *out = stepper_finish_exhausted(st, NULL, st->is_throw);
  return 1;
}


/**
   Map a pending exception onto the async-generator surface: a close
   that reached its GeneratorExit completes, and a StopIteration
   escaping into the await machinery would read as a clean return of
   its value, so the protocol wraps it with the cause preserved.
*/
static void stepper_map_pending_error(KosIterStepper *st) {

  if (st->closing && PyErr_Occurred()
      && PyErr_ExceptionMatches(PyExc_GeneratorExit)) {
    PyErr_Clear();
    PyErr_SetNone(PyExc_StopIteration);
    return;
  }

  if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_StopIteration))
    _PyErr_FormatFromCause(PyExc_RuntimeError,
                           "async generator raised StopIteration");
}


static PyObject *stepper_drive(
    KosIterStepper *st,
    KosIterSlots *islots,
    int durable,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  PyObject *hsend = NULL;        /* owned; eval ingress */
  PyObject *httyp = NULL;
  PyObject *htexc = NULL;
  int eval_pending = 0;          /* enter the eval without a suite step */

  if (st->awaiting && st->ctx) {
    /* Mid-work await: drive the context's in-flight awaitable; its
       completion re-enters the eval, its failure enters through
       the eval throw ingress. */
    if (! throw_typ) {
      PyObject *still = NULL;
      int rr = stepper_resume_await(
          st, send_val, &still, &hsend, &httyp, &htexc);
      send_val = NULL;
      if (rr > 0) {
        Py_XDECREF(throw_exc);
        return still;
      }
      if (rr < 0) {
        Py_XDECREF(throw_exc);
        return NULL;
      }
      eval_pending = 1;
    } else {
      /* throw into a mid-work await: eval ingress delivers it */
      st->awaiting = 0;
      httyp = throw_typ;
      htexc = throw_exc;
      throw_typ = NULL;
      throw_exc = NULL;
      Py_XDECREF(send_val);
      send_val = NULL;
      eval_pending = 1;
    }
  }

  if (! eval_pending) {
    PyObject *out = NULL;
    KosIterStepResult r;

    if (throw_exc && ! durable) {
      r = iterate_suite_throw(&st->ctx, &st->ctx_owned, islots,
                              st->provider, throw_typ, throw_exc, &out);
      Py_CLEAR(throw_typ);
      Py_CLEAR(throw_exc);
    } else {
      /* A durable provider's throw pair rides the eval ingress below,
         delivered at the suspended point inside its chain. */
      r = iterate_suite_step(&st->ctx, &st->ctx_owned, islots,
                             st->provider, send_val, &out);
    }

    switch (r) {
      case KOS_ITER_VALUE:
        Py_XDECREF(send_val);
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        return stepper_finish_yield(st, out);

      case KOS_ITER_EXHAUSTED:
        /* Already-finished shortcut: a throw-flavored drive completes
           quietly (CPython parity), a send-flavored one raises
           StopAsyncIteration. */
        Py_XDECREF(send_val);
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        return stepper_finish_exhausted(st, out, st->is_throw);

      case KOS_ITER_ERROR: {
        PyObject *truncated = NULL;

        Py_XDECREF(send_val);
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        if (stepper_take_truncation(st, islots, durable, &truncated))
          return truncated;
        stepper_map_pending_error(st);
        return NULL;
      }

      case KOS_ITER_INSTALLED:
        st->drive_mode = 0;
        break;

      case KOS_ITER_INSTALLED_VALUE:
        st->drive_mode = 1;
        break;

      default:
        Py_XDECREF(out);
        Py_XDECREF(send_val);
        Py_XDECREF(throw_typ);
        Py_XDECREF(throw_exc);
        PyErr_SetString(PyExc_SystemError,
                        "provider returned an unexpected step result");
        return NULL;
    }

    if (durable) {
      hsend = send_val;
      httyp = throw_typ;
      htexc = throw_exc;
      send_val = NULL;
      throw_typ = NULL;
      throw_exc = NULL;
    } else {
      Py_CLEAR(send_val);
    }
  }

  {
    KosOpResult res = koskari_eval(st->ctx, NULL, hsend, httyp, htexc);
    Py_CLEAR(hsend);
    Py_CLEAR(httyp);
    Py_CLEAR(htexc);

    if (res.kind == KOS_RES_AWAIT) {
      st->awaiting = 1;   /* drive_mode persists across the await */
      return res.val;
    }
    st->awaiting = 0;

    switch (res.kind) {
      case KOS_RES_RETURN:
        if (st->drive_mode == 1) {
          st->drive_mode = 0;
          islots->ki_finish(st->provider, 0);
          iterate_reset_context(islots, &st->ctx, &st->ctx_owned);
          return stepper_finish_yield(st, res.val);
        }
        islots->ki_finish(st->provider, 1);
        iterate_reset_context(islots, &st->ctx, &st->ctx_owned);
        return stepper_finish_exhausted(st, res.val, 0);

      case KOS_RES_YIELD:
        st->drive_mode = 0;
        if (st->closing) {
          islots->ki_finish(st->provider, 1);
          Py_XDECREF(res.val);
          PyErr_SetString(PyExc_RuntimeError,
                          "async generator ignored GeneratorExit");
          return NULL;
        }
        islots->ki_finish(st->provider, 0);
        return stepper_finish_yield(st, res.val);

      case KOS_RES_RAISE:
      case KOS_RES_FATAL:
      default: {
        PyObject *truncated = NULL;

        st->drive_mode = 0;
        /* Exhaustion surfacing at the context root: the provider's step
           ended its iteration. */
        if (stepper_take_truncation(st, islots, durable, &truncated))
          return truncated;

        {
          /* The teardown runs Python code; fence the surfaced
             exception across it. */
          PyObject *ftyp, *fval, *ftb;
          PyErr_Fetch(&ftyp, &fval, &ftb);
          islots->ki_finish(st->provider, durable ? 1 : 0);
          iterate_reset_context(islots, &st->ctx, &st->ctx_owned);
          PyErr_Restore(ftyp, fval, ftb);
        }

        stepper_map_pending_error(st);
        if (! PyErr_Occurred())
          PyErr_SetNone(PyExc_StopAsyncIteration);
        return NULL;
      }
    }
  }
}


static PyObject *stepper_iternext(PyObject *self) {

  KosIterStepper *st = (KosIterStepper *) self;
  KosIterSlots *islots = st->islots;

  if (! st->provider) {
    PyErr_SetString(PyExc_RuntimeError,
                    (st->is_throw || st->closing)
                    ? "cannot reuse already awaited aclose()/athrow()"
                    : "cannot reuse already awaited __anext__()/asend()");
    return NULL;
  }

  /* The body being driven can reach this stepper and drive it again;
     refused as CPython refuses a running generator, before anything
     the outer drive still uses could be released. */
  if (st->running) {
    PyErr_SetString(PyExc_ValueError, "generator already executing");
    return NULL;
  }

  {
    PyObject *send_val = st->send_value;
    PyObject *throw_typ = st->throw_typ;
    PyObject *throw_exc = st->throw_exc;
    PyObject *res;
    /* Durable here means generator-style: suspended state lives in
       the provider's context between steps. A provider whose durable
       context is recyclable is stateless between steps and keeps the
       fresh-step drive semantics (ki_throw ingress, StopIteration
       exhaustion, value-graft returns). A durable context that cannot
       be had fails the step with the provider's error. */
    int durable = 0;

    if (islots->ki_pyctx && ! islots->ki_pyctx_recyclable) {
      if (! islots->ki_pyctx(st->provider))
        return NULL;
      durable = 1;
    }

    st->send_value = NULL;
    st->throw_typ = NULL;
    st->throw_exc = NULL;

    st->running = 1;
    st->started = 1;
    res = stepper_drive(st, islots, durable, send_val, throw_typ,
                        throw_exc);
    st->running = 0;
    /* The step ended, in a value, exhaustion or an error: the
       awaitable is spent, and a second await meets the guard above. A
       mid-step await keeps the state for its resume. */
    if (! st->awaiting) {
      PyObject *ftyp, *fval, *ftb;

      PyErr_Fetch(&ftyp, &fval, &ftb);
      stepper_release_state(st);
      PyErr_Restore(ftyp, fval, ftb);
    }
    return res;
  }
}


/**
   The coroutine protocol's send. Before the first drive the stepper
   holds what asend, athrow or aclose primed it with, and the starting
   send(None) leaves that in place, as CPython's awaitables do: an
   asend takes a non-None value in place of its own, while an athrow
   or aclose refuses one. After the first drive a send feeds the await
   in flight.
*/
static PyObject *stepper_send(PyObject *self, PyObject *value) {

  KosIterStepper *st = (KosIterStepper *) self;

  /* A send from the body this stepper is driving: refused before the
     fields are written, since the drive has already taken what they
     held and a value left here would be the next resume's send. */
  if (st->running) {
    PyErr_SetString(PyExc_ValueError, "generator already executing");
    return NULL;
  }

  /* A spent awaitable refuses from the state it was primed with,
     before a send could overwrite it. */
  if (! st->provider)
    return stepper_iternext(self);

  if (! st->started) {
    if (st->is_throw) {
      if (value && value != Py_None) {
        PyErr_SetString(PyExc_RuntimeError,
                        "can't send non-None value to a just-started"
                        " coroutine");
        return NULL;
      }
      return stepper_iternext(self);
    }
    if (! value || value == Py_None)
      return stepper_iternext(self);
  }

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

  /* A throw from the body this stepper is driving: refused before the
     fields are written, as in stepper_send. */
  if (st->running) {
    PyErr_SetString(PyExc_ValueError, "generator already executing");
    return NULL;
  }

  if (KosExc_NormalizeThrowArgs(args, "O|OO:throw", &typ, &exc) < 0)
    return NULL;

  st->is_throw = 1;
  Py_CLEAR(st->send_value);
  Py_CLEAR(st->throw_typ);
  Py_CLEAR(st->throw_exc);
  st->throw_typ = typ;
  st->throw_exc = exc;

  return stepper_iternext(self);
}


#if PY_MINOR_VERSION >= 10
/**
   The send slot asyncio's task step and PyIter_Send use: the same
   drive as send, with a yielded element reported as a value through
   the result slot instead of a StopIteration.
*/
static PySendResult stepper_am_send(
    PyObject *self, PyObject *value, PyObject **result) {

  KosIterStepper *st = (KosIterStepper *) self;
  PyObject *ret = NULL;
  PyObject *res;

  st->ret = &ret;
  res = stepper_send(self, value);
  st->ret = NULL;
  if (res) {
    *result = res;
    return PYGEN_NEXT;
  }
  if (ret) {
    *result = ret;
    return PYGEN_RETURN;
  }

  /* A step that completed by raising StopIteration, as an athrow or
     aclose awaitable does, is a return to a send-slot caller: the
     slot's contract reports the value, never the exception, which a
     coroutine awaiting through this slot would otherwise turn into
     "coroutine raised StopIteration". */
  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    if (PyGen_FetchStopIterationValue(&ret) < 0) {
      *result = NULL;
      return PYGEN_ERROR;
    }
    *result = ret;
    return PYGEN_RETURN;
  }
  *result = NULL;
  return PYGEN_ERROR;
}
#endif


static PyObject *stepper_close(PyObject *self) {

  KosIterStepper *st = (KosIterStepper *) self;

  /* A close from the body this stepper is driving: refused before the
     state the outer drive still uses is released. */
  if (st->running) {
    PyErr_SetString(PyExc_ValueError, "generator already executing");
    return NULL;
  }

  /* Close cascades are terminal (loop shutdown, task destruction, GC
     of an abandoned chain), so closing a step's awaitable closes the
     provider too -- CPython 3.13+ semantics, applied uniformly. A
     stepper abandoned mid-await drives GeneratorExit through its context
     (the live chain sits there); otherwise the provider's own close
     runs its teardown (a provider another stepper is actively driving
     declines internally). */
  if (st->awaiting && st->ctx) {
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
#if PY_MINOR_VERSION >= 10
  .am_send = stepper_am_send,
#endif
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
  if (! KosIter_CheckSlots(provider, &st->islots)) {
    Py_DECREF(provider);
    PyObject_GC_Del(st);
    PyErr_SetString(PyExc_TypeError, "not a KosIter provider");
    return NULL;
  }
  st->ctx = NULL;
  st->ctx_owned = 0;
  st->running = 0;
  st->started = 0;
  st->ret = NULL;

  Py_XINCREF(send_value);
  st->send_value = send_value;

  st->is_throw = (throw_typ != NULL);
  st->closing = closing;
  Py_XINCREF(throw_typ);
  st->throw_typ = throw_typ;
  Py_XINCREF(throw_exc);
  st->throw_exc = throw_exc;

  st->awaiting = 0;
  st->drive_mode = 0;

  PyObject_GC_Track((PyObject *) st);
  return (PyObject *) st;
}


/* === KosPyIter === */


/**
   The CPython iterator over a KosIter provider, filling a provider's
   tp_iter. A drive from Python (next, send, throw, close) is a
   boundary drive: it forces KOS_FRAME_DEMAND onto the grafted frame,
   so real suspension denies in-eval as KoskariSyncViolation; YIELD
   surfaces as iteration. To the evaluator the adapter is its provider:
   its iter suite forwards, so a Koskari drive flattens and carries
   only its own invoking frame's demand state.
*/
typedef struct {
  PyObject_HEAD
  PyObject *provider;      /* KosIter provider; NULL once closed */
  KosIterSlots *islots;    /* provider's suite; resolved once (static) */
  KosContext *ctx;         /* drive context, a reference of our own
                              (see iterate_acquire_context) */
  PyObject *display_name;  /* provider __name__ for repr; may be NULL */
  uint8_t ctx_owned;       /* ctx is adapter-owned: recycled between
                              steps, not the provider's durable one */
  uint8_t running;         /* a step is on the C stack: a re-entrant
                              step or close from the body is refused */
} KosPyIter;


static KosEvaluatedTypeObject KosPyIterType;


/**
   Drop the view without closing the provider: dealloc and GC-clear of
   a KosPyIter abandon this view, but the underlying provider may
   still be referenced and iterable elsewhere (e.g. iter(agen) used
   once, agen driven on afterward).
*/
static void pyiter_drop_state(KosPyIter *gen) {

  Py_CLEAR(gen->provider);

  Py_CLEAR(gen->ctx);
  gen->ctx_owned = 0;
}


static void pyiter_release_state(KosPyIter *gen) {

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

  Py_CLEAR(gen->ctx);
  gen->ctx_owned = 0;
}


static void pyiter_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyIter *gen = (KosPyIter *) self;

  pyiter_drop_state(gen);
  Py_CLEAR(gen->display_name);

  Py_TYPE(self)->tp_free(self);
}


static int pyiter_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPyIter *gen = (KosPyIter *) self;

  Py_VISIT(gen->provider);
  Py_VISIT(gen->display_name);
  Py_VISIT(gen->ctx);

  return 0;
}


static int pyiter_clear(PyObject *self) {

  KosPyIter *gen = (KosPyIter *) self;

  pyiter_drop_state(gen);
  Py_CLEAR(gen->display_name);

  return 0;
}


/**
   Render exhaustion into the sync generator protocol: release the
   view and raise StopIteration carrying the payload. payload consumed
   (may be NULL).
*/
static PyObject *pyiter_finish_exhausted(
    KosPyIter *gen,
    PyObject *payload) {

  pyiter_release_state(gen);
  return kos_raise_stop_iteration(payload);
}


/**
   One demand-path step of the provider. Suite entries run against the
   provider's durable context when its suite has one, else the
   adapter-owned context; installed work runs there via koskari_eval under
   KOS_FRAME_DEMAND, so genuine suspension denies in-eval as
   KoskariSyncViolation. The provider arrangements differ at the same
   points as the async stepper: a durable-context provider receives
   sends and throws through the eval ingress, exhausts on every root
   return, and releases on error; a fresh-step provider receives
   throws through ki_throw, marks a value graft's root return as the
   element, converts a root StopIteration to exhaustion, and stays
   iterable after other errors. send_val and the throw pair are
   borrowed.
*/
static PyObject *pyiter_step_run(
    KosPyIter *gen,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  KosIterSlots *islots = gen->islots;
  int durable = 0;
  int drive_mode = 0;   /* 1: value graft (frame return is element) */

  /* a durable context that cannot be had fails the step with the
     provider's error */
  if (islots->ki_pyctx && ! islots->ki_pyctx_recyclable) {
    if (! islots->ki_pyctx(gen->provider))
      return NULL;
    durable = 1;
  }

  {
    PyObject *out = NULL;
    KosIterStepResult r;
    int thrown = (throw_exc != NULL);

    if (throw_exc && ! durable) {
      r = iterate_suite_throw(&gen->ctx, &gen->ctx_owned, islots,
                              gen->provider, throw_typ, throw_exc, &out);
    } else {
      /* A durable provider's throw pair rides the eval ingress below. */
      r = iterate_suite_step(&gen->ctx, &gen->ctx_owned, islots,
                             gen->provider, send_val, &out);
    }

    switch (r) {
      case KOS_ITER_VALUE:
        return out;

      case KOS_ITER_EXHAUSTED:
        if (thrown) {
          /* the provider was already finished: a throw into a finished
             generator raises what was thrown, not StopIteration */
          Py_XDECREF(out);
          pyiter_release_state(gen);
          PyErr_SetObject(throw_typ, throw_exc);
          return NULL;
        }
        return pyiter_finish_exhausted(gen, out);

      case KOS_ITER_ERROR:
        return NULL;

      case KOS_ITER_INSTALLED:
        drive_mode = 0;
        break;

      case KOS_ITER_INSTALLED_VALUE:
        drive_mode = 1;
        break;

      default:
        Py_XDECREF(out);
        PyErr_SetString(PyExc_SystemError,
                        "provider returned an unexpected step result");
        return NULL;
    }
  }

  /* Demand intent, embodied: force KOS_FRAME_DEMAND onto the
     installed frame, so AWAIT denies in-eval as KoskariSyncViolation
     -- catchable by the body for sync fallback; uncaught, it unwinds
     and exits as KOS_RES_RAISE. YIELD is untouched: it surfaces as
     iteration. */
  {
    KosDynFrame *frame = KosContext_FRAME(gen->ctx);
    if (frame)
      KosDynFrame_FLAGS(frame) |= KOS_FRAME_DEMAND;
  }

  {
    KosOpResult res = koskari_eval(gen->ctx, NULL,
                                   durable ? send_val : NULL,
                                   durable ? throw_typ : NULL,
                                   durable ? throw_exc : NULL);

    switch (res.kind) {
      case KOS_RES_YIELD:
        islots->ki_finish(gen->provider, 0);
        return res.val;

      case KOS_RES_RETURN:
        if (drive_mode == 1) {
          islots->ki_finish(gen->provider, 0);
          iterate_reset_context(islots, &gen->ctx, &gen->ctx_owned);
          return res.val;
        }
        islots->ki_finish(gen->provider, 1);
        iterate_reset_context(islots, &gen->ctx, &gen->ctx_owned);
        return pyiter_finish_exhausted(gen, res.val);

      case KOS_RES_AWAIT:
        /* Unreachable: the frame carries KOS_FRAME_DEMAND (forced
           above), so eval_event_await converts AWAIT to an in-eval
           KoskariSyncViolation before it can surface here. Kept
           defensively. */
        islots->ki_finish(gen->provider, 1);
        pyiter_release_state(gen);
        Py_XDECREF(res.val);
        kos_demand_set_sync_violation(
            "async suspension is not allowed in demand generator");
        return NULL;

      case KOS_RES_RAISE:
      case KOS_RES_FATAL:
      default:
        if (! durable && PyErr_Occurred()
            && PyErr_ExceptionMatches(PyExc_StopIteration)) {
          /* Exhaustion surfacing at the context root: mark the provider
             closed; StopIteration is already the sync protocol's end
             signal. */
          islots->ki_finish(gen->provider, 1);
          iterate_reset_context(islots, &gen->ctx, &gen->ctx_owned);
          pyiter_release_state(gen);
          return NULL;
        }
        {
          /* The teardown and context free below run Python code; fence
             the surfaced exception across them. */
          PyObject *ftyp, *fval, *ftb;
          PyErr_Fetch(&ftyp, &fval, &ftb);
          islots->ki_finish(gen->provider, durable ? 1 : 0);
          iterate_reset_context(islots, &gen->ctx, &gen->ctx_owned);
          if (durable)
            pyiter_release_state(gen);
          PyErr_Restore(ftyp, fval, ftb);
        }
        return NULL;
    }
  }
}


/**
   The step entry: a finished adapter raises StopIteration, and a step
   from inside the step being run -- the body reaching its own adapter
   through next, send, throw or close -- is refused as CPython refuses
   a running generator, before an inner exhaustion or close could
   release the provider and context the outer step is still driving.
*/
static PyObject *pyiter_step(
    KosPyIter *gen,
    PyObject *send_val,
    PyObject *throw_typ,
    PyObject *throw_exc) {

  PyObject *res;

  if (! gen->provider) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  if (gen->running) {
    PyErr_SetString(PyExc_ValueError, "generator already executing");
    return NULL;
  }

  gen->running = 1;
  res = pyiter_step_run(gen, send_val, throw_typ, throw_exc);
  gen->running = 0;
  return res;
}


static PyObject *pyiter_iternext(PyObject *self) {

  return pyiter_step((KosPyIter *) self, NULL, NULL, NULL);
}


static PyObject *pyiter_send(PyObject *self, PyObject *value) {

  if (! value)
    value = Py_None;
  Py_INCREF(value);
  PyObject *result = pyiter_step(
      (KosPyIter *) self, value, NULL, NULL);
  Py_DECREF(value);
  return result;
}


static PyObject *pyiter_throw(PyObject *self, PyObject *args) {

  KosPyIter *gen = (KosPyIter *) self;

  PyObject *typ = NULL, *exc = NULL;
  if (KosExc_NormalizeThrowArgs(args, "O|OO:throw", &typ, &exc) < 0)
    return NULL;

  if (! gen->provider) {
    /* a throw into a finished generator raises what was thrown, as the
       generator protocol does; pyiter_ki_throw answers the same way */
    PyErr_SetObject(typ, exc);
    Py_DECREF(typ);
    Py_DECREF(exc);
    return NULL;
  }

  PyObject *result = pyiter_step(gen, NULL, typ, exc);
  Py_DECREF(typ);
  Py_DECREF(exc);
  return result;
}


static PyObject *pyiter_close(PyObject *self, PyObject *ignored) {

  KosPyIter *gen = (KosPyIter *) self;

  (void) ignored;

  if (! gen->provider)
    Py_RETURN_NONE;

  /* A close from the body being stepped: refused before the state the
     outer step still uses is released. */
  if (gen->running) {
    PyErr_SetString(PyExc_ValueError, "generator already executing");
    return NULL;
  }

  /* Generator close semantics: throw GeneratorExit through the drive.
     A body that yields in response has ignored the close; a return or
     an escaping GeneratorExit/StopIteration is the success shape. */
  {
    PyObject *exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
    if (! exc)
      return NULL;

    PyObject *step = pyiter_step(gen, NULL, PyExc_GeneratorExit, exc);
    Py_DECREF(exc);

    if (step) {
      Py_DECREF(step);
      pyiter_release_state(gen);
      PyErr_SetString(PyExc_RuntimeError,
                      "generator ignored GeneratorExit");
      return NULL;
    }

    if (PyErr_ExceptionMatches(PyExc_GeneratorExit)
        || PyErr_ExceptionMatches(PyExc_StopIteration))
      PyErr_Clear();
    else if (PyErr_Occurred()) {
      pyiter_release_state(gen);
      return NULL;
    }
  }

  pyiter_release_state(gen);
  Py_RETURN_NONE;
}


static PyObject *pyiter_iter(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *pyiter_repr(PyObject *self) {

  KosPyIter *gen = (KosPyIter *) self;

  if (gen->display_name)
    return PyUnicode_FromFormat(
        "<koskari iterator %U at %p>", gen->display_name, (void *) self);
  return PyUnicode_FromFormat("<koskari iterator at %p>", (void *) self);
}


static PyMethodDef pyiter_methods[] = {
  { "send", (PyCFunction) pyiter_send, METH_O,
    "Send a value into the generator" },

  { "throw", (PyCFunction) pyiter_throw, METH_VARARGS,
    "Throw an exception into the generator" },

  { "close", (PyCFunction) pyiter_close, METH_NOARGS,
    "Close the generator" },

  { NULL, NULL, 0, NULL },
};


/* The iter suite: the adapter is its provider to the evaluator. Each
   entry forwards to the provider's suite and adds nothing -- a
   Koskari drive through the adapter carries whatever demand state its
   own invoking frame carries, as a drive of the bare provider does. */


static KosIterStepResult pyiter_ki_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosPyIter *gen = (KosPyIter *) self;

  if (! gen->provider) {
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }
  return gen->islots->ki_step(ctx, gen->provider, send_val, out);
}


static KosIterStepResult pyiter_ki_throw(
    KosContext *ctx,
    PyObject *self,
    PyObject *throw_typ,
    PyObject *throw_exc,
    PyObject **out) {

  KosPyIter *gen = (KosPyIter *) self;

  if (! gen->provider) {
    PyErr_SetObject(throw_typ, throw_exc);
    return KOS_ITER_ERROR;
  }
  return gen->islots->ki_throw(ctx, gen->provider, throw_typ, throw_exc,
                               out);
}


static int pyiter_ki_close(PyObject *self) {

  KosPyIter *gen = (KosPyIter *) self;

  return gen->provider ? gen->islots->ki_close(gen->provider) : 0;
}


static int pyiter_ki_busy(PyObject *self) {

  KosPyIter *gen = (KosPyIter *) self;

  if (gen->provider && gen->islots->ki_busy)
    return gen->islots->ki_busy(gen->provider);
  return 0;
}


/**
   The request's finish applies the recyclable rule against this
   adapter's suite, which is not recyclable; apply the provider's own
   rule here so a wrapped provider finishes as a bare one does.
*/
static void pyiter_ki_finish(PyObject *self, int exhausted) {

  KosPyIter *gen = (KosPyIter *) self;

  if (gen->provider)
    kos_iter_finish(gen->provider, gen->islots, exhausted);
}


static KosEvaluatedTypeObject KosPyIterType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.Iterator",
    .tp_basicsize = sizeof(KosPyIter),
    .tp_itemsize = 0,
    .tp_dealloc = pyiter_dealloc,
    .tp_repr = pyiter_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "CPython iterator over a KosIter provider",
    .tp_traverse = pyiter_traverse,
    .tp_clear = pyiter_clear,
    .tp_iter = pyiter_iter,
    .tp_iternext = pyiter_iternext,
    .tp_methods = pyiter_methods,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = pyiter_ki_step,
    .ki_throw = pyiter_ki_throw,
    .ki_close = pyiter_ki_close,
    .ki_finish = pyiter_ki_finish,
    .ki_pyctx = NULL,
    .ki_busy = pyiter_ki_busy,
  },
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

  if (KosExc_NormalizeThrowArgs(args, "O|OO:athrow", &typ, &exc) < 0)
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


PyObject *KosIterAdapt_PyIter(PyObject *self) {

  KosIterSlots *islots;

  if (! KosIter_CheckSlots(self, &islots)) {
    PyErr_SetString(PyExc_TypeError, "not a KosIter provider");
    return NULL;
  }

  KosPyIter *gen = PyObject_GC_New(
      KosPyIter, KOS_EVALUATED_PYTYPE(KosPyIterType));
  if (! gen)
    return NULL;

  Py_INCREF(self);
  gen->provider = self;
  gen->islots = islots;
  gen->ctx = NULL;
  gen->ctx_owned = 0;
  gen->running = 0;
  gen->display_name = iterate_display_name(self);

  PyObject_GC_Track((PyObject *) gen);
  return (PyObject *) gen;
}


/* === Unit lifecycle === */


int koskari_iterate_init(PyObject *mod) {

  (void) mod;

  STR_CONST(iterate_str_name, "__name__");

  if (PyType_Ready(&KosIterStepperType) ||
      PyType_Ready(KOS_EVALUATED_PYTYPE(KosPyIterType)))
    return -1;

  return 0;
}


int koskari_iterate_clear(PyObject *mod) {

  Py_CLEAR(iterate_str_name);

  (void) mod;
  return 0;
}


// The end.
