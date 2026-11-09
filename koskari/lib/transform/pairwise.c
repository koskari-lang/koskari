/* Included by transform.c -- pairwise */


/**
   koskari/lib/transform/pairwise.c

   KosPairwise: a native lazy pairwise over one source, duck-typed
   against itertools.pairwise, implemented as a KosIter provider on
   the shared transform body (transform.c). This unit holds only the
   pairwise step and the resume chain it arms.

   A provider with no callable at all: the argument tuple is
   just the source, and nothing is applied per element. The previous
   element outlives the step that fetched it, so it is this provider's
   own field rather than base.pending, which ki_finish clears at every
   step completion.

   The first step fetches twice and every step after it fetches once,
   so the driven path is the same flattened shape zip uses: one loop
   that leaves only for a fetch that suspends and re-enters where it
   left off, the held element deciding which of the two it is on.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosPairwise {
  KosTransform base;
  PyObject *held;   /* the previous element, paired with the next one;
                       NULL until the first fetch primes it */
} KosPairwise;


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   pairwise-step frame refuses by name (see the kos_transform_stage
   call site below and kos_dynframe_clone's resume refusal). */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult pairwise_resume_deliver(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult pairwise_resume_fetch(
    KosContext *ctx, void *state, PyObject *value);

KOS_TRANSFORM_STEP_KIND_AS_IS(pairwise_resume_deliver_kind, "pairwise-step",
    pairwise_resume_deliver);
KOS_TRANSFORM_STEP_KIND_AS_IS(pairwise_resume_fetch_kind, "pairwise-step",
    pairwise_resume_fetch);


/**
   FETCH: request the next source element. Always entered with a NULL
   value -- either cold, from the driven-source install, or from
   pairwise_resume_deliver after the fetch that primed the pair:
   every fetch here is a request.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult pairwise_resume_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransform *t = (KosTransform *) state;

  (void) value;

  if (PyErr_Occurred())
    return kos_transform_decline(ctx, t);

  {
    KosOpResult r = KosContext_NextResumeBound(ctx, &t->src,
                                            &pairwise_resume_deliver_kind, t);
    RESULT_ENTRY(ctx, r, &pairwise_resume_deliver_kind, t);
  }
}


/**
   DELIVER: the first element of a cold step primes the pair and
   fetches again; any later one completes the step as the second half
   of a pair and becomes the held element. element is consumed.

   A NULL element arrives with the source's exception pending. When
   that is StopIteration the source is spent: release the held element
   and mark the provider closed, as the sync branch of the step does,
   before the raise carries the exhaustion to the stepping node.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult pairwise_resume_deliver(
    KosContext *ctx, void *state, PyObject *element) {

  KosPairwise *p = (KosPairwise *) state;
  KosTransform *t = &p->base;
  PyObject *row;

  kos_transform_fetched(&t->src, element);

  if (! element) {
    if (kos_transform_source_spent()) {
      Py_CLEAR(p->held);
      t->closed = 1;
    }
    return kos_transform_decline(ctx, t);
  }

  if (! p->held) {
    p->held = element;  /* steals */
    RESUME_HOP(&pairwise_resume_fetch_kind, ctx, p, NULL);
  }

  row = PyTuple_Pack(2, p->held, element);
  if (! row) {
    Py_DECREF(element);
    return EVAL_RAISE();
  }

  Py_DECREF(p->held);
  p->held = element;  /* steals */

  RESULT_FRAME_RETURN(ctx, row, &t->cached_frame);
}


/* === the step === */


static KosIterStepResult pairwise_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosPairwise *p = (KosPairwise *) self;
  KosTransform *t = &p->base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);

  {
    PyObject *element;
    PyObject *row;

    /* Prime the pair on the first step. A source of fewer than two
       elements never yields, exactly as Python's does. */
    if (! p->held) {
      KOS_TRANSFORM_FETCH(ctx, t, &t->src, &pairwise_resume_fetch_kind,
                          pre_top, out, p->held);
    }

    /* The fetch runs the source's __next__, which may step this
       pairwise again; an inner step that exhausts the source clears
       p->held, so this step pairs from its own reference to it. */
    PyObject *held = p->held;
    Py_INCREF(held);

    switch (kos_transform_next_or_defer(
        ctx, t, &t->src, &pairwise_resume_fetch_kind, &element)) {
      case KOS_IREQ_DELIVERED:
        break;
      case KOS_IREQ_EXHAUSTED:
        Py_DECREF(held);
        Py_CLEAR(p->held);
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      case KOS_IREQ_CONTINUES:
        /* the held element stays primed for pairwise_resume_deliver */
        Py_DECREF(held);
        return KOS_ITER_INSTALLED_VALUE;
      default:
        Py_DECREF(held);
        return kos_transform_step_abort(ctx, t, pre_top);
    }

    row = PyTuple_Pack(2, held, element);
    Py_DECREF(held);
    if (! row) {
      Py_DECREF(element);
      return KOS_ITER_ERROR;
    }

    Py_XSETREF(p->held, element);  /* steals */

    *out = row;
    return KOS_ITER_VALUE;
  }

}


static KosIterStepResult pairwise_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, pairwise_step_body);
}


/* === the type === */


static const KosTransformSpec pairwise_spec = {
  .has_callable = 0,
  .none_means = KOS_TF_NONE_REJECT,
};


static PyObject *kospairwise_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  return kos_transform_new(type, args, kwds, &pairwise_spec);
}


static int pairwise_traverse(PyObject *self, visitproc visit, void *arg) {

  KosPairwise *p = (KosPairwise *) self;

  Py_VISIT(p->held);
  return kos_transform_traverse(self, visit, arg);
}


static int pairwise_clear(PyObject *self) {

  KosPairwise *p = (KosPairwise *) self;

  Py_CLEAR(p->held);
  return kos_transform_clear(self);
}


KosEvaluatedTypeObject KosPairwiseType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.pairwise",
    .tp_basicsize = sizeof(KosPairwise),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosPairwise(iterable) - native lazy pairwise; iterable "
              "both asynchronously (ambient await, koskari-flattened) "
              "and synchronously (demand path)",
    .tp_traverse = pairwise_traverse,
    .tp_clear = pairwise_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kospairwise_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = pairwise_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
