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
   Close the chain on ctx with GeneratorExit for a close with no caller
   to raise into, holding the error the close owes for the caller to
   report or raise once the chain is freed (see eval.h)
*/
int kos_close_drive(
    KosContext *ctx,
    PyObject **typ,
    PyObject **val,
    PyObject **tb) {

  PyObject *pending = KosContext_PY_AWAIT_ITER(ctx);
  PyObject *exc;
  KosOpResult res;
  int left = 0;

  *typ = NULL;
  *val = NULL;
  *tb = NULL;

  /* a context the collector cleared lost its chain without the
     cleanup; the throw cannot be made, and that is what is owed */
  if (KosContext_FLAGS(ctx) & KOS_CTX_CLEARED) {
    PyErr_SetString(PyExc_RuntimeError,
                    "close of a generator whose context was cleared");
    PyErr_Fetch(typ, val, tb);
    return 1;
  }

  if (pending) {
    Py_INCREF(pending);
    kos_iter_close(pending);
    Py_DECREF(pending);
    Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
  }

  exc = PyObject_CallNoArgs(PyExc_GeneratorExit);
  if (! exc) {
    PyErr_Fetch(typ, val, tb);
    return 1;
  }

  res = koskari_eval(ctx, NULL, NULL, PyExc_GeneratorExit, exc);
  Py_DECREF(exc);
  Py_XDECREF(res.val);

  switch (res.kind) {
    case KOS_RES_RETURN:
      break;

    case KOS_RES_AWAIT:
    case KOS_RES_YIELD:
      /* a close with no caller cannot suspend: cannot honor */
      PyErr_SetString(
          PyExc_RuntimeError, "async generator ignored GeneratorExit");
      left = 1;
      break;

    default:
      /* GeneratorExit or StopIteration coming back is the close
         completing; anything else is what the cleanup raised */
      if (PyErr_ExceptionMatches(PyExc_GeneratorExit)
          || PyErr_ExceptionMatches(PyExc_StopIteration)
          || PyErr_ExceptionMatches(PyExc_StopAsyncIteration))
        PyErr_Clear();
      break;
  }

  PyErr_Fetch(typ, val, tb);
  return left;
}


/**
   Report a close error held by kos_close_drive against obj; no-op when
   nothing is held
*/
void kos_close_report(
    PyObject *obj,
    PyObject *typ,
    PyObject *val,
    PyObject *tb) {

  if (! typ)
    return;

  PyErr_Restore(typ, val, tb);
  PyErr_WriteUnraisable(obj);
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

/* === The fetch: one next element, keyed by the iterator's kind ===

   Every site that asks an iterator for its next element asks through
   kos_iter_fetch with the iterator's kind (bound by
   kos_dyn_iter_request_bind_iter or kos_iter_request_bind_next): the
   for-each step reads the kind its region bound once, the next
   request and a provider's in-step fetch classify on the way in. The
   sync arm calls tp_iternext through the type, as CPython's FOR_ITER
   does, and consults the error state only on a NULL result; a
   StopIteration there is exhaustion. A provider steps through its
   suite, a python async iterator through the eager __anext__ step.
   The fetch records nothing: a step that defers leaves the caller's
   own record holding the iterator, and the resume that takes the
   delivery finishes it. */


/* The cold end of kos_iter_next_sync (evaluated.h): a sync step
   returned NULL. */
KOS_NOINLINE KosIterRequestStatus kos_iter_next_sync_ended(int had_next) {

  if (PyErr_Occurred()) {
    if (! PyErr_ExceptionMatches(PyExc_StopIteration))
      return KOS_IREQ_ERROR;
    PyErr_Clear();
  } else if (unlikely(! had_next)) {
    PyErr_SetString(PyExc_TypeError, "not an iterator");
    return KOS_IREQ_ERROR;
  }
  return KOS_IREQ_EXHAUSTED;
}


/* The async arm: __anext__'s awaitable stepped once, eagerly. A
   completion is the element; a suspension is EXIT with *out the
   awaitable to suspend on, the element arriving at revive; a
   StopAsyncIteration, synchronous or from the step, is exhaustion. */
static KosIterRequestStatus iter_fetch_async(
    KosContext *ctx,
    PyObject *iter,
    PyObject **out) {

  PyObject *awaitable = get_await_next(iter);
  PyObject *result, *yielded = NULL;
  int suspended = 0;

  if (! awaitable) {
    /* No error: the type lost its __anext__ since it was classified;
       take the sync step, which raises for an iterator with neither. */
    if (! PyErr_Occurred())
      return kos_iter_next_sync(iter, out);
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyErr_Clear();
      return KOS_IREQ_EXHAUSTED;
    }
    return KOS_IREQ_ERROR;
  }

  result = KosContext_EagerStepAwaitable(ctx, awaitable, &suspended, &yielded);
  Py_DECREF(awaitable);

  if (suspended && yielded) {
    KosContext_MarkSuspendedNotRunning(ctx);
    *out = yielded;
    return KOS_IREQ_EXIT;
  }
  if (result) {
    *out = result;
    return KOS_IREQ_DELIVERED;
  }
  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyErr_Clear();
    return KOS_IREQ_EXHAUSTED;
  }
  return KOS_IREQ_ERROR;
}


/**
   The next element of the bound iterator req describes, by its kind.
   *out is NULL on entry's behalf and set as KosIterRequestStatus says.
*/
KOS_OP_INLINE KosIterRequestStatus kos_iter_fetch(
    KosContext *ctx,
    const KosIterRequest *req,
    PyObject **out) {

  *out = NULL;

#if KOS_EVAL_SAFETYNET
  if (! kos_iter_kind_fits(req->iter, req->kind, req->islots)) {
    PyErr_SetString(PyExc_SystemError,
                    "iteration request kind does not match its iterator");
    return KOS_IREQ_ERROR;
  }
#endif

  switch (req->kind) {
    case KOS_IREQ_KIND_PY_SYNC:
      return kos_iter_next_sync(req->iter, out);

    case KOS_IREQ_KIND_PROVIDER:
      /* one step through the suite; the end's payload is dropped,
         since a fetch has no use for it, and yield-from, which does,
         steps the provider itself */
      switch (kos_iter_step_provider(ctx, req->iter, req->islots,
                                     req->send, out)) {
        case KOS_ITER_VALUE:
          return KOS_IREQ_DELIVERED;
        case KOS_ITER_EXHAUSTED:
          Py_CLEAR(*out);
          return KOS_IREQ_EXHAUSTED;
        case KOS_ITER_INSTALLED:
        case KOS_ITER_INSTALLED_VALUE:
          return KOS_IREQ_CONTINUES;
        case KOS_ITER_ERROR:
        default:
          return KOS_IREQ_ERROR;
      }

    case KOS_IREQ_KIND_PY_ASYNC:
      return iter_fetch_async(ctx, req->iter, out);
    default:
      PyErr_SetString(PyExc_TypeError, "not an iterator");
      return KOS_IREQ_ERROR;
  }
}


/* === The for-each step: one entry, one resume per step instruction ===

   The step instructions ITER_NEXT, ITER_NEXT_STORE_FAST and
   ITER_NEXT_STORE_VAR each name a delivery and a resume and make the
   request through KOS_ITER_STEP, which fetches by the kind the region
   bound. An element in hand is delivered by the handler itself: it
   finishes the region's request from the region it holds, runs the
   instruction's delivery (a push, a slot store, a named store) and
   threads on, with no cooperation point of its own, since CONTINUE,
   the back-edge every step follows, polls the breaker. A deferred
   element (a Koskari generator installed, a provider's invoke
   grafted, a python async step awaited) reaches the resume and
   nothing else: the resume is set on the loop's frame and the
   delivery that reaches the frame runs it, the element or its end.
   The resume finishes the region's request, since the region owns
   the iterator, runs the same delivery, then threads on. The resume's
   state is the step instruction, which carries the slot or name. The
   kinds take their delivery as it is: an element is data, never
   awaited. An end in hand never reaches the resume: the step finishes
   the request and jumps to the else target itself. A failure reaches
   the resume as a NULL with the error pending: the region's own end
   is the end of the iteration, anything else declines and the raise
   walk goes on. */


/* The step is over: finish the region's request, exhausted at the
   end. The region is at dyn_top when a delivery reaches the loop's
   frame. */
KOS_OP_INLINE void kos_iter_step_finish(KosContext *ctx, int ended) {
  KosDynNode *region = KosContext_DYN_TOP(ctx);

  if (region && KosDynNode_IS_FOREACH(region))
    kos_dyn_iter_request_finish_req(KosDynForeach_REQ(region), ended);
}


/**
   A failure delivered to a step resume: NULL with the error pending.
   The region's own exhaustion exception (StopIteration from an
   installed step, StopAsyncIteration from an async source) is the
   end of the iteration, the same judgment the eager step makes: the
   region's request is finished and idle, the error cleared, and the
   resume goes to its else target. Anything else propagates: the
   resume finishes the request, since the step it waited on is over
   and nobody else holds that record, and declines; the raise walk
   goes on to the region. The region is at dyn_top, the frames above
   it already unwound.

   :return: 1 when the exception was the end, the request finished and
     the error cleared; 0 to decline, the request finished and the
     error still pending
*/
KOS_OP_INLINE int kos_iter_step_ended_by_exception(KosContext *ctx) {
  KosDynNode *region = KosContext_DYN_TOP(ctx);

  if (! region || ! KosDynNode_IS_FOREACH(region) || ! PyErr_Occurred())
    return 0;
  /* the region's own end: StopAsyncIteration from an async source,
     StopIteration from any other kind */
  if (! PyErr_ExceptionMatches(
          KosDynForeach_REQ(region)->kind == KOS_IREQ_KIND_PY_ASYNC
              ? PyExc_StopAsyncIteration : PyExc_StopIteration)) {
    kos_dyn_iter_request_finish_req(KosDynForeach_REQ(region), 0);
    return 0;
  }

  PyErr_Clear();
  kos_dyn_iter_request_finish_req(KosDynForeach_REQ(region), 1);
  return 1;
}


/* === The three deliveries: the element's destination by instruction.
   Each consumes value and answers 0, or -1 with the error set. The
   handler runs them for an element in hand, the resume for a deferred
   one. === */


/* ITER_NEXT: the element onto the stack. */
KOS_OP_INLINE int iter_next_push_deliver(
    KosContext *ctx,
    KosOp *insn,
    PyObject *value) {

  (void) insn;
  return KosContext_StackPUSH(ctx, value);
}


/* ITER_NEXT_STORE_FAST: the element into the fast slot the
   instruction names. */
KOS_OP_INLINE int iter_next_store_fast_deliver(
    KosContext *ctx,
    KosOp *insn,
    PyObject *value) {

  const KosExtra *row = KosOp_ROW(insn);

  return kos_ns_fast_store_steal(KosContext_NAMESPACE(ctx),
                                 (size_t) KosExtra_IMM(row, 0), value);
}


/* ITER_NEXT_STORE_VAR: the element under the name the instruction
   names. */
KOS_OP_INLINE int iter_next_store_var_deliver(
    KosContext *ctx,
    KosOp *insn,
    PyObject *value) {

  const KosExtra *row = KosOp_ROW(insn);
  int res = frame_ns_store(ctx, KosExtra_OBJ(row, 0), value);

  Py_DECREF(value);
  return res;
}


/* The element or a failure: push it and thread on, or jump to the
   instruction's else target. */
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
  kos_iter_step_finish(ctx, 0);
  if (unlikely(iter_next_push_deliver(ctx, insn, value) < 0))
    return EVAL_RAISE();
  KOS_THREAD_NEXT(ctx);
}


/* The element into the fast slot the instruction names, or a
   failure. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult iter_next_store_fast_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosOp *insn = (KosOp *) state;

  if (unlikely(! value)) {
    if (! kos_iter_step_ended_by_exception(ctx))
      return EVAL_RAISE();
    KOS_THREAD_AT(ctx, KosExtra_TARGET(KosOp_ROW(insn), 1));
  }
  kos_iter_step_finish(ctx, 0);
  if (unlikely(iter_next_store_fast_deliver(ctx, insn, value) < 0))
    return EVAL_RAISE();
  KOS_THREAD_NEXT(ctx);
}


/* The element under the name the instruction names, or a failure. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult iter_next_store_var_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosOp *insn = (KosOp *) state;

  if (unlikely(! value)) {
    if (! kos_iter_step_ended_by_exception(ctx))
      return EVAL_RAISE();
    KOS_THREAD_AT(ctx, KosExtra_TARGET(KosOp_ROW(insn), 1));
  }
  kos_iter_step_finish(ctx, 0);
  if (unlikely(iter_next_store_var_deliver(ctx, insn, value) < 0))
    return EVAL_RAISE();
  KOS_THREAD_NEXT(ctx);
}


KOS_RESUME_KIND_AS_IS(kos_iter_next_resume_kind, "for-each-step",
                       iter_next_push_resume, NULL, kos_resume_clone_shared);
KOS_RESUME_KIND_AS_IS(kos_iter_next_store_fast_resume_kind, "for-each-step",
                       iter_next_store_fast_resume, NULL,
                       kos_resume_clone_shared);
KOS_RESUME_KIND_AS_IS(kos_iter_next_store_var_resume_kind, "for-each-step",
                       iter_next_store_var_resume, NULL,
                       kos_resume_clone_shared);


/* A step instruction outside a FOREACH region. */
static KOS_NOINLINE void kos_iter_step_misplaced(const KosOp *insn) {
  PyErr_Format(PyExc_RuntimeError,
               "%s must be used inside a PUSH_FOREACH block",
               kos_opcode_name((KosOpCode) KosOp_OP(insn)));
}


/**
   KOS_ITER_STEP(ctx, insn, deliver, resume, else_target): one request
   for the next element of the FOREACH region at dyn_top, from the
   step instruction insn whose delivery is deliver and whose resume is
   resume, each outcome threaded to its destination. An element in
   hand finishes the region's request, is delivered by the handler
   and threads on; the end in hand finishes the region's request as
   exhausted and threads to else_target; a deferred step (the
   producer's frames installed, an async step awaiting) sets the
   resume on the loop's frame, the one that ran the step, and threads
   into the installed frame or leaves for the await; a step that
   fails before it defers raises at the frame with no resume set:
   nothing is in flight, the provider has undone its own step, and
   the raise walks as any instruction's does. The region is at
   dyn_top when a step runs: the step sits at the loop's continue
   target and every node the body pushes is gone before CONTINUE,
   whose poll is the loop's cooperation point. The calling handler
   must have the KosThreadFn prototype.
*/
#define KOS_ITER_STEP(ctx, insn, deliver, resume, else_target)          \
  do {                                                                  \
    KosDynNode *kos_is_region_ = KosContext_DYN_TOP(ctx);               \
    KosDynFrame *kos_is_frame_ = KosContext_FRAME(ctx);                 \
    KosIterRequest *kos_is_req_;                                        \
    PyObject *kos_is_out_;                                              \
    if (unlikely(! kos_is_region_                                       \
                 || ! KosDynNode_IS_FOREACH(kos_is_region_))) {         \
      kos_iter_step_misplaced((const KosOp *) (insn));                  \
      RESULT_RAISE();                                                   \
    }                                                                   \
    kos_is_req_ = KosDynForeach_REQ(kos_is_region_);                    \
    switch (kos_iter_fetch((ctx), kos_is_req_, &kos_is_out_)) {         \
      case KOS_IREQ_DELIVERED:                                          \
        if (kos_is_req_->islots)                                        \
          kos_iter_finish(kos_is_req_->iter, kos_is_req_->islots, 0);   \
        if (unlikely(deliver((ctx), (KosOp *) (insn), kos_is_out_) < 0))\
          RESULT_RAISE();                                               \
        KOS_THREAD_NEXT(ctx);                                           \
      case KOS_IREQ_EXHAUSTED:                                          \
        kos_iter_step_finish((ctx), 1);                                 \
        KOS_THREAD_AT((ctx), (else_target));                            \
      case KOS_IREQ_CONTINUES:                                          \
        KosDynFrame_RESUME_KIND(kos_is_frame_) = (resume);              \
        KosDynFrame_RESUME_STATE(kos_is_frame_) = (void *) (insn);      \
        KOS_ITER_INSTALLED_THREAD(ctx);                                 \
      case KOS_IREQ_EXIT:                                               \
        KosDynFrame_RESUME_KIND(kos_is_frame_) = (resume);              \
        KosDynFrame_RESUME_STATE(kos_is_frame_) = (void *) (insn);      \
        return EVAL_AWAIT(kos_is_out_);                                 \
      case KOS_IREQ_ERROR:                                              \
      default:                                                          \
        RESULT_RAISE();                                                 \
    }                                                                   \
  } while (0)


/* === The next request: KosContext_NextResume, the arity as kind ===

   OBJECT_NEXT and its fused twins VAR_NEXT and FAST_NEXT make their
   request through KosContext_NextResume with one of two kinds, the
   instruction's arity picking it: kos_next_resume_kind for the
   one-argument form, kos_next_default_resume_kind for the
   two-argument form. The iterator is the resume's owned state. The
   element reaches that resume and nothing else: in hand the handler
   hops into it through RESULT_ENTRY, deferred the delivery reaches it
   through the frame, and an in-hand end through the prologue with
   StopIteration pending. The two-argument form's default stays on
   the operand stack under the halt and is the result at the end. A
   resume cleared without a delivery, as a transfer landing on the
   frame clears it, finishes the step. */


/**
   The kind's clear: a resume cleared without its delivery, as a
   transfer landing on the frame clears it, abandons the step in
   flight. The provider is finished as not exhausted so it can be
   stepped again, and the iterator released.
*/
static void kos_next_state_clear(void *state) {

  PyObject *iter = (PyObject *) state;
  KosIterSlots *islots;

  if (! iter)
    return;
  if (KosIter_CheckSlots(iter, &islots))
    kos_iter_finish(iter, islots, 0);
  Py_DECREF(iter);
}


/* The kind's traverse: the iterator is the state's one owned object. */
static int kos_next_state_traverse(void *state, visitproc visit, void *arg) {
  Py_VISIT((PyObject *) state);
  return 0;
}


/**
   The element, the end, or a failure, for a next instruction of arity
   n, iter being the resume's owned iterator. The step is finished and
   the iterator released first. An element replaces the default on the
   stack, when there is one, and the result is NORMAL for the calling
   resume to thread on. The end, StopIteration (or an async source's
   StopAsyncIteration) pending, leaves the default as the result for
   the two-argument form, also NORMAL, and raises StopIteration for
   the one-argument form, the pending exception standing when it is
   one. Anything else declines.
*/
static inline KosOpResult next_step_deliver(
    KosContext *ctx,
    PyObject *iter,
    unsigned n,
    PyObject *value) {

  KosIterSlots *islots;
  int ended;

  if (! value) {
    if (unlikely(! PyErr_Occurred())) {
      Py_DECREF(iter);
      PyErr_SetString(PyExc_SystemError, "next resume with no delivery");
      return EVAL_RAISE();
    }
    ended = PyErr_ExceptionMatches(PyExc_StopIteration)
        || PyErr_ExceptionMatches(PyExc_StopAsyncIteration);
  } else {
    ended = 0;
  }

  /* The step is over: the instruction's iterator, owned by the
     resume's state, is finished when it is a provider and released. */
  if (KosIter_CheckSlots(iter, &islots))
    kos_iter_finish(iter, islots, ended);
  Py_DECREF(iter);
  if (! value && ! ended)
    return EVAL_RAISE();

  if (! ended) {
    if (n == 2) {
      PyObject *dflt = KosContext_StackPopRef(ctx);
      if (unlikely(! dflt)) {
        Py_DECREF(value);
        return EVAL_RAISE();
      }
      Py_DECREF(dflt);
    }
    if (unlikely(KosContext_StackPUSH(ctx, value) < 0))
      return EVAL_RAISE();
    return EVAL_NORMAL();
  }

  if (n == 2) {
    /* the default already sits where the result goes */
    PyErr_Clear();
    return EVAL_NORMAL();
  }
  if (PyErr_Occurred()) {
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyErr_Clear();
      PyErr_SetNone(PyExc_StopIteration);
    }
    return EVAL_RAISE();
  }
  PyErr_SetNone(PyExc_StopIteration);
  return EVAL_RAISE();
}


/* The one-argument form's resume: the end raises StopIteration. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult next_step_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosOpResult r = next_step_deliver(ctx, (PyObject *) state, 1, value);

  if (likely(r.kind == KOS_RES_NORMAL))
    KOS_THREAD_NEXT(ctx);
  return r;
}


/* The two-argument form's resume: the end leaves the default. */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult next_default_step_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  KosOpResult r = next_step_deliver(ctx, (PyObject *) state, 2, value);

  if (likely(r.kind == KOS_RES_NORMAL))
    KOS_THREAD_NEXT(ctx);
  return r;
}


KOS_RESUME_KIND_AS_IS_OWNING(kos_next_resume_kind, "next",
                             next_step_resume, kos_next_state_clear,
                             kos_resume_clone_object,
                             kos_next_state_traverse);

KOS_RESUME_KIND_AS_IS_OWNING(kos_next_default_resume_kind, "next",
                             next_default_step_resume, kos_next_state_clear,
                             kos_resume_clone_object,
                             kos_next_state_traverse);


/* === The awaited step: anext, asend, athrow and aclose on a native frame ===

   An awaited stepper installs through its await suite as a next or
   throw request made from a native frame (KosContext_NextInto,
   KosContext_ThrowInto). The element or end reaches the resume on that
   frame and the resume completes the frame: a yield of the provider
   returns to whoever awaited. The state is the owned provider, as for
   a next, so the next kinds' clear and traverse serve. */


/**
   The anext, asend and athrow kinds' resume: an element is returned
   from the frame; the end, StopIteration or StopAsyncIteration
   pending, raises StopAsyncIteration carrying the payload, as the
   async generator protocol answers; any other failure declines.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult astep_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  PyObject *iter = (PyObject *) state;
  KosIterSlots *islots;
  int ended = 0;

  if (! value) {
    if (unlikely(! PyErr_Occurred())) {
      Py_DECREF(iter);
      PyErr_SetString(PyExc_SystemError, "awaited step with no delivery");
      return EVAL_RAISE();
    }
    ended = PyErr_ExceptionMatches(PyExc_StopIteration)
        || PyErr_ExceptionMatches(PyExc_StopAsyncIteration);
  }

  if (KosIter_CheckSlots(iter, &islots))
    kos_iter_finish(iter, islots, ended);
  Py_DECREF(iter);

  if (value)
    RESULT_FRAME_RETURN(ctx, value, NULL);
  if (! ended)
    return EVAL_RAISE();

  if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
    PyObject *payload = NULL;

    if (PyGen_FetchStopIterationValue(&payload) < 0)
      return EVAL_RAISE();
    if (payload && payload != Py_None) {
      PyObject *exc = koskari_StopAsyncIteration_FromValue(payload);

      Py_DECREF(payload);
      if (exc) {
        PyErr_SetObject((PyObject *) Py_TYPE(exc), exc);
        Py_DECREF(exc);
        return EVAL_RAISE();
      }
      if (PyErr_Occurred())
        return EVAL_RAISE();
    } else {
      Py_XDECREF(payload);
    }
    PyErr_SetNone(PyExc_StopAsyncIteration);
  }
  return EVAL_RAISE();
}


/**
   The aclose kind's resume: the end, or a GeneratorExit the body let
   through, returns None from the frame; an element means the body
   yielded under GeneratorExit, which the protocol refuses; any other
   failure declines.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult aclose_resume(
    KosContext *ctx,
    void *state,
    PyObject *value) {

  PyObject *iter = (PyObject *) state;
  KosIterSlots *islots;
  int ended = 0;

  if (! value) {
    if (unlikely(! PyErr_Occurred())) {
      Py_DECREF(iter);
      PyErr_SetString(PyExc_SystemError, "awaited close with no delivery");
      return EVAL_RAISE();
    }
    ended = PyErr_ExceptionMatches(PyExc_StopIteration)
        || PyErr_ExceptionMatches(PyExc_StopAsyncIteration)
        || PyErr_ExceptionMatches(PyExc_GeneratorExit);
  }

  if (KosIter_CheckSlots(iter, &islots))
    kos_iter_finish(iter, islots, 1);
  Py_DECREF(iter);

  if (value) {
    Py_DECREF(value);
    PyErr_SetString(PyExc_RuntimeError,
                    "async generator ignored GeneratorExit");
    return EVAL_RAISE();
  }
  if (! ended)
    return EVAL_RAISE();

  PyErr_Clear();
  RESULT_FRAME_RETURN(ctx, Py_NewRef(Py_None), NULL);
}


const KosResumeKind kos_astep_resume_kind = {
  "anext", astep_resume, kos_next_state_clear, kos_resume_clone_object,
  NULL, 0, kos_next_state_traverse };

const KosResumeKind kos_aclose_resume_kind = {
  "aclose", aclose_resume, kos_next_state_clear, kos_resume_clone_object,
  NULL, 0, kos_next_state_traverse };


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
