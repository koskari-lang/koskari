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


/* === The for-each step: one entry, one resume per step instruction ===

   The step instructions ITER_NEXT, ITER_NEXT_STORE_FAST and
   ITER_NEXT_STORE_VAR each name a resume and hand the request to
   kos_iter_step. The element reaches that resume and nothing else:
   in hand, the handler hops into it at once (RESULT_ENTRY); deferred
   (a Koskari generator installed, a python async step awaited), the
   resume is set on the loop's frame and the iteration node calls it
   when the element arrives, or with KosIterEnd when the iteration
   ends (KosIterRequest_Deliver). The resume's state is the step
   instruction, which carries the else target and the slot or name.
   The kinds are direct and take their delivery as it is: an element
   is data, never awaited. Nothing comes here on failure: a failure
   propagates through the raise walk, and the region's unwind clears
   the resume. */


/**
   A failure delivered to a step resume: NULL with the error pending.
   The region's own exhaustion exception (StopIteration from an
   installed step, StopAsyncIteration from an async source) is the
   end of the iteration, the same judgment the eager step makes: the
   region's request is finished and idle, the error cleared, and the
   resume goes to its else target. Anything else propagates: the
   resume declines and the raise walk goes on to the region. The
   region is at dyn_top, the frames above it already unwound.

   :return: 1 when the exception was the end, the request finished and
     the error cleared; 0 to decline, the error still pending
*/
KOS_OP_INLINE int kos_iter_step_ended_by_exception(KosContext *ctx) {
  KosDynNode *region = KosContext_DYN_TOP(ctx);

  if (! region || ! KosDynNode_IS_FOREACH(region) || ! PyErr_Occurred())
    return 0;
  if (! PyErr_ExceptionMatches(kos_dyn_iter_node_exhaustion_exc(region)))
    return 0;

  PyErr_Clear();
  kos_dyn_iter_request_finish(region, 1);
  kos_dyn_iter_request_set_mode(region, KOS_IREQ_IDLE);
  return 1;
}


/* The element, the end, or a failure: push it and thread on, or jump
   to the instruction's else target. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult iter_next_push_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosOp *insn = (KosOp *) state;

  if (unlikely(! value)) {
    if (! kos_iter_step_ended_by_exception(ctx))
      return EVAL_RAISE();
    KOS_THREAD_AT(ctx, KosOp_TARGET(insn));
  }
  if (value == KosIterEnd) {
    Py_DECREF(value);
    KOS_THREAD_AT(ctx, KosOp_TARGET(insn));
  }
  if (unlikely(KosContext_StackPUSH(ctx, value) < 0))
    return EVAL_RAISE();
  KOS_THREAD_NEXT(ctx);
}


/* The element into the fast slot the instruction names, the end, or
   a failure. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult iter_next_store_fast_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosOp *insn = (KosOp *) state;
  const KosExtra *row = KosOp_ROW(insn);

  if (unlikely(! value)) {
    if (! kos_iter_step_ended_by_exception(ctx))
      return EVAL_RAISE();
    KOS_THREAD_AT(ctx, KosExtra_TARGET(row, 1));
  }
  if (value == KosIterEnd) {
    Py_DECREF(value);
    KOS_THREAD_AT(ctx, KosExtra_TARGET(row, 1));
  }
  if (unlikely(kos_ns_fast_store_steal(
          KosContext_NAMESPACE(ctx), (size_t) KosExtra_IMM(row, 0),
          value) < 0))
    return EVAL_RAISE();
  KOS_THREAD_NEXT(ctx);
}


/* The element under the name the instruction names, the end, or a
   failure. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult iter_next_store_var_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosOp *insn = (KosOp *) state;
  const KosExtra *row = KosOp_ROW(insn);
  int res;

  if (unlikely(! value)) {
    if (! kos_iter_step_ended_by_exception(ctx))
      return EVAL_RAISE();
    KOS_THREAD_AT(ctx, KosExtra_TARGET(row, 1));
  }
  if (value == KosIterEnd) {
    Py_DECREF(value);
    KOS_THREAD_AT(ctx, KosExtra_TARGET(row, 1));
  }
  res = frame_ns_store(ctx, KosExtra_OBJ(row, 0), value);
  Py_DECREF(value);
  if (unlikely(res < 0))
    return EVAL_RAISE();
  KOS_THREAD_NEXT(ctx);
}


KOS_RESUME_KIND_DIRECT(kos_iter_next_resume_kind, "for-each-step",
                       iter_next_push_resume, NULL, kos_resume_clone_shared);
KOS_RESUME_KIND_DIRECT(kos_iter_next_store_fast_resume_kind, "for-each-step",
                       iter_next_store_fast_resume, NULL,
                       kos_resume_clone_shared);
KOS_RESUME_KIND_DIRECT(kos_iter_next_store_var_resume_kind, "for-each-step",
                       iter_next_store_var_resume, NULL,
                       kos_resume_clone_shared);


/**
   One request for the next element of the FOREACH region at dyn_top,
   for the step instruction insn whose resume is resume. A plain sync
   iterator steps through tp_iternext here; every other kind through
   KosIterRequest_Next. Reports RETURN with the element, or with
   KosIterEnd at the end, for the handler's RESULT_ENTRY to hop into
   the resume; a deferred step sets the resume on the loop's frame
   and reports the install or the await. The region is at dyn_top
   when a step runs: the step sits at the loop's continue target and
   every node the body pushes is gone before CONTINUE.
*/
KOS_OP_INLINE KosOpResult kos_iter_step(
    KosContext *ctx,
    KosOp *insn,
    const KosResumeKind *resume) {

  KosDynNode *region = KosContext_DYN_TOP(ctx);
  KosDynFrame *frame = KosContext_FRAME(ctx);
  KosIterRequest *req;

  if (unlikely(! region || ! KosDynNode_IS_FOREACH(region))) {
    PyErr_Format(PyExc_RuntimeError,
                 "%s must be used inside a PUSH_FOREACH block",
                 kos_opcode_name((KosOpCode) KosOp_OP(insn)));
    return EVAL_RAISE();
  }
  req = KosDynForeach_REQ(region);

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
        return EVAL_RAISE();
      }
    }
#endif

    n = likely(next) ? next(iter) : NULL;
    if (likely(n))
      return EVAL_RETURN(n);
    if (op_get_next_sync_ended(next != NULL) == GET_NEXT_ERROR) {
      /* the failure reaches the resume like any other delivery */
      KosDynFrame_RESUME_KIND(frame) = resume;
      KosDynFrame_RESUME_STATE(frame) = (void *) insn;
      return EVAL_RAISE();
    }
    return EVAL_RETURN(Py_NewRef(KosIterEnd));
  }

  {
    PyObject *out = NULL;

    switch (KosIterRequest_Next(ctx, region, NULL, &out)) {
      case KOS_IREQ_DELIVERED:
        return EVAL_RETURN(out);

      case KOS_IREQ_EXHAUSTED:
        Py_XDECREF(out);
        return EVAL_RETURN(Py_NewRef(KosIterEnd));

      case KOS_IREQ_CONTINUES:
        /* the install left the producer's frame current; the loop's
           frame, the one that ran the step, takes the resume */
        KosDynFrame_RESUME_KIND(frame) = resume;
        KosDynFrame_RESUME_STATE(frame) = (void *) insn;
        return kos_iter_installed(ctx);

      case KOS_IREQ_EXIT:
        KosDynFrame_RESUME_KIND(frame) = resume;
        KosDynFrame_RESUME_STATE(frame) = (void *) insn;
        return EVAL_AWAIT(out);

      case KOS_IREQ_ERROR:
      default:
        /* the failure reaches the resume like any other delivery: set
           on the frame, raised, offered by the prologue as a NULL */
        KosDynFrame_RESUME_KIND(frame) = resume;
        KosDynFrame_RESUME_STATE(frame) = (void *) insn;
        return EVAL_RAISE();
    }
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
