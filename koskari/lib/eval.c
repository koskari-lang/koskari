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
   koskari/lib/eval.c

   Evaluator core implementation: delimited multi-shot continuations,
   dynamic wind/unwind, and flattened async execution.

   This version uses the DynNode chain infrastructure from dynchain.h
   for unified dynamic extent tracking.

   Invariants:

   1. Context ownership: A KosContext is owned by exactly one Python
      wrapper (KosPyCoroutine or KosPyAsyncGen) during its lifetime.

   2. Chain ownership: The dynamic chain is owned by the context.
      On continuation capture, chains are cloned for multi-shot semantics.

   3. Suspension: When the VM suspends (AWAIT), the context's py_await_iter
      holds the iterator. Resumption injects the result onto the stack.

   4. Multi-shot: Continuations clone all state on invocation, allowing
      multiple independent resumptions.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "lib.h"
#include "eval.h"
#include "bytecode.h"


/* === Static string constants === */

static PyObject *str_await = NULL;
static PyObject *str_send = NULL;
static PyObject *str_throw = NULL;
static PyObject *str_close = NULL;


/* === KosFrame implementation (wrapper around KosDynFrame) === */


KosFrame *kos_frame_new(
        KosCode *code,
        PyObject *ns,
        KosStackBlock *stack,
        KosDynNode *parent) {

  return kos_dynframe_new(parent, code, ns, stack);
}


KosFrame *kos_frame_clone(KosFrame *frame) {
  return kos_dynframe_clone(frame);
}


void kos_frame_free(KosFrame *frame) {
  kos_dynframe_free(frame);
}


int kos_frame_traverse(
        KosFrame *frame,
        visitproc visit,
        void *arg) {

  if (! frame)
    return 0;

  Py_VISIT(frame->ns);

  if (frame->stack) {
    KosStackBlock *b = frame->stack;
    while (b) {
      for (size_t i = b->len; i--; ) {
        Py_VISIT(b->objs[i]);
      }
      b = KosStackBlock_PARENT(b);
    }
  }

  return 0;
}


/* === KosContext implementation === */


KosContext *kos_context_new(KosDynFrame *top) {

  KosContext *ctx = PyMem_Malloc(sizeof(KosContext));
  if (! ctx) {
    PyErr_NoMemory();
    return NULL;
  }

  ctx->dyn_top = (KosDynNode *) top;
  ctx->frame_top = top;

  /* Initialize hook runner */
  ctx->hook_runner.code = NULL;
  ctx->hook_runner.pc = 0;
  ctx->hook_runner.scratch = NULL;
  ctx->hook_runner.active = 0;

  /* Initialize transfer plan state */
  ctx->plan = NULL;
  ctx->plan_stack = NULL;
  ctx->chain_version = 0;

  /* Initialize Python await bridging */
  ctx->py_await_iter = NULL;
  ctx->py_last_yielded = NULL;

  /* Initialize exception injection */
  ctx->pending_throw = 0;
  ctx->pending_exc = NULL;

  ctx->flags = 0;

  return ctx;
}


void kos_context_free(KosContext *ctx) {

  if (! ctx)
    return;

  /* Free the dynamic chain */
  kos_dynchain_free(ctx->dyn_top, NULL);

  /* Free any active transfer plan */
  kos_free_transfer_plan(ctx->plan);
  while (ctx->plan_stack) {
    KosTransferPlan *p = ctx->plan_stack;
    ctx->plan_stack = p->suspended;
    kos_free_transfer_plan(p);
  }

  /* Free hook runner scratch stack */
  if (ctx->hook_runner.scratch) {
    KosStackBlock *b = ctx->hook_runner.scratch;
    while (b) {
      KosStackBlock *p = KosStackBlock_PARENT(b);
      KosStackBlock_Release(b);
      b = p;
    }
  }

  Py_XDECREF(ctx->py_await_iter);
  Py_XDECREF(ctx->py_last_yielded);
  Py_XDECREF(ctx->pending_exc);

  PyMem_Free(ctx);
}


int kos_context_traverse(
        KosContext *ctx,
        visitproc visit,
        void *arg) {

  if (! ctx)
    return 0;

  Py_VISIT(ctx->py_await_iter);
  Py_VISIT(ctx->py_last_yielded);
  Py_VISIT(ctx->pending_exc);

  /* Traverse the dynamic chain */
  int res = kos_dynchain_traverse(ctx->dyn_top, NULL, visit, arg);
  if (res)
    return res;

  /* Traverse transfer plan */
  res = kos_transferplan_traverse(ctx->plan, visit, arg);
  if (res)
    return res;

  /* Traverse hook runner */
  res = kos_hookrunner_traverse(&ctx->hook_runner, visit, arg);
  if (res)
    return res;

  return 0;
}


/* === GC helper for stack blocks === */


int kos_stackblock_traverse(
        KosStackBlock *b,
        visitproc visit,
        void *arg) {

  size_t i;
  while (b) {
    for (i = b->len; i--; ) {
      Py_VISIT(b->objs[i]);
    }
    b = b->back;
  }

  return 0;
}


/* === Koskari call operations === */


int kos_koscall(
        KosContext *ctx,
        KosCode *code,
        PyObject *ns) {

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for koscall");
    return -1;
  }

  KosDynFrame *frame = kos_dyn_push_frame(ctx, code, ns);
  if (! frame)
    return -1;

  return 0;
}


int kos_tail_koscall(
        KosContext *ctx,
        KosCode *code,
        PyObject *ns) {

  if (! ctx || ! ctx->frame_top) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for tail_koscall");
    return -1;
  }

  KosDynFrame *old = ctx->frame_top;
  KosDynNode *parent = old->base.parent;

  /* Pop the old frame */
  ctx->dyn_top = parent;

  /* Create new frame with same parent */
  KosDynFrame *frame = kos_dynframe_new(parent, code, ns, NULL);
  if (! frame) {
    /* Restore old frame on failure */
    ctx->dyn_top = (KosDynNode *) old;
    return -1;
  }

  /* Free old frame */
  kos_dynframe_free(old);

  /* Install new frame */
  ctx->dyn_top = (KosDynNode *) frame;
  ctx->frame_top = frame;
  ctx->chain_version++;

  return 0;
}


/* === Drive loop === */


/**
   Attempt eager-step of a Python awaitable.

   This tries to get __await__() and call send(None) once.
   If the awaitable completes immediately (StopIteration), the value
   is returned. If it yields, we store the iterator and return the
   yielded value with *suspended = 1.

   :param awaitable: the Python awaitable object
   :param ctx: context to store await state if suspended
   :param suspended: output flag, set to 1 if we need to suspend
   :param yielded: output, the yielded value if suspended

   :return: completed value on immediate completion, NULL on suspend or error
*/
static PyObject *eager_step_awaitable(
        PyObject *awaitable,
        KosContext *ctx,
        int *suspended,
        PyObject **yielded) {

  *suspended = 0;
  *yielded = NULL;

  /* Get __await__ */
  PyObject *await_iter = NULL;

  if (! str_await) {
    str_await = PyUnicode_InternFromString("__await__");
    if (! str_await)
      return NULL;
  }

  PyObject *await_method = PyObject_GetAttr(awaitable, str_await);
  if (! await_method) {
    /* Maybe it's already an iterator? */
    PyErr_Clear();
    await_iter = awaitable;
    Py_INCREF(await_iter);
  } else {
    await_iter = PyObject_CallNoArgs(await_method);
    Py_DECREF(await_method);
    if (! await_iter)
      return NULL;
  }

  /* Try first send(None) */
  if (! str_send) {
    str_send = PyUnicode_InternFromString("send");
    if (! str_send) {
      Py_DECREF(await_iter);
      return NULL;
    }
  }

  PyObject *result = PyObject_CallMethodObjArgs(await_iter, str_send,
                                                Py_None, NULL);

  if (result) {
    /* Yielded a value - need to suspend */
    *suspended = 1;
    *yielded = result;

    /* Store iterator for later resumption */
    Py_XDECREF(ctx->py_await_iter);
    ctx->py_await_iter = await_iter;

    return NULL;
  }

  /* Check for StopIteration (immediate completion) */
  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    PyObject *value = NULL;
    PyGen_FetchStopIterationValue(&value);
    Py_DECREF(await_iter);

    if (! value)
      value = Py_None;

    Py_INCREF(value);
    return value;
  }

  /* Other exception */
  Py_DECREF(await_iter);
  return NULL;
}


int kos_drive(KosContext *ctx, KosEvent *out_ev) {

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context for kos_drive");
    return -1;
  }

  if (! out_ev) {
    PyErr_SetString(PyExc_RuntimeError, "NULL output event");
    return -1;
  }

  /* Mark context as running */
  ctx->flags |= KOS_CTX_RUNNING;

  /* Handle pending exception injection */
  if (ctx->pending_throw && ctx->pending_exc) {
    PyErr_SetObject((PyObject *) Py_TYPE(ctx->pending_exc), ctx->pending_exc);
    Py_CLEAR(ctx->pending_exc);
    ctx->pending_throw = 0;

    out_ev->kind = KOS_EV_RAISE;
    out_ev->payload = NULL;
    PyErr_Fetch(&out_ev->payload, NULL, NULL);

    ctx->flags &= ~KOS_CTX_RUNNING;
    ctx->flags |= KOS_CTX_DEAD;

    return 0;
  }

  /* Check for active transfer plan */
  if (ctx->plan) {
    int plan_res = kos_run_transfer_step(ctx);
    if (plan_res < 0) {
      out_ev->kind = KOS_EV_RAISE;
      out_ev->payload = NULL;
      PyErr_Fetch(&out_ev->payload, NULL, NULL);
      ctx->flags &= ~KOS_CTX_RUNNING;
      ctx->flags |= KOS_CTX_DEAD;
      return 0;
    }
    if (plan_res == 0) {
      /* More plan steps - check if hook is now active */
      if (kos_hook_active(ctx)) {
        /* Hook execution - would continue in hook mode */
        /* For now, just return RETURN with None */
      }
    } else {
      /* Plan complete - free it */
      kos_free_transfer_plan(ctx->plan);
      ctx->plan = NULL;
      ctx->flags &= ~KOS_CTX_IN_TRANSFER;

      /* Check for suspended plans to resume */
      if (ctx->plan_stack) {
        kos_resume_transfer_plan(ctx);
      }
    }
  }

  /*
    TODO: Implement actual opcode execution loop here.
    For now, this is a stub that demonstrates the event mechanism.

    The full implementation would:
    1. Check if hook_runner is active, if so execute from hook code/pc
    2. Otherwise fetch opcode from ctx->frame_top->code at ctx->frame_top->pc
    3. Dispatch and execute
    4. Handle boundaries (RETURN, RAISE, YIELD, AWAIT)
    5. Handle new opcodes (PUSH_PROMPT, POP_PROMPT, SHIFT, etc.)
  */

  if (! ctx->frame_top) {
    /* No frame - return None */
    out_ev->kind = KOS_EV_RETURN;
    out_ev->payload = Py_None;
    Py_INCREF(Py_None);

    ctx->flags &= ~KOS_CTX_RUNNING;
    ctx->flags |= KOS_CTX_DEAD;

    return 0;
  }

  /*
    Stub: immediately return None.
    Real implementation would run opcodes here.
  */
  out_ev->kind = KOS_EV_RETURN;
  out_ev->payload = Py_None;
  Py_INCREF(Py_None);

  ctx->flags &= ~KOS_CTX_RUNNING;
  ctx->flags |= KOS_CTX_DEAD;

  return 0;
}


/* === KosPyContinuation type === */


static void continuation_dealloc(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  kos_continuation_free(cont->captured);

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

  /* Parse arguments: single optional value */
  PyObject *resume_value = Py_None;
  if (! PyArg_ParseTuple(args, "|O:continuation", &resume_value))
    return NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError,
                    "continuation does not accept keyword arguments");
    return NULL;
  }

  if (! cont->captured) {
    PyErr_SetString(PyExc_RuntimeError, "continuation has no captured state");
    return NULL;
  }

  /* Clone captured state for multi-shot semantics */
  KosContinuation *cloned = kos_continuation_clone(cont->captured);
  if (! cloned)
    return NULL;

  /* Create new context with the cloned chain */
  KosDynFrame *top_frame = cloned->resume_frame;
  cloned->captured_chain = NULL;  /* context takes ownership */

  KosContext *ctx = kos_context_new(top_frame);
  if (! ctx) {
    kos_continuation_free(cloned);
    return NULL;
  }

  /* Set up the context's dynamic chain */
  /* TODO: properly restore the full chain */

  kos_continuation_free(cloned);

  /* Create and return a coroutine wrapper */
  return KosPyCoroutine_New(ctx);
}


static PyObject *continuation_repr(PyObject *self) {

  return PyUnicode_FromFormat("<continuation at %p>", self);
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
  .tp_doc = "Captured continuation (multi-shot, delimited)",
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


/* === KosPyCoroutine type === */


static void coroutine_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  if (coro->ctx)
    kos_context_free(coro->ctx);

  Py_XDECREF(coro->pending_await);

  Py_TYPE(self)->tp_free(self);
}


static int coroutine_traverse(
        PyObject *self,
        visitproc visit,
        void *arg) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  Py_VISIT(coro->pending_await);

  if (coro->ctx) {
    return kos_context_traverse(coro->ctx, visit, arg);
  }

  return 0;
}


static int coroutine_clear(PyObject *self) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

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

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

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
    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  str_send, value, NULL);

    if (result) {
      /* Still yielding */
      return result;
    }

    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      /* Python awaitable completed */
      PyObject *await_result = NULL;
      PyGen_FetchStopIterationValue(&await_result);

      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_RUNNING;

      /* TODO: inject await_result into VM and continue driving */
      Py_XDECREF(await_result);

    } else {
      /* Exception from Python awaitable */
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_DONE;
      return NULL;
    }
  }

  coro->in_send = 1;
  coro->state = KOS_CORO_RUNNING;

  /* Drive the VM */
  KosEvent ev = {0};
  int res = kos_drive(coro->ctx, &ev);

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
      /* Eager-step the awaitable */
      int suspended = 0;
      PyObject *yielded = NULL;
      PyObject *immediate = eager_step_awaitable(ev.payload, coro->ctx,
                                                 &suspended, &yielded);
      Py_DECREF(ev.payload);

      if (immediate) {
        /* Awaitable completed immediately - inject result and continue */
        /* TODO: inject immediate result and loop back to drive */
        Py_DECREF(immediate);

        /* For now, just return it as if yielded */
        return immediate;
      }

      if (suspended) {
        coro->state = KOS_CORO_WAITING_PY;
        return yielded;
      }

      /* Error from awaitable */
      coro->state = KOS_CORO_DONE;
      return NULL;
    }
  }

  /* Should not reach here */
  PyErr_SetString(PyExc_RuntimeError, "unexpected event kind");
  return NULL;
}


static PyObject *coroutine_throw(PyObject *self, PyObject *args) {

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  if (! PyArg_ParseTuple(args, "O|OO:throw", &typ, &val, &tb))
    return NULL;

  if (coro->state == KOS_CORO_DONE) {
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  /* If waiting on Python awaitable, forward throw */
  if (coro->state == KOS_CORO_WAITING_PY && coro->ctx->py_await_iter) {
    if (! str_throw) {
      str_throw = PyUnicode_InternFromString("throw");
      if (! str_throw)
        return NULL;
    }

    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  str_throw, typ, val, tb,
                                                  NULL);

    if (result) {
      return result;
    }

    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
      PyObject *await_result = NULL;
      PyGen_FetchStopIterationValue(&await_result);
      Py_CLEAR(coro->ctx->py_await_iter);
      coro->state = KOS_CORO_RUNNING;
      /* TODO: inject and continue */
      Py_XDECREF(await_result);
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

  KosPyCoroutine *coro = (KosPyCoroutine *) self;

  if (coro->state == KOS_CORO_DONE) {
    Py_RETURN_NONE;
  }

  /* Close any pending Python awaitable */
  if (coro->ctx->py_await_iter) {
    if (! str_close) {
      str_close = PyUnicode_InternFromString("close");
      if (! str_close)
        return NULL;
    }

    PyObject *result = PyObject_CallMethodObjArgs(coro->ctx->py_await_iter,
                                                  str_close, NULL);
    Py_XDECREF(result);
    PyErr_Clear();
    Py_CLEAR(coro->ctx->py_await_iter);
  }

  /* Throw GeneratorExit */
  PyObject *args = Py_BuildValue("(O)", PyExc_GeneratorExit);
  if (! args)
    return NULL;

  PyObject *result = coroutine_throw(self, args);
  Py_DECREF(args);

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

  KosPyCoroutine *coro = (KosPyCoroutine *) self;
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


PyTypeObject KosPyCoroutineType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.Coroutine",
  .tp_basicsize = sizeof(KosPyCoroutine),
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


PyObject *KosPyCoroutine_New(KosContext *ctx) {

  KosPyCoroutine *coro = PyObject_GC_New(KosPyCoroutine, &KosPyCoroutineType);
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
  int res = kos_drive(agen->ctx, &ev);

  agen->in_progress = 0;

  if (res < 0) {
    agen->closed = 1;
    PyErr_SetNone(PyExc_StopAsyncIteration);
    return NULL;
  }

  switch (ev.kind) {
    case KOS_EV_RETURN:
      /* Generator completed */
      agen->closed = 1;
      Py_XDECREF(ev.payload);
      PyErr_SetNone(PyExc_StopAsyncIteration);
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

    case KOS_EV_YIELD:
      /* Yielded a value - wrap in StopIteration to signal completion
         of this __anext__ awaitable */
      PyErr_SetObject(PyExc_StopIteration, ev.payload);
      Py_XDECREF(ev.payload);
      return NULL;

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


int koskari_eval_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* Initialize string constants */
  STR_CONST(str_await, "__await__");
  STR_CONST(str_send, "send");
  STR_CONST(str_throw, "throw");
  STR_CONST(str_close, "close");

  /* Initialize dynchain module */
  if (koskari_dynchain_init(mod))
    return -1;

  /* Register types */
  if (PyType_Ready(&KosPyContinuationType))
    return -1;

  if (PyType_Ready(&KosPyCoroutineType))
    return -1;

  if (PyType_Ready(&KosPyAsyncGenType))
    return -1;

  if (PyType_Ready(&KosPyAsyncGenASendType))
    return -1;

  if (PyDict_SetItemString(dict, "Continuation",
                           (PyObject *) &KosPyContinuationType))
    return -1;

  if (PyDict_SetItemString(dict, "Coroutine",
                           (PyObject *) &KosPyCoroutineType))
    return -1;

  if (PyDict_SetItemString(dict, "AsyncGenerator",
                           (PyObject *) &KosPyAsyncGenType))
    return -1;

  return 0;
}


int koskari_eval_clear(PyObject *mod) {

  Py_CLEAR(str_await);
  Py_CLEAR(str_send);
  Py_CLEAR(str_throw);
  Py_CLEAR(str_close);

  koskari_dynchain_clear(mod);

  return 0;
}


// The end.
