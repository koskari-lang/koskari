/* Included by transform.c -- filter */


/**
   koskari/lib/transform/filter.c

   KosFilter: a native lazy filter over one source, duck-typed against
   builtins.filter, implemented as a KosIter provider on the shared
   transform body (transform.c). This unit holds only the filter step
   and the resume chain it arms.

   The fast path is pure C: a sync source element and an immediate
   verdict -- the element's own truthiness for a None predicate, or a
   non-suspending predicate result -- emit or skip with the evaluator
   untouched, skipped elements never leaving the loop. Anything
   asynchronous runs under one CPS resume-carrying frame, whose
   resumes carry the three-phase progression -- FETCH requests the
   next source element (KosContext_NextResume), INVOKE applies the
   predicate (KosContext_Call) with the element held on the
   filter, VERDICT tests the predicate's result and either completes
   the frame with the held element or fetches again. A rejected
   element loops by a tail call into FETCH, never by C
   recursion, so an arbitrarily long run of rejects stays flat. The
   frame is cached on the filter between steps
   (KosNativeFrame_ReturnToCache), stripped inert so the cache holds no
   references. Exhaustion and errors leave the frame as exceptions;
   the consumer's step resume converts StopIteration per its own
   rules.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosFilter {
  KosTransform base;
  uint8_t invert;   /* filterfalse: the verdict selects what filter
                       would drop */
} KosFilter;


/**
   Does this verdict keep the element? filterfalse is filter with the
   answer turned over, which is the whole of the difference between
   them.
*/
static inline int filter_keeps(KosFilter *f, int ok) {

  return f->invert ? ! ok : ok;
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   filter-step frame refuses by name (see the kos_transform_stage call
   sites below and kos_dynframe_clone's resume refusal).

   The FETCH -> INVOKE -> VERDICT progression each step runs is
   carried by the resume set on the frame, not by a phase field: the
   resume function pointer is itself the state. */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_after_verdict(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_do_fetch(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_do_invoke(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND(filter_resume_after_verdict_kind, "filter-step",
                filter_resume_after_verdict, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(filter_resume_do_fetch_kind, "filter-step",
                filter_resume_do_fetch, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(filter_resume_do_invoke_kind, "filter-step",
                filter_resume_do_invoke, kos_transform_step_clear, NULL);


/**
   Loop back to FETCH after a rejected element. A tail call, so an
   arbitrarily long reject run stays flat: each rejection reaches the
   next fetch by discarding its own frame, never by nesting, and never
   by a bounce through the trampoline.
*/
#define filter_refetch(ctx, f) \
  RESUME_HOP(&filter_resume_do_fetch_kind, (ctx), (f), NULL)


/**
   FETCH: request the next source element. Always entered with a NULL
   value -- either cold, from the driven-source install, or from
   filter_refetch after a rejected element. A NULL with an exception
   already set is the raise walk propagating that exception; this step
   always lets it propagate (the consumer's step resume owns the
   conversion, StopIteration truncation parity included).
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_do_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransform *f = (KosTransform *) state;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  {
    KosOpResult r = KosContext_NextResume(ctx, f->source,
                                            &filter_resume_do_invoke_kind, f);
    RESULT_ENTRY(ctx, r, &filter_resume_do_invoke_kind, f);
  }
}


/**
   INVOKE: test element against the predicate. An immediate verdict
   resolves inline; a frame-wanting predicate goes through CallResult,
   the element held on the filter until its verdict settles. element
   is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_do_invoke(
    KosContext *ctx, void *state, PyObject *element) {

  KosTransform *f = (KosTransform *) state;

  kos_transform_fetched(f->source, element);

  if (! element)
    return EVAL_RAISE();

  /* None predicate: the element's own truthiness is the verdict. */
  if (! f->fn.cs_callable) {
    int ok = PyObject_IsTrue(element);

    if (ok < 0) {
      Py_DECREF(element);
      return EVAL_RAISE();
    }
    if (filter_keeps((KosFilter *) state, ok))
      return KosNativeFrame_ReturnToCache(ctx, element, &f->cached_frame);

    Py_DECREF(element);
    filter_refetch(ctx, f);
  }

  /* Every calling shape goes through the one entry --
     KosContext_Call resolves the predicate and prepares (or declines
     to prepare) a frame per its own suite, so a predicate that wants
     no frame still runs in this evaluator rather than through the
     Python boundary. pending steals the element's reference and holds
     it across the call; the argument vector borrows it for the call's
     duration. */
  f->pending = element;
  {
    KosOpResult r = KosContext_Call(
        ctx, f->fn.cs_callable, &element, 1, NULL, 0,
        &filter_resume_after_verdict_kind, f);
    RESULT_ENTRY(ctx, r, &filter_resume_after_verdict_kind, f);
  }
}


/**
   VERDICT: a true verdict completes the step with the held element, a
   false one releases it and fetches again. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult filter_resume_after_verdict(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransform *f = (KosTransform *) state;
  int ok;

  if (! value)
    return EVAL_RAISE();

  ok = PyObject_IsTrue(value);
  Py_DECREF(value);
  if (ok < 0)
    return EVAL_RAISE();

  if (filter_keeps((KosFilter *) state, ok)) {
    PyObject *element = f->pending;

    f->pending = NULL;
    return KosNativeFrame_ReturnToCache(ctx, element, &f->cached_frame);
  }

  Py_CLEAR(f->pending);
  filter_refetch(ctx, f);
}


/* === the step === */


static KosIterStepResult filter_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransform *f = (KosTransform *) self;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);

  {
    for (;;) {
      PyObject *element;

      KOS_TRANSFORM_FETCH(ctx, f, f->source, &filter_resume_do_fetch_kind, pre_top, out,
                          element);

      if (! f->fn.cs_callable) {
        int ok = PyObject_IsTrue(element);

        if (ok < 0) {
          Py_DECREF(element);
          return KOS_ITER_ERROR;
        }
        if (filter_keeps((KosFilter *) self, ok)) {
          *out = element;
          return KOS_ITER_VALUE;
        }
        Py_DECREF(element);
        continue;
      }

      /* Any KosCallable predicate's verdict must come back to this
         step, so a resume-carrying frame installs here and the element
         is staged raw for filter_resume_do_invoke, which makes the
         call in this evaluator. filter_iter_step runs before
         koskari_eval drives this chain (see stepper_step), so the call
         itself -- which can install a callee or need an await -- must
         happen from inside that later dispatch, not here. */
      if (f->fn.cs_kind != KOS_CALL_SHAPE_PLAIN)
        return kos_transform_stage(ctx, f, element,
                                   &filter_resume_do_invoke_kind, pre_top);

      /* A plain python predicate needs no evaluator at all. */
      {
        PyObject *result = PyObject_CallOneArg(f->fn.cs_callable, element);

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
            if (filter_keeps((KosFilter *) self, ok)) {
              *out = element;
              return KOS_ITER_VALUE;
            }
            Py_DECREF(element);
            continue;
          }
          case 0: {
            /* Awaitable verdict: stage it raw on a resume-carrying
               frame, which owns every suspension; the await itself
               happens from inside koskari_eval's own dispatch. The
               element is held on the filter until the verdict
               settles. */
            KosIterStepResult staged = kos_transform_stage(
                ctx, f, result, &filter_resume_after_verdict_kind, pre_top);

            if (staged != KOS_ITER_INSTALLED_VALUE) {
              Py_DECREF(element);
              return staged;
            }
            f->pending = element;  /* steals */
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


static KosIterStepResult filter_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, filter_step_body);
}


/* === the type === */


static const KosTransformSpec filter_spec = {
  .has_callable = 1,
  .none_means = KOS_TF_NONE_BARE,
};


static PyObject *kosfilter_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  return kos_transform_new(type, args, kwds, &filter_spec);
}


/**
   filterfalse(func, iterable): a filter whose verdict is turned over.
   It is a subtype of filter with one flag set at construction rather
   than a provider of its own: the step reads the flag, and the type
   exists so that messages and the repr carry its name.
*/
static PyObject *kosfilterfalse_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *made = kos_transform_new(type, args, kwds, &filter_spec);

  if (made)
    ((KosFilter *) made)->invert = 1;
  return made;
}


KosEvaluatedTypeObject KosFilterType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.filter",
    .tp_basicsize = sizeof(KosFilter),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosFilter(func, iterable) - native lazy filter; a None "
              "func keeps truthy elements; iterable both asynchronously "
              "(ambient await, koskari-flattened) and synchronously "
              "(demand path)",
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kosfilter_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = filter_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


/* A static subtype resolves to its own extended type object, so it
   carries the filter suite itself. */
KosEvaluatedTypeObject KosFilterFalseType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.filterfalse",
    .tp_basicsize = sizeof(KosFilter),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
    .tp_doc = "KosFilterFalse(func, iterable) - native lazy filterfalse; "
              "keeps what filter would drop. A None func keeps falsey "
              "elements",
    .tp_new = kosfilterfalse_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosFilterType),
  },
  .kos_iter = {
    .ki_step = filter_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
