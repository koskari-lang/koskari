/* Included by transform.c -- dropwhile */


/**
   koskari/lib/transform/dropwhile.c

   KosDropWhile: a native lazy dropwhile over one source, duck-typed
   against itertools.dropwhile, implemented as a KosIter provider on
   the shared transform body (transform.c). This unit holds only the
   dropwhile step and the resume chain it arms.

   The predicate is consulted only while it keeps holding. The first
   element it rejects is emitted, the flag latches, and every element
   after it is a bare pass-through with no call at all -- so a long
   tail costs what a bare iteration costs. The flag outlives each
   step, so it is this provider's own field rather than base.pending,
   which ki_finish clears at every step completion.

   The fast path is pure C: a sync source element and an immediate
   verdict drop or emit with the evaluator untouched, dropped elements
   never leaving the loop. Anything asynchronous runs under one CPS
   resume-carrying frame whose resumes carry FETCH -> INVOKE ->
   VERDICT, the element held on the transform while its verdict
   settles. A dropped element loops by a tail call into FETCH, so an
   arbitrarily long dropped run stays flat.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosDropWhile {
  KosTransform base;
  uint8_t passed;   /* the predicate has rejected once; it is never
                       consulted again */
} KosDropWhile;


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   dropwhile-step frame refuses by name (see the kos_transform_stage
   call sites below and kos_dynframe_clone's resume refusal).

   The FETCH -> INVOKE -> VERDICT progression each step runs is
   carried by the resume set on the frame, not by a phase field: the
   resume function pointer is itself the state. */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult dropwhile_resume_fetch(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult dropwhile_resume_invoke(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult dropwhile_resume_verdict(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND_AS_IS(dropwhile_resume_fetch_kind, "dropwhile-step",
                dropwhile_resume_fetch, kos_transform_step_clear, NULL);
KOS_RESUME_KIND_AS_IS(dropwhile_resume_invoke_kind, "dropwhile-step",
                dropwhile_resume_invoke, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(dropwhile_resume_verdict_kind, "dropwhile-step",
                dropwhile_resume_verdict, kos_transform_step_clear, NULL);


/**
   Loop back to FETCH after a dropped element. A tail call, so an
   arbitrarily long dropped run stays flat.
*/
#define dropwhile_refetch(ctx, d) \
  RESUME_HOP(&dropwhile_resume_fetch_kind, (ctx), (d), NULL)


/**
   FETCH: request the next source element. Always entered with a NULL
   value -- either cold, from the driven-source install, or from
   dropwhile_refetch after a dropped element.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult dropwhile_resume_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransform *t = (KosTransform *) state;

  (void) value;

  if (PyErr_Occurred())
    return kos_transform_decline(ctx, t);

  {
    KosOpResult r = KosContext_NextResumeBound(ctx, &t->src,
                                            &dropwhile_resume_invoke_kind, t);
    RESULT_ENTRY(ctx, r, &dropwhile_resume_invoke_kind, t);
  }
}


/**
   INVOKE: test element against the predicate, unless the predicate
   has already been retired. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult dropwhile_resume_invoke(
    KosContext *ctx, void *state, PyObject *element) {

  KosDropWhile *d = (KosDropWhile *) state;
  KosTransform *t = &d->base;

  kos_transform_fetched(&t->src, element);

  if (! element)
    return kos_transform_decline(ctx, t);

  /* Retired predicate: every remaining element passes untested. */
  if (d->passed)
    RESULT_FRAME_RETURN(ctx, element, &t->cached_frame);

  /* pending steals the element's reference and holds it across the
     call; the argument vector borrows it for the call's duration. */
  t->pending = element;
  {
    KosOpResult r = KosContext_Call(
        ctx, t->fn.cs_callable, &element, 1, NULL, 0,
        &dropwhile_resume_verdict_kind, d);
    RESULT_ENTRY(ctx, r, &dropwhile_resume_verdict_kind, d);
  }
}


/**
   VERDICT: a true verdict drops the held element and fetches again, a
   false one retires the predicate and emits the element that broke
   the run. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult dropwhile_resume_verdict(
    KosContext *ctx, void *state, PyObject *value) {

  KosDropWhile *d = (KosDropWhile *) state;
  KosTransform *t = &d->base;
  int ok;

  if (! value)
    return kos_transform_decline(ctx, t);

  ok = PyObject_IsTrue(value);
  Py_DECREF(value);
  if (ok < 0)
    return EVAL_RAISE();

  if (ok) {
    Py_CLEAR(t->pending);
    dropwhile_refetch(ctx, d);
  }

  {
    PyObject *element = t->pending;

    d->passed = 1;
    t->pending = NULL;
    RESULT_FRAME_RETURN(ctx, element, &t->cached_frame);
  }
}


/* === the step === */


static KosIterStepResult dropwhile_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosDropWhile *d = (KosDropWhile *) self;
  KosTransform *t = &d->base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);

  {
    for (;;) {
      PyObject *element;

      KOS_TRANSFORM_FETCH(ctx, t, &t->src, &dropwhile_resume_fetch_kind, pre_top, out,
                          element);

      if (d->passed) {
        *out = element;
        return KOS_ITER_VALUE;
      }

      /* Any KosCallable predicate's verdict must come back to this
         step, so a resume-carrying frame installs here and the
         element is staged raw for dropwhile_resume_invoke, which
         makes the call in this evaluator. */
      if (t->fn.cs_kind != KOS_CALL_SHAPE_PLAIN)
        return kos_transform_stage(ctx, t, element,
                                   &dropwhile_resume_invoke_kind, pre_top);

      /* A plain python predicate needs no evaluator at all. */
      {
        PyObject *result = PyObject_CallOneArg(t->fn.cs_callable, element);

        if (! result) {
          Py_DECREF(element);
          return KOS_ITER_ERROR;
        }

        switch (KosEval_ResultIsValue(result)) {
          case 1: {
            int ok = PyObject_IsTrue(result);

            Py_DECREF(result);
            if (ok < 0) {
              Py_DECREF(element);
              return KOS_ITER_ERROR;
            }
            if (ok) {
              Py_DECREF(element);
              continue;
            }
            d->passed = 1;
            *out = element;
            return KOS_ITER_VALUE;
          }
          case 0: {
            /* Awaitable verdict: stage it raw on a resume-carrying
               frame, which owns every suspension. The element is held
               on the transform until the verdict settles. */
            KosIterStepResult staged = kos_transform_stage(
                ctx, t, result, &dropwhile_resume_verdict_kind, pre_top);

            if (staged != KOS_ITER_INSTALLED_VALUE) {
              Py_DECREF(element);
              return staged;
            }
            t->pending = element;  /* steals */
            return staged;
          }
          default:
            Py_DECREF(result);
            Py_DECREF(element);
            return KOS_ITER_ERROR;
        }
      }
    }
  }

}


static KosIterStepResult dropwhile_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, dropwhile_step_body);
}


/* === the type === */


static const KosTransformSpec dropwhile_spec = {
  .has_callable = 1,
  .none_means = KOS_TF_NONE_REJECT,
};


static PyObject *kosdropwhile_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  return kos_transform_new(type, args, kwds, &dropwhile_spec);
}


KosEvaluatedTypeObject KosDropWhileType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.dropwhile",
    .tp_basicsize = sizeof(KosDropWhile),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosDropWhile(pred, iterable) - native lazy dropwhile; "
              "iterable both asynchronously (ambient await, "
              "koskari-flattened) and synchronously (demand path)",
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kosdropwhile_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = dropwhile_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
