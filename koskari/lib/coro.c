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
*/


#include "lib.h"
#include "debug.h"


static PyObject *coro_str_throw = NULL;
static PyObject *coro_str_close = NULL;


/* === Run code as coroutine === */


PyObject *KosAsyncGen_NewFromCodeAndNs(KosCode *code, PyObject *ns) {

  KosDynGen *gen = kos_dyngen_new(NULL);
  if (! gen) {
    Py_DECREF(ns);
    return NULL;
  }

  /* the frame steals ns and releases it on failure */
  KosDynFrame *frame = kos_dynframe_new(
      (KosDynNode *) gen, code, ns);
  if (! frame) {
    kos_dyngen_free(gen);
    return NULL;
  }

  /* An unstarted generator rests in the same slot a suspended one does;
     started == 0 on the wrapper tells the first install to bootstrap
     rather than resume. No context: the splice from Koskari never needs
     one, and a drive from Python creates the wrapper's at that moment. */
  KosDynGen_ACTIVE(gen) = (KosDynNode *) frame;

  PyObject *agen = KosPyAsyncGen_New(NULL);
  if (! agen) {
    kos_dyngen_free(gen);
    return NULL;
  }

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

  /* the bound call: nothing runs and no context exists until Python
     drives it, and a Koskari await takes the program without either */
  KosCoroutine *coro = PyObject_GC_New(KosCoroutine, &KosCoroutineType);
  if (! coro) {
    Py_DECREF(ns);
    return NULL;
  }

  coro->ctx = NULL;
  coro->state = KOS_CORO_NEW;
  coro->in_send = 0;
  Py_INCREF(code);
  coro->code = (KosCode *) code;
  coro->ns = ns;
  coro->domain = NULL;
  coro->param_refs = NULL;

  if (KosContext_Snapshot(&coro->domain, &coro->param_refs) < 0) {
    Py_DECREF(coro);
    return NULL;
  }

  PyObject_GC_Track((PyObject *) coro);
  return (PyObject *) coro;
}


/**
   Build the drive context from the bound call, for the first drive from
   Python: the root frame over the program and its namespace, and a
   context over that frame starting from the creation-time snapshot. A
   no-op once built. Returns -1 with an error set.
*/
static void coroutine_drop_record(KosCoroutine *coro);


static int coroutine_materialize(KosCoroutine *coro) {

  KosDynFrame *frame;
  KosContext *ctx;

  if (coro->ctx)
    return 0;
  if (! coro->code) {
    PyErr_SetString(PyExc_RuntimeError, "coroutine has no program");
    return -1;
  }

  frame = kos_dynframe_new(NULL, coro->code, coro->ns);  /* steals ns */
  coro->ns = NULL;
  if (! frame)
    goto fail;

  ctx = KosContext_NewFromSnapshot(frame, coro->domain, coro->param_refs);
  coro->domain = NULL;
  coro->param_refs = NULL;
  if (! ctx) {
    kos_dynframe_free(frame);
    goto fail;
  }

  Py_CLEAR(coro->code);
  coro->ctx = ctx;
  return 0;

 fail:
  /* half a record is no program: the coroutine is finished */
  coroutine_drop_record(coro);
  coro->state = KOS_CORO_DONE;
  return -1;
}


/**
   Drop the bound call of a coroutine closed before any drive: nothing
   has entered, so there is nothing to unwind.
*/
static void coroutine_drop_record(KosCoroutine *coro) {

  Py_CLEAR(coro->code);
  Py_CLEAR(coro->ns);
  Py_CLEAR(coro->domain);
  Py_CLEAR(coro->param_refs);
}


/* === KosCoroutine type === */


static PyObject *coroutine_close(PyObject *self);


/**
   Mark every frame on the suspended chain as a demand root, so the
   close that follows runs on the demand path: cleanup that completes
   synchronously runs, and cleanup that tries to suspend is refused
   in-eval with KoskariSyncViolation, which the remaining finally blocks
   still see. Child frames inherit the bit only at creation, and these
   frames already exist.
*/
static void coroutine_mark_demand(KosCoroutine *coro) {
  for (KosDynNode *node = KosContext_DYN_TOP(coro->ctx);
       node; node = KosDynNode_PARENT(node)) {
    if (KosDynNode_IS_FRAME(node))
      KosDynFrame_FLAGS(node) |= KOS_FRAME_DEMAND;
  }
}


/**
   Finalizer: a coroutine dropped while suspended is closed before its
   context is torn down, the way CPython closes an abandoned coroutine,
   so its finally blocks, with exits, and wind after thunks run.
   GeneratorExit is thrown at the suspension point and whatever
   propagates back out is swallowed here; anything other than the close
   itself is reported as unraisable. The forced free in dealloc follows
   regardless of how the close went. A coroutine never started has
   entered nothing, and a finished one has nothing left to unwind.
*/
static void coroutine_finalize(PyObject *self) {
  KosCoroutine *const coro = (KosCoroutine *) self;

  if (! coro->ctx || coro->in_send || ! coro_str_close)
    return;
  if (coro->state != KOS_CORO_WAITING_PY && coro->state != KOS_CORO_YIELDED)
    return;

  PyObject *typ, *val, *tb;
  PyErr_Fetch(&typ, &val, &tb);

  coroutine_mark_demand(coro);
  PyObject *result = coroutine_close(self);
  if (result)
    Py_DECREF(result);
  else
    PyErr_WriteUnraisable(self);

  PyErr_Restore(typ, val, tb);
}


static void coroutine_dealloc(PyObject *self) {

  /* run the finalizer first; it may resurrect the coroutine, in which
     case the dealloc is abandoned here and happens on the next drop */
  if (PyObject_CallFinalizerFromDealloc(self) < 0)
    return;

  PyObject_GC_UnTrack(self);

  KosCoroutine *const coro = (KosCoroutine *) self;

  Py_CLEAR(coro->ctx);
  coroutine_drop_record(coro);

  Py_TYPE(self)->tp_free(self);
}


static int coroutine_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosCoroutine *const coro = (KosCoroutine *) self;

  Py_VISIT(coro->ctx);
  Py_VISIT(coro->code);
  Py_VISIT(coro->ns);
  Py_VISIT(coro->domain);
  Py_VISIT(coro->param_refs);

  return 0;
}


static int coroutine_clear(PyObject *self) {

  KosCoroutine *const coro = (KosCoroutine *) self;

  Py_CLEAR(coro->ctx);
  coroutine_drop_record(coro);

  return 0;
}


static PyObject *coroutine_send(PyObject *self, PyObject *value);
static PyObject *coroutine_throw(PyObject *self, PyObject *args);


static PyObject *coroutine_await(PyObject *self) {

  Py_INCREF(self);
  return self;
}


static PyObject *coroutine_iternext(PyObject *self) {

  return coroutine_send(self, Py_None);
}


/**
   Settle a KosOpResult from driving the evaluator into the coroutine
   protocol's three outcomes, with the coroutine state updated to
   match: 0 for a return with *out the value (owned), 1 for a yield or
   an await with *out the payload (owned), -1 with an exception set and
   *out NULL. The two entry points report these their own way: the
   iterator protocol raises StopIteration for a return, the send slot
   hands the value back as PYGEN_RETURN.
*/
static int coroutine_settle(
    KosCoroutine *coro,
    KosOpResult res,
    PyObject **out) {

  *out = NULL;
  switch (res.kind) {
    case KOS_RES_RETURN:
      coro->state = KOS_CORO_DONE;
      *out = res.val ? res.val : Py_NewRef(Py_None);
      return 0;

    case KOS_RES_RAISE:
    case KOS_RES_FATAL:
      coro->state = KOS_CORO_DONE;
      return -1;

    case KOS_RES_YIELD:
      coro->state = KOS_CORO_YIELDED;
      *out = res.val;  /* transfer ownership to caller */
      return 1;

    case KOS_RES_AWAIT:
      if (! res.val) {
        PyErr_SetString(PyExc_RuntimeError,
                        "KOS_RES_AWAIT with NULL payload");
        coro->state = KOS_CORO_DONE;
        return -1;
      }
      /* Driver already stepped the awaitable in push_or_await and set
         ctx->py_await_iter. Just suspend and return the awaitable to
         the event loop. */
      coro->state = KOS_CORO_WAITING_PY;
      *out = res.val;  /* transfer ownership to caller (asyncio) */
      return 1;

    default:
      PyErr_SetString(PyExc_RuntimeError, "unexpected KosOpResult kind");
      coro->state = KOS_CORO_DONE;
      return -1;
  }
}


/**
   Report a settled step on the iterator protocol: the payload for a
   yield or await, StopIteration(value) for a return (built as a real
   instance so the value is not tuple-splat during normalization), NULL
   with the error set otherwise.
*/
static PyObject *coroutine_report(int settled, PyObject *out) {

  if (settled == 1)
    return out;
  if (settled == 0) {
    PyObject *err = PyObject_CallOneArg(PyExc_StopIteration, out);
    Py_DECREF(out);
    if (err) {
      PyErr_SetObject(PyExc_StopIteration, err);
      Py_DECREF(err);
    }
  }
  return NULL;
}


static PyObject *coroutine_result(KosCoroutine *coro, KosOpResult res) {
  PyObject *out;
  return coroutine_report(coroutine_settle(coro, res, &out), out);
}


/**
   The inner Python awaitable raised: clear the await iterator and
   re-enter the evaluator with the pending exception as a throw, so it
   is delivered at the await point and the frame's handler chain runs
   (mirror revive_await's EVAL_RAISE ingress).
*/
static KosOpResult coroutine_drive_await_error(
    KosCoroutine *coro,
    KosEvalState *coro_es) {

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_Fetch(&typ, &val, &tb);
  PyErr_NormalizeException(&typ, &val, &tb);
  if (val && tb)
    PyException_SetTraceback(val, tb);

  KosEvalState_PythonAwaitClearIter(coro_es);

  coro->in_send = 1;
  coro->state = KOS_CORO_RUNNING;
  KosOpResult res = koskari_eval(coro->ctx, NULL, typ, val ? val : typ);
  coro->in_send = 0;

  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);

  return res;
}


static PyObject *coroutine_raise_at_await(
    KosCoroutine *coro,
    KosEvalState *coro_es) {

  return coroutine_result(coro, coroutine_drive_await_error(coro, coro_es));
}


/**
   One step of the coroutine, shared by the send method, tp_iternext,
   and the send slot: the reuse and re-entry guards, the forward into a
   Python awaitable the coroutine is waiting on, the drive, and the
   settle. Returns coroutine_settle's code with *out as it describes.

   A waiting coroutine forwards through kos_iter_send, so an awaitable
   with a send slot (a generator, a coroutine, a future's iterator)
   completes without building a StopIteration; its return value is the
   send value for the drive that follows.
*/
static int coroutine_step(
    KosCoroutine *coro,
    PyObject *value,
    PyObject **out) {

  *out = NULL;

  /* Re-entrancy guard */
  if (coro->in_send) {
    PyErr_SetString(PyExc_RuntimeError,
                    "coroutine already being awaited");
    return -1;
  }

  /* Check state: a completed coroutine is single-shot, like CPython's */
  if (coro->state == KOS_CORO_DONE) {
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot reuse already awaited coroutine");
    return -1;
  }

  /* the first drive from Python builds the context; it takes no value,
     as for a CPython coroutine */
  if (coro->state == KOS_CORO_NEW) {
    if (value && value != Py_None) {
      PyErr_SetString(PyExc_TypeError,
                      "can't send non-None value to a"
                      " just-started coroutine");
      return -1;
    }
    if (coroutine_materialize(coro) < 0)
      return -1;
  }

  /* If suspended on yield, pass value as send_val */
  PyObject *send_val = NULL;
  if (coro->state == KOS_CORO_YIELDED) {
    send_val = (value != NULL) ? value : Py_None;
    Py_INCREF(send_val);
    coro->state = KOS_CORO_RUNNING;
  }

  /* If waiting on Python awaitable, forward to it. The iterator is held
     across the forward, and in_send is set so close, send and throw
     from inside the step are refused: either can otherwise clear the
     context's reference to the iterator while it runs. */
  if (coro->state == KOS_CORO_WAITING_PY) {
    KosEvalState coro_es;
    KosEvalState_Init(&coro_es, coro->ctx);
    PyObject *await_iter = KosEvalState_PythonAwaitIter(&coro_es);
    if (await_iter) {
      PyObject *await_result = NULL;
      Py_INCREF(await_iter);
      coro->in_send = 1;
      PySendResult step = kos_iter_send(await_iter, value ? value : Py_None,
                                        &await_result);
      coro->in_send = 0;
      Py_DECREF(await_iter);
      if (step == PYGEN_NEXT) {
        *out = await_result;   /* still yielding */
        return 1;
      }
      if (step == PYGEN_ERROR)
        return coroutine_settle(
            coro, coroutine_drive_await_error(coro, &coro_es), out);
      /* PYGEN_RETURN: await_result is the completion, owned, never NULL */
      /* Python awaitable completed: its value is the send value for
         the drive. await_result is uniformly owned; the XDECREF after
         the drive releases it. */
      KosEvalState_PythonAwaitClearIter(&coro_es);
      coro->state = KOS_CORO_RUNNING;
      send_val = await_result;
    }
  }

  coro->in_send = 1;
  coro->state = KOS_CORO_RUNNING;

  KosOpResult res = koskari_eval(coro->ctx, send_val, NULL, NULL);
  Py_XDECREF(send_val);

  coro->in_send = 0;

  SAYF("[coroutine_step] koskari_eval -> kind=%d payload=%R\n",
        (int) res.kind, res.val);

  return coroutine_settle(coro, res, out);
}


static PyObject *coroutine_send(PyObject *self, PyObject *value) {
  PyObject *out;
  return coroutine_report(
      coroutine_step((KosCoroutine *) self, value, &out), out);
}


#if PY_MINOR_VERSION >= 10
/**
   The send slot asyncio's task step uses: the same step, reported as
   a code and a value, so a return never builds a StopIteration.
*/
static PySendResult coroutine_am_send(
    PyObject *self, PyObject *value, PyObject **result) {

  int settled = coroutine_step((KosCoroutine *) self, value, result);
  if (settled == 1)
    return PYGEN_NEXT;
  return settled == 0 ? PYGEN_RETURN : PYGEN_ERROR;
}
#endif


/**
   Drive a throw of the normalized (class, instance) pair into the
   coroutine. Borrows both; the caller releases them.
*/
static PyObject *coroutine_throw_pair(
    KosCoroutine *coro,
    PyObject *typ,
    PyObject *exc) {

  /* Re-entrancy guard, before the WAITING_PY arm below touches the
     stored await iterator: this ends in koskari_eval on the same
     context a drive already holds. */
  if (coro->in_send) {
    PyErr_SetString(PyExc_RuntimeError,
                    "coroutine already being awaited");
    return NULL;
  }

  if (coro->state == KOS_CORO_DONE) {
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot reuse already awaited coroutine");
    return NULL;
  }

  /* a throw into an unstarted coroutine is delivered at its start */
  if (coro->state == KOS_CORO_NEW && coroutine_materialize(coro) < 0)
    return NULL;

  /* If yielded, throw will inject exception and run (no special case) */
  /* If waiting on Python awaitable, forward throw; the iterator is held
     and in_send set across it, as in coroutine_step */
  if (coro->state == KOS_CORO_WAITING_PY) {
    KosEvalState coro_es;
    KosEvalState_Init(&coro_es, coro->ctx);
    PyObject *await_iter = KosEvalState_PythonAwaitIter(&coro_es);
    if (await_iter) {
      Py_INCREF(await_iter);
      coro->in_send = 1;
      PyObject *result = PyObject_CallMethodObjArgs(
          await_iter, coro_str_throw, exc, NULL);
      coro->in_send = 0;
      Py_DECREF(await_iter);

      if (result) {
        return result;
      }

      if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
        /* await_result is uniformly owned; PythonResumeSet steals it. */
        PyObject *await_result = NULL;
        PyGen_FetchStopIterationValue(&await_result);
        if (! await_result) {
          await_result = Py_None;
          Py_INCREF(await_result);
        }
        KosEvalState_PythonAwaitClearIter(&coro_es);
        coro->state = KOS_CORO_RUNNING;
        KosEvalState_PythonResumeSet(&coro_es, await_result, KOS_RESUME_AWAIT);
        return coroutine_send((PyObject *) coro, Py_None);
      } else {
        return coroutine_raise_at_await(coro, &coro_es);
      }
    }
  }

  coro->in_send = 1;
  coro->state = KOS_CORO_RUNNING;
  KosOpResult res = koskari_eval(coro->ctx, NULL, typ, exc);
  coro->in_send = 0;

  return coroutine_result(coro, res);
}


static PyObject *coroutine_throw(PyObject *self, PyObject *args) {

  PyObject *typ = NULL, *exc = NULL;
  if (KosExc_NormalizeThrowArgs(args, "O|OO:throw", &typ, &exc) < 0)
    return NULL;

  PyObject *result = coroutine_throw_pair((KosCoroutine *) self, typ, exc);
  Py_DECREF(typ);
  Py_DECREF(exc);
  return result;
}


static PyObject *coroutine_close(PyObject *self) {

  KosCoroutine *const coro = (KosCoroutine *) self;

  /* Refused here rather than at the throw below, which this reaches
     only after closing and clearing the stored await iterator. */
  if (coro->in_send) {
    PyErr_SetString(PyExc_RuntimeError,
                    "coroutine already being awaited");
    return NULL;
  }

  if (coro->state == KOS_CORO_DONE) {
    Py_RETURN_NONE;
  }

  /* never started: nothing entered, nothing to unwind */
  if (coro->state == KOS_CORO_NEW) {
    coroutine_drop_record(coro);
    coro->state = KOS_CORO_DONE;
    Py_RETURN_NONE;
  }

  /* Close any pending Python awaitable. The iterator is held and
     in_send set across the call, as in coroutine_step: its close can
     run user code that calls back into this coroutine. */
  {
    KosEvalState coro_es;
    KosEvalState_Init(&coro_es, coro->ctx);
    PyObject *await_iter = KosEvalState_PythonAwaitIter(&coro_es);
    if (await_iter) {
      Py_INCREF(await_iter);
      coro->in_send = 1;
      PyObject *result = PyObject_CallMethodObjArgs(
          await_iter, coro_str_close, NULL);
      coro->in_send = 0;
      Py_DECREF(await_iter);
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

  KosCoroutine *const coro = (KosCoroutine *) self;

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
#if PY_MINOR_VERSION >= 10
  .am_send = coroutine_am_send,
#endif
};


static PyObject *coroutine_repr(PyObject *self) {

  KosCoroutine *const coro = (KosCoroutine *) self;
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
  .tp_finalize = coroutine_finalize,
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


int KosCoroutine_TakeFresh(PyObject *self, KosCode **code, PyObject **ns) {

  KosCoroutine *const coro = (KosCoroutine *) self;

  if (coro->state != KOS_CORO_NEW || coro->in_send)
    return 0;

  /* the bound call itself: the program and namespace move to the
     caller, the snapshot is dropped -- the grafted frame runs under
     the awaiter's dynamic state */
  if (! coro->code)
    return 0;
  *code = coro->code;
  coro->code = NULL;
  *ns = coro->ns;
  coro->ns = NULL;
  Py_CLEAR(coro->domain);
  Py_CLEAR(coro->param_refs);
  coro->state = KOS_CORO_DONE;
  return 1;
}


/* === Unit lifecycle === */


int koskari_coro_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

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

  Py_CLEAR(coro_str_throw);
  Py_CLEAR(coro_str_close);

  (void) mod;
  return 0;
}


// The end.
