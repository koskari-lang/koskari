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

   Evaluator core implementation: undelimited multi-shot continuations,
   dynamic wind/unwind, and flattened async execution.

   Invariants:

   1. Context ownership: A KosContext is owned by exactly one Python
      wrapper (KosPyCoroutine or KosPyAsyncGen) during its lifetime.

   2. Frame ownership: Frames are owned by the context. On continuation
      capture, frames are cloned for multi-shot semantics.

   3. Handler chain: Each frame has its own handler chain. Handlers
      are allocated/freed with the frame or via push/pop_wind.

   4. Suspension: When the VM suspends (AWAIT), the context's py_await_iter
      holds the iterator. Resumption injects the result onto the stack.

   5. Multi-shot: Continuations clone all state on invocation, allowing
      multiple independent resumptions.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "lib.h"
#include "eval.h"
#include "stack.h"


/* === Static string constants === */

static PyObject *str_await = NULL;
static PyObject *str_send = NULL;
static PyObject *str_throw = NULL;
static PyObject *str_close = NULL;


/* === KosHandler implementation === */


KosHandler *kos_handler_new(KosHandler *parent,
        PyObject *before, PyObject *after) {

  KosHandler *h = PyMem_Malloc(sizeof(KosHandler));
  if (! h) {
    PyErr_NoMemory();
    return NULL;
  }

  h->parent = parent;
  h->before = before;
  h->after = after;

  Py_XINCREF(before);
  Py_XINCREF(after);

  return h;
}


void kos_handler_free(KosHandler *handler) {

  if (! handler)
    return;

  Py_XDECREF(handler->before);
  Py_XDECREF(handler->after);
  PyMem_Free(handler);
}


static void kos_handler_chain_free(KosHandler *handler) {

  KosHandler *next;
  while (handler) {
    next = handler->parent;
    kos_handler_free(handler);
    handler = next;
  }
}


static KosHandler *kos_handler_clone(KosHandler *handler) {

  if (! handler)
    return NULL;

  KosHandler *cloned_parent = kos_handler_clone(handler->parent);

  KosHandler *clone = kos_handler_new(cloned_parent,
                                      handler->before, handler->after);
  if (! clone) {
    kos_handler_chain_free(cloned_parent);
    return NULL;
  }

  return clone;
}


int kos_handler_traverse(KosHandler *handler, visitproc visit, void *arg) {

  while (handler) {
    Py_VISIT(handler->before);
    Py_VISIT(handler->after);
    handler = handler->parent;
  }

  return 0;
}


/* === KosFrame implementation === */


KosFrame *kos_frame_new(KosCode *code, PyObject *ns,
        KosStackBlock *stack, KosFrame *parent,
        KosHandler *handlers) {

  KosFrame *frame = PyMem_Malloc(sizeof(KosFrame));
  if (! frame) {
    PyErr_NoMemory();
    return NULL;
  }

  frame->parent = parent;
  frame->code = code;
  frame->pc = 0;
  frame->stack = stack;
  frame->ns = ns;
  frame->handlers = handlers;
  frame->flags = 0;

  Py_XINCREF(ns);
  /* Note: code is borrowed, stack is owned */

  return frame;
}


KosFrame *kos_frame_clone(KosFrame *frame) {

  if (! frame)
    return NULL;

  /* Clone parent chain recursively */
  KosFrame *cloned_parent = kos_frame_clone(frame->parent);

  /* Clone handler chain */
  KosHandler *cloned_handlers = kos_handler_clone(frame->handlers);

  /* Use COW for stack - just increment refcount on all blocks */
  KosStackBlock *cloned_stack = KosStackBlock_COW(frame->stack);

  KosFrame *clone = kos_frame_new(frame->code, frame->ns,
                                  cloned_stack, cloned_parent,
                                  cloned_handlers);
  if (! clone) {
    kos_handler_chain_free(cloned_handlers);
    /* Release the COW'd stack blocks */
    while (cloned_stack) {
      KosStackBlock *p = KosStackBlock_PARENT(cloned_stack);
      KosStackBlock_Release(cloned_stack);
      cloned_stack = p;
    }
    /* Free cloned parent chain */
    while (cloned_parent) {
      KosFrame *next = cloned_parent->parent;
      kos_frame_free(cloned_parent);
      cloned_parent = next;
    }
    return NULL;
  }

  clone->pc = frame->pc;
  clone->flags = frame->flags;

  return clone;
}


void kos_frame_free(KosFrame *frame) {

  if (! frame)
    return;

  Py_XDECREF(frame->ns);
  /* code is borrowed, don't decref */

  /* Release the stack block chain */
  KosStackBlock *block = frame->stack;
  while (block) {
    KosStackBlock *p = KosStackBlock_PARENT(block);
    KosStackBlock_Release(block);
    block = p;
  }

  kos_handler_chain_free(frame->handlers);
  PyMem_Free(frame);
}


static void kos_frame_chain_free(KosFrame *frame) {

  KosFrame *next;
  while (frame) {
    next = frame->parent;
    kos_frame_free(frame);
    frame = next;
  }
}


int kos_frame_traverse(
    KosFrame *frame,
    visitproc visit,
    void *arg) {

  while (frame) {
    Py_VISIT(frame->ns);

    if (frame->stack) {
      int res = kos_stackblock_traverse(frame->stack, visit, arg);
      if (res)
        return res;
    }

    if (frame->handlers) {
      int res = kos_handler_traverse(frame->handlers, visit, arg);
      if (res)
        return res;
    }

    frame = frame->parent;
  }

  return 0;
}


/* === KosContext implementation === */


KosContext *kos_context_new(KosFrame *top) {

  KosContext *ctx = PyMem_Malloc(sizeof(KosContext));
  if (! ctx) {
    PyErr_NoMemory();
    return NULL;
  }

  ctx->top = top;
  ctx->py_await_iter = NULL;
  ctx->py_last_yielded = NULL;
  ctx->pending_throw = 0;
  ctx->pending_exc = NULL;
  ctx->flags = 0;

  return ctx;
}


void kos_context_free(KosContext *ctx) {

  if (! ctx)
    return;

  kos_frame_chain_free(ctx->top);
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

  if (ctx->top) {
    return kos_frame_traverse(ctx->top, visit, arg);
  }

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


/* === Wind chain management === */


int kos_push_wind(
    KosContext *ctx,
    PyObject *before,
    PyObject *after) {

  if (! ctx || ! ctx->top) {
    PyErr_SetString(PyExc_RuntimeError, "no active frame for push_wind");
    return -1;
  }

  if (! before && ! after) {
    PyErr_SetString(PyExc_ValueError,
                    "push_wind requires at least one of before or after");
    return -1;
  }

  KosHandler *handler = kos_handler_new(ctx->top->handlers, before, after);
  if (! handler)
    return -1;

  ctx->top->handlers = handler;
  return 0;
}


int kos_push_before_wind(KosContext *ctx, PyObject *before) {

  return kos_push_wind(ctx, before, NULL);
}


int kos_push_after_wind(KosContext *ctx, PyObject *after) {

  return kos_push_wind(ctx, NULL, after);
}


int kos_pop_wind(KosContext *ctx) {

  if (! ctx || ! ctx->top) {
    PyErr_SetString(PyExc_RuntimeError, "no active frame for pop_wind");
    return -1;
  }

  KosHandler *handler = ctx->top->handlers;
  if (! handler) {
    PyErr_SetString(PyExc_RuntimeError, "no handler to pop");
    return -1;
  }

  ctx->top->handlers = handler->parent;
  kos_handler_free(handler);

  return 0;
}


/* === Continuation application === */


/**
   Compute the lowest common ancestor frame between two frame chains.

   This walks both chains simultaneously to find where they diverge.
   Returns the LCA frame, or NULL if chains don't share a common ancestor.
*/
static KosFrame *compute_frame_lca(
    KosFrame *current,
    KosFrame *target) {

  /* Count depths */
  size_t current_depth = 0;
  size_t target_depth = 0;

  for (KosFrame *f = current; f; f = f->parent)
    current_depth++;

  for (KosFrame *f = target; f; f = f->parent)
    target_depth++;

  /* Align to same depth */
  KosFrame *c = current;
  KosFrame *t = target;

  while (current_depth > target_depth) {
    c = c->parent;
    current_depth--;
  }

  while (target_depth > current_depth) {
    t = t->parent;
    target_depth--;
  }

  /* Walk together until we find LCA */
  while (c != t) {
    c = c->parent;
    t = t->parent;
  }

  return c;
}


/**
   Collect unwind (after) hooks from current frame chain down to (but not
   including) the lca frame.

   Returns a malloc'd array of PyObject* (borrowed refs) with count stored
   in out_count. Caller must free the array.

   Hooks are collected inner-to-outer (from current towards lca).
*/
static PyObject **collect_unwind_hooks(
    KosFrame *current,
    KosFrame *lca,
    size_t *out_count) {

  size_t capacity = 8;
  size_t count = 0;
  PyObject **hooks = PyMem_Malloc(sizeof(PyObject *) * capacity);

  if (! hooks) {
    PyErr_NoMemory();
    return NULL;
  }

  for (KosFrame *f = current; f && f != lca; f = f->parent) {
    for (KosHandler *h = f->handlers; h; h = h->parent) {
      if (h->after) {
        if (count >= capacity) {
          capacity *= 2;
          PyObject **new_hooks = PyMem_Realloc(hooks,
                                               sizeof(PyObject *) * capacity);
          if (! new_hooks) {
            PyMem_Free(hooks);
            PyErr_NoMemory();
            return NULL;
          }
          hooks = new_hooks;
        }
        hooks[count++] = h->after;
      }
    }
  }

  *out_count = count;
  return hooks;
}


/**
   Collect wind (before) hooks from lca down to target frame.

   Returns a malloc'd array of PyObject* (borrowed refs) with count stored
   in out_count. Caller must free the array.

   Hooks are collected outer-to-inner (from lca towards target).
   We need to reverse the natural traversal order since we walk from
   target backwards.
*/
static PyObject **collect_wind_hooks(
    KosFrame *target,
    KosFrame *lca,
    size_t *out_count) {

  /* First, collect in reverse order (target to lca) */
  size_t capacity = 8;
  size_t count = 0;
  PyObject **hooks = PyMem_Malloc(sizeof(PyObject *) * capacity);

  if (! hooks) {
    PyErr_NoMemory();
    return NULL;
  }

  for (KosFrame *f = target; f && f != lca; f = f->parent) {
    for (KosHandler *h = f->handlers; h; h = h->parent) {
      if (h->before) {
        if (count >= capacity) {
          capacity *= 2;
          PyObject **new_hooks = PyMem_Realloc(hooks,
                                               sizeof(PyObject *) * capacity);
          if (! new_hooks) {
            PyMem_Free(hooks);
            PyErr_NoMemory();
            return NULL;
          }
          hooks = new_hooks;
        }
        hooks[count++] = h->before;
      }
    }
  }

  /* Reverse the array to get outer-to-inner order */
  for (size_t i = 0; i < count / 2; i++) {
    PyObject *tmp = hooks[i];
    hooks[i] = hooks[count - 1 - i];
    hooks[count - 1 - i] = tmp;
  }

  *out_count = count;
  return hooks;
}


int kos_apply_continuation(
    KosContext *ctx,
    KosFrame *target_top,
    KosHandler *target_handlers,
    int is_throw,
    PyObject *value_or_exc) {

  if (! ctx) {
    PyErr_SetString(PyExc_RuntimeError, "NULL context");
    return -1;
  }

  if (! target_top) {
    PyErr_SetString(PyExc_RuntimeError, "NULL target frame");
    return -1;
  }

  KosFrame *current = ctx->top;
  KosFrame *lca = compute_frame_lca(current, target_top);

  /* Collect hooks */
  size_t unwind_count = 0;
  size_t wind_count = 0;

  PyObject **unwind_hooks = collect_unwind_hooks(current, lca, &unwind_count);
  if (! unwind_hooks && PyErr_Occurred())
    return -1;

  PyObject **wind_hooks = collect_wind_hooks(target_top, lca, &wind_count);
  if (! wind_hooks && PyErr_Occurred()) {
    PyMem_Free(unwind_hooks);
    return -1;
  }

  /* Execute unwind hooks (inner to outer) */
  for (size_t i = 0; i < unwind_count; i++) {
    PyObject *result = PyObject_CallNoArgs(unwind_hooks[i]);
    if (! result) {
      /* Exception during unwind - this is bad but we continue */
      PyErr_Clear();
    } else {
      Py_DECREF(result);
    }
  }

  PyMem_Free(unwind_hooks);

  /* Install target state */
  ctx->top = target_top;
  /* Note: target_handlers would be on target_top->handlers already */

  /* Execute wind hooks (outer to inner) */
  for (size_t i = 0; i < wind_count; i++) {
    PyObject *result = PyObject_CallNoArgs(wind_hooks[i]);
    if (! result) {
      /* Exception during wind - this is problematic */
      PyMem_Free(wind_hooks);
      return -1;
    }
    Py_DECREF(result);
  }

  PyMem_Free(wind_hooks);

  /* Inject resume value or exception */
  if (is_throw) {
    ctx->pending_throw = 1;
    Py_XINCREF(value_or_exc);
    Py_XDECREF(ctx->pending_exc);
    ctx->pending_exc = value_or_exc;

  } else {
    /*
      Push value onto target stack.
      This requires access to a PyObject* stack wrapper.
      For now, we store it in a temporary location.
    */
    /* TODO: properly push onto target frame's stack */
    if (target_top->stack) {
      PyErr_SetString(PyExc_NotImplementedError,
                      "stack value injection not yet implemented");
      return -1;
    }
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

  /*
    TODO: Create a new stack for the frame.
    For now, pass NULL.
  */
  KosFrame *frame = kos_frame_new(code, ns, NULL, ctx->top, NULL);
  if (! frame)
    return -1;

  ctx->top = frame;
  return 0;
}


int kos_tail_koscall(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  if (! ctx || ! ctx->top) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for tail_koscall");
    return -1;
  }

  KosFrame *old = ctx->top;
  KosFrame *parent = old->parent;

  /* Unwind handlers on the old frame */
  for (KosHandler *h = old->handlers; h; h = h->parent) {
    if (h->after) {
      PyObject *result = PyObject_CallNoArgs(h->after);
      if (result) {
        Py_DECREF(result);
      } else {
        PyErr_Clear();  /* Swallow exception during unwind */
      }
    }
  }

  /* Create new frame with same parent */
  KosFrame *frame = kos_frame_new(code, ns, NULL, parent, NULL);
  if (! frame)
    return -1;

  /* Free old frame */
  old->parent = NULL;  /* Don't free the parent chain */
  kos_frame_free(old);

  ctx->top = frame;
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

  /*
    TODO: Implement actual opcode execution loop here.
    For now, this is a stub that demonstrates the event mechanism.

    The full implementation would:
    1. Fetch opcode from ctx->top->code at ctx->top->pc
    2. Dispatch and execute
    3. Handle boundaries (RETURN, RAISE, YIELD, AWAIT)

    For the initial slice, we just return RETURN with None.
  */

  if (! ctx->top) {
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

  kos_frame_chain_free(cont->captured_top);
  kos_handler_chain_free(cont->captured_handlers);

  Py_TYPE(self)->tp_free(self);
}


static int continuation_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  if (cont->captured_top) {
    int res = kos_frame_traverse(cont->captured_top, visit, arg);
    if (res)
      return res;
  }

  if (cont->captured_handlers) {
    return kos_handler_traverse(cont->captured_handlers, visit, arg);
  }

  return 0;
}


static int continuation_clear(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  kos_frame_chain_free(cont->captured_top);
  cont->captured_top = NULL;

  kos_handler_chain_free(cont->captured_handlers);
  cont->captured_handlers = NULL;

  return 0;
}


static PyObject *continuation_call(PyObject *self,
        PyObject *args, PyObject *kwds) {

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

  /* Clone captured state for multi-shot semantics */
  KosFrame *cloned_top = kos_frame_clone(cont->captured_top);
  if (! cloned_top && cont->captured_top) {
    /* Clone failed */
    return NULL;
  }

  KosHandler *cloned_handlers = kos_handler_clone(cont->captured_handlers);
  if (! cloned_handlers && cont->captured_handlers) {
    kos_frame_chain_free(cloned_top);
    return NULL;
  }

  /* Create new context */
  KosContext *ctx = kos_context_new(cloned_top);
  if (! ctx) {
    kos_frame_chain_free(cloned_top);
    kos_handler_chain_free(cloned_handlers);
    return NULL;
  }

  /* TODO: inject resume_value onto the stack */

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
  .tp_doc = "Captured continuation (multi-shot, undelimited)",
  .tp_traverse = continuation_traverse,
  .tp_clear = continuation_clear,
};


PyObject *KosPyContinuation_New(
    KosFrame *top,
    KosHandler *handlers) {

  KosPyContinuation *cont = PyObject_GC_New(KosPyContinuation,
                                            &KosPyContinuationType);
  if (! cont)
    return NULL;

  cont->captured_top = top;
  cont->captured_handlers = handlers;

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

  return 0;
}


// The end.
