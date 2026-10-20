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
  koskari/lib/eval/eval_iter.c

  Python iteration from the evaluator: iterator acquire and close over
  both protocols, result classification, the OBJECT_NEXT / ITER_NEXT
  next paths, and the globals namespace lookup. Included into eval.c as
  a segment (see the eval/ includes there).

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


/* === Python iter next (OBJECT_NEXT / ITER_NEXT opcodes) === */


/* defined later in eval.c; the deliver helper stores by name through it */
static int frame_ns_store(KosContext *ctx, PyObject *name, PyObject *val);


/**
   Acquire an iterator from an iterable, uniform over both protocols
   (the PUSH_FOREACH preference order, shared with OBJECT_ITER so the
   two cannot drift): a KosIter provider passes through as itself (a
   KosPyIter included: its suite forwards to its provider); an object
   with am_aiter yields its async iterator; otherwise the sync protocol
   (PyObject_GetIter, including the sequence fallback).

   :param iterable: borrowed
   :return: new iterator reference, or NULL with PyErr set
*/
PyObject *kos_iter_acquire(PyObject *iterable) {

  if (KosIter_Check(iterable)) {
    Py_INCREF(iterable);
    return iterable;
  }

  if (Py_TYPE(iterable)->tp_as_async
      && Py_TYPE(iterable)->tp_as_async->am_aiter)
    return Py_TYPE(iterable)->tp_as_async->am_aiter(iterable);

  return PyObject_GetIter(iterable);
}


/**
   Report an error a close raised: there is no frame to propagate into,
   so it goes to sys.unraisablehook as a finalizer's error does. No-op
   when nothing is raised.
*/
static void iter_close_report(PyObject *iter) {

  if (PyErr_Occurred())
    PyErr_WriteUnraisable(iter);
}


/**
   Close an acquired iterator, best-effort and exception-fenced: a
   provider through its suite's ki_close, a sync iterator through
   close(), an async iterator through aclose() with its awaitable
   demand-driven -- an immediate completion honors the cleanup, a
   genuine suspension takes the cannot-honor fallback of closing the
   in-flight iterator. A missing close method is ignored; an error the
   close raises is reported as unraisable. Any pending exception is
   preserved.
*/
void kos_iter_close(PyObject *iter) {

  PyObject *ftyp, *fval, *ftb;

  if (! iter)
    return;

  PyErr_Fetch(&ftyp, &fval, &ftb);

  {
    KosIterSlots *islots;

    if (KosIter_CheckSlots(iter, &islots)) {
      (void) islots->ki_close(iter);
      iter_close_report(iter);

    } else if (Py_TYPE(iter)->tp_iternext) {
      (void) kos_iter_close_or_raise(iter);
      iter_close_report(iter);

    } else {
      /* Look the method up apart from calling it, so only a missing
         aclose is ignored and an AttributeError its body raises is
         reported like any other error. */
      PyObject *meth = PyObject_GetAttrString(iter, "aclose");
      PyObject *aw = NULL;

      if (meth) {
        aw = PyObject_CallNoArgs(meth);
        Py_DECREF(meth);
      } else if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
      }

      if (aw) {
        PyObject *it = kos_get_awaitable(aw);
        Py_DECREF(aw);
        if (it) {
          PyObject *step = NULL;
          if (kos_iter_send(it, Py_None, &step) == PYGEN_NEXT) {
            /* Suspend attempt during close; cannot honor further --
               close the iterator so its GeneratorExit cleanup runs. */
            (void) kos_iter_close_or_raise(it);
          }
          Py_XDECREF(step);
          Py_DECREF(it);
        }
      }
      iter_close_report(iter);
    }
  }

  PyErr_Restore(ftyp, fval, ftb);
}


/**
   Classify a produced result under ambient async: a non-awaitable, a
   Defer, or a future/task handle is a completed value; anything else
   awaitable must be awaited before use.
*/
int KosEval_ResultIsValue(PyObject *result) {

  PyTypeObject *typ = Py_TYPE(result);

  if (! typ->tp_as_async || ! typ->tp_as_async->am_await)
    return 1;
  if (KosDefer_Check(result))
    return 1;
  return KosEval_IsFuture(result);
}

/**
   Get next value from a Python async iterator using the given __anext__
   awaitable. Caller must have obtained awaitable via get_await_next(iter);
   this helper consumes it (DECREF). *result_out is set with a new
   reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_async_iter(
    KosContext *ctx,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out,
    PyObject *awaitable) {

  (void) iter;
  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosContext_EagerStepAwaitable(
      ctx, awaitable, &suspended, &yielded);
  Py_DECREF(awaitable);

  if (suspended && yielded) {
    KosContext_MarkSuspendedNotRunning(ctx);
    *await_out = yielded;
    return GET_NEXT_SUSPEND;
  }

  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyErr_Clear();
    return GET_NEXT_EXHAUSTED;
  }

  if (result) {
    *result_out = result;
    return GET_NEXT_VALUE;
  }

  return GET_NEXT_ERROR;
}


/**
   Get next value from a Python sync iterator (PyIter_Next).
   *result_out is set with a new reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_sync_iter(
    KosContext *ctx,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out) {

  (void) ctx;
  (void) await_out;

  PyObject *n = PyIter_Next(iter);
  if (n) {
    *result_out = n;
    return GET_NEXT_VALUE;
  }

  if (PyErr_Occurred())
    return GET_NEXT_ERROR;

  return GET_NEXT_EXHAUSTED;
}


/**
   Dispatcher: get next from iter (Koskari async gen, Python async iter,
   or Python sync iter). Does not push; does not perform GetIter.
   *result_out set only on GET_NEXT_VALUE (caller pushes and DECREF).
*/
static inline KosGetNextResult op_get_next_from_iter(
    KosContext *ctx,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out) {

  /* Koskari async gen is handled in NEXT/ITER_NEXT via in-loop attach; never here. */

  PyObject *awaitable = get_await_next(iter);
  if (awaitable) {
    KosGetNextResult r = next_from_python_async_iter(ctx, await_out, iter,
                                                      result_out, awaitable);
    return r;
  }

  /* NULL from get_await_next means the object has no am_anext, with
     no exception set, or that __anext__ or GetAwaitable failed. A
     synchronous StopAsyncIteration is this iterator's exhaustion,
     exactly as it is once the awaitable is stepped. */
  if (PyErr_Occurred()) {
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyErr_Clear();
      return GET_NEXT_EXHAUSTED;
    }
    return GET_NEXT_ERROR;
  }

  if (PyIter_Check(iter))
    return next_from_python_sync_iter(ctx, await_out, iter, result_out);

  PyErr_SetString(PyExc_TypeError, "not an iterator");
  return GET_NEXT_ERROR;
}


/**
   The step for an iteration node's bound Python iterator, keyed by the
   kind its bind recorded, so no step probes the type again. The sync
   arm calls tp_iternext through the type, as CPython's FOR_ITER does,
   and consults the error state only on a NULL result; a StopIteration
   there is exhaustion. The async arm is the eager __anext__ step.
   Providers never reach here; their suite steps them.
*/
/* A sync step returned NULL: a StopIteration or a clean NULL is
   exhaustion, any other error propagates. had_next is 0 when the type
   lost its tp_iternext since the bind classified it. */
static KOS_NOINLINE KosGetNextResult op_get_next_sync_ended(int had_next) {

  if (PyErr_Occurred()) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration))
      return GET_NEXT_ERROR;
    PyErr_Clear();
  } else if (unlikely(! had_next)) {
    PyErr_SetString(PyExc_TypeError, "not an iterator");
    return GET_NEXT_ERROR;
  }
  return GET_NEXT_EXHAUSTED;
}


static inline KosGetNextResult op_get_next_from_bound(
    KosContext *ctx,
    uint8_t kind,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out) {

  if (likely(kind == KOS_IREQ_KIND_PY_SYNC)) {
    iternextfunc next = Py_TYPE(iter)->tp_iternext;
    PyObject *n = likely(next) ? next(iter) : NULL;
    if (likely(n)) {
      *result_out = n;
      return GET_NEXT_VALUE;
    }
    return op_get_next_sync_ended(next != NULL);
  }

  if (kind == KOS_IREQ_KIND_PY_ASYNC) {
    PyObject *awaitable = get_await_next(iter);
    if (awaitable)
      return next_from_python_async_iter(ctx, await_out, iter, result_out,
                                         awaitable);
    /* No error: the type lost its __anext__ since the bind classified
       it; take the sync step, which raises for an iterator with neither.
       Otherwise __anext__ or GetAwaitable failed, or raised
       StopAsyncIteration synchronously, which is this iterator's
       exhaustion. */
    if (! PyErr_Occurred())
      return op_get_next_from_bound(ctx, KOS_IREQ_KIND_PY_SYNC, await_out,
                                    iter, result_out);
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyErr_Clear();
      return GET_NEXT_EXHAUSTED;
    }
    return GET_NEXT_ERROR;
  }

  PyErr_SetString(PyExc_TypeError, "not an iterator");
  return GET_NEXT_ERROR;
}


/**
   The step the FOREACH handlers take.  A sync iterator steps through
   tp_iternext in the handler itself; every other kind steps through
   KosIterRequest_Next.  The bind guarantees a sync kind carries no
   provider suite, so the request mode is untouched on this arm.  On
   DELIVERED *fast_out is the element (new ref), else NULL.
*/
KOS_OP_INLINE KosIterRequestStatus kos_foreach_request_next(
    KosContext *ctx,
    KosDynNode *region,
    PyObject **fast_out) {

  KosIterRequest *req = KosDynForeach_REQ(region);

  if (likely(req->kind == KOS_IREQ_KIND_PY_SYNC)) {
    PyObject *iter = req->iter;
    iternextfunc next = Py_TYPE(iter)->tp_iternext;
    PyObject *n;

#if KOS_EVAL_SAFETYNET
    {
      KosIterSlots *fresh = NULL;
      if (req->islots || KosIter_CheckSlots(iter, &fresh)) {
        PyErr_SetString(PyExc_SystemError,
                        "sync iteration request carries a provider suite");
        *fast_out = NULL;
        return KOS_IREQ_ERROR;
      }
    }
#endif

    n = likely(next) ? next(iter) : NULL;
    if (likely(n)) {
      *fast_out = n;
      return KOS_IREQ_DELIVERED;
    }
    *fast_out = NULL;
    if (op_get_next_sync_ended(next != NULL) == GET_NEXT_ERROR)
      return KOS_IREQ_ERROR;
    return KOS_IREQ_EXHAUSTED;
  }

  return KosIterRequest_Next(ctx, region, NULL, fast_out);
}


/**
   Record the step instruction's else pc on the FOREACH node before the
   step, so a raise-walk exhaustion conversion lands on it: ITER_NEXT
   carries the pc as its arg, the STORE twins as the LABEL field of
   their operand record.
*/
KOS_OP_INLINE void kos_foreach_record_else(
    KosDynNode *region,
    const KosOp *step) {

  if (KosOp_OP(step) == KOS_OP_ITER_NEXT)
    KosDynForeach_ITER_NEXT_ELSE_PC(region) = KosOp_ARG(step);
  else
    KosDynForeach_ITER_NEXT_ELSE_PC(region) =
        KosOp_PC(KosExtra_TARGET(KosOp_ROW(step), 1));
}


/* The else target of a step instruction, for the exhaustion jump. */
KOS_OP_INLINE KosOp *kos_foreach_step_else(const KosOp *step) {
  if (KosOp_OP(step) == KOS_OP_ITER_NEXT)
    return KosOp_TARGET(step);
  return KosExtra_TARGET(KosOp_ROW(step), 1);
}


/**
   Store an element where an ITER_NEXT_STORE_* step names: the fast
   slot of ITER_NEXT_STORE_FAST, the name of ITER_NEXT_STORE_VAR, in
   the current frame namespace. Consumes val, failure included. 0/-1.
*/
KOS_OP_INLINE int kos_foreach_store_element(
    KosContext *ctx,
    const KosOp *step,
    PyObject *val) {

  const KosExtra *row = KosOp_ROW(step);

  if (KosOp_OP(step) == KOS_OP_ITER_NEXT_STORE_FAST)
    return kos_ns_fast_store_steal(
        KosContext_NAMESPACE(ctx), (size_t) KosExtra_IMM(row, 0), val);

  {
    int res = frame_ns_store(ctx, KosExtra_OBJ(row, 0), val);
    Py_DECREF(val);
    return res;
  }
}


/**
   Deliver an element in hand as the step instruction would: ITER_NEXT
   pushes it, a STORE twin stores it. For a step run away from its
   instruction (the CONTINUE back-edge). Consumes val. 0/-1.
*/
KOS_OP_INLINE int kos_foreach_deliver_in_hand(
    KosContext *ctx,
    const KosOp *step,
    PyObject *val) {

  if (KosOp_OP(step) == KOS_OP_ITER_NEXT)
    return KosContext_StackPUSH(ctx, val);
  return kos_foreach_store_element(ctx, step, val);
}


/**
   The store resume: the deferred element of an ITER_NEXT_STORE_* step
   arrives here, staged by the FOREACH node's delivery because the
   frame carries this resume, and is stored where the step names. The
   state is the step instruction that deferred. The kind takes its
   delivery as it is: settling it would await an element that is
   itself awaitable. Nothing comes here on failure: the prologue hands
   no failure to a bytecode frame's resume, and the node clears the
   resume at exhaustion and at its teardown.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult foreach_store_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  if (unlikely(! value))
    return EVAL_RAISE();
  if (unlikely(kos_foreach_store_element(ctx, (const KosOp *) state, value) < 0))
    return EVAL_RAISE();
  return EVAL_NORMAL();
}

const KosResumeKind kos_foreach_store_resume_kind = {
  "for-each-store", foreach_store_resume, NULL, kos_resume_clone_shared, 0
};


/**
   A STORE step that defers (a Koskari generator installed, an async
   step awaited) sets the store resume on the loop's frame for the
   element to come: frame is the live frame as it was before the step,
   since an install leaves the generator's frame current. ITER_NEXT
   sets nothing, its element is pushed as ever.
*/
KOS_OP_INLINE void kos_foreach_defer_step(
    KosDynFrame *frame,
    const KosOp *step) {

  if (KosOp_OP(step) != KOS_OP_ITER_NEXT) {
    KosDynFrame_RESUME_KIND(frame) = &kos_foreach_store_resume_kind;
    KosDynFrame_RESUME_STATE(frame) = (void *) step;
  }
}


/**
  Globals
*/
static inline PyObject *KosContext_GlobalsNamespace(KosContext *ctx) {

  KosDynFrame *frame = KosContext_FRAME(ctx);
  PyObject *ns;

  if (! frame) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for globals lookup");
    return NULL;
  }

  ns = KosDynFrame_NS(frame);
  if (! ns || ! KosNamespace_Check(ns)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "frame has no namespace for globals lookup");
    return NULL;
  }

  while (KosNamespace_PARENT(ns))
    ns = KosNamespace_PARENT(ns);

  return ns;
}


// The end.
