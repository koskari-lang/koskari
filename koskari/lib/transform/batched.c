/* Included by transform.c -- batched */


/**
   koskari/lib/transform/batched.c

   KosBatched: a native lazy batched over one source, duck-typed
   against itertools.batched, implemented as a KosIter provider on the
   shared transform body (transform.c). This unit holds only the
   batched step and the resume chain it arms.

   One step fills one batch, so the batch under construction lives in
   base.pending -- which ki_finish and the step abort already clear --
   and only the fill position and the batch size are this provider's
   own fields. The position is a Py_ssize_t rather than the base's
   resume index because a batch size is not bounded by it.

   A step fetches up to n times, so the driven path is the flattened
   walk zip's row uses: one loop that leaves only for a fetch that
   suspends and re-enters where it left off. When a source runs out
   part way through a batch the resume handles the pending
   StopIteration rather than propagating it: the short batch is the
   step's value, and the batch after it is the exhaustion.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosBatched {
  KosTransform base;
  Py_ssize_t n;       /* elements per batch */
  Py_ssize_t filled;  /* elements already in the batch under
                         construction, which lives in base.pending */
} KosBatched;


/**
   Close out a batch the source ended part way through: trim the
   over-allocated tuple to what actually arrived and mark the provider
   spent, so the step after this one reports exhaustion.

   :return: the short batch, or NULL with PyErr set
*/
static PyObject *batched_short(KosBatched *b) {

  KosTransform *t = &b->base;
  PyObject *batch = t->pending;
  PyObject *shorter = PyTuple_GetSlice(batch, 0, b->filled);

  t->pending = NULL;
  Py_DECREF(batch);
  t->closed = 1;

  return shorter;
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   batched-step frame refuses by name (see the kos_transform_stage
   call site below and kos_dynframe_clone's resume refusal).

   The fill is carried by b->filled rather than by a resume per
   element: FILL runs the loop and only leaves it for a fetch that
   suspends, and DELIVER files that element and hops back in. */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult batched_resume_deliver(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult batched_resume_fill(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND_AS_IS(batched_resume_deliver_kind, "batched-step",
                batched_resume_deliver, kos_transform_step_clear, NULL);
KOS_RESUME_KIND_AS_IS(batched_resume_fill_kind, "batched-step",
                batched_resume_fill, kos_transform_step_clear, NULL);


/**
   FILL: take elements until the batch is full, then deliver it as the
   step's value. Always entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult batched_resume_fill(
    KosContext *ctx, void *state, PyObject *value) {

  KosBatched *b = (KosBatched *) state;
  KosTransform *t = &b->base;

  (void) value;

  if (PyErr_Occurred())
    return kos_transform_decline(ctx, t);

  /* Entered from the step's deferred fetch, with the batch so far
     pending, and re-entered from batched_resume_deliver: every fetch
     here is a request. */
  while (b->filled < b->n) {
    KosOpResult r = KosContext_NextResumeBound(ctx, &t->src,
                                            &batched_resume_deliver_kind, b);
    RESULT_ENTRY(ctx, r, &batched_resume_deliver_kind, b);
  }

  {
    PyObject *batch = t->pending;

    t->pending = NULL;
    RESULT_FRAME_RETURN(ctx, batch, &t->cached_frame);
  }
}


/**
   DELIVER: a driven source either produced the next element of the
   batch, or ran out. Running out part way through is not a failure of
   this step -- the batch so far is its value -- so the pending
   StopIteration is handled here rather than propagated. element is
   consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult batched_resume_deliver(
    KosContext *ctx, void *state, PyObject *element) {

  KosBatched *b = (KosBatched *) state;
  KosTransform *t = &b->base;

  kos_transform_fetched(&t->src, element);

  if (! element) {
    PyObject *shorter;

    if (! b->filled || ! kos_transform_source_spent())
      return kos_transform_decline(ctx, t);

    PyErr_Clear();
    shorter = batched_short(b);
    if (! shorter)
      return EVAL_RAISE();

    RESULT_FRAME_RETURN(ctx, shorter, &t->cached_frame);
  }

  PyTuple_SET_ITEM(t->pending, b->filled, element);
  b->filled++;

  RESUME_HOP(&batched_resume_fill_kind, ctx, b, NULL);
}


/* === the step === */


static KosIterStepResult batched_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosBatched *b = (KosBatched *) self;
  KosTransform *t = &b->base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);
  PyObject *batch;

  batch = PyTuple_New(b->n);
  if (! batch)
    return KOS_ITER_ERROR;

  b->filled = 0;

  {
    Py_ssize_t i;

    for (i = 0; i < b->n; i++) {
      PyObject *element;

      switch (KosContext_NextOrDefer(ctx, &t->src,
                                       &batched_resume_fill_kind, b,
                                       &t->cached_frame, &element)) {
        case KOS_IREQ_DELIVERED:
          PyTuple_SET_ITEM(batch, i, element);
          continue;
        case KOS_IREQ_EXHAUSTED:
          if (! i) {
            Py_DECREF(batch);
            t->closed = 1;
            *out = NULL;
            return KOS_ITER_EXHAUSTED;
          }
          t->pending = batch;
          b->filled = i;
          *out = batched_short(b);
          return *out ? KOS_ITER_VALUE : KOS_ITER_ERROR;
        case KOS_IREQ_CONTINUES:
          /* the fill goes on in batched_resume_fill from here */
          t->pending = batch;  /* steals */
          b->filled = i;
          return KOS_ITER_INSTALLED_VALUE;
        default:
          Py_DECREF(batch);
          return kos_transform_step_abort(ctx, t, pre_top);
      }
    }

    *out = batch;
    return KOS_ITER_VALUE;
  }
}


static KosIterStepResult batched_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, batched_step_body);
}


/* === the type === */


static PyObject *kosbatched_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *iterable = NULL;
  PyObject *count = NULL;
  KosBatched *b;
  Py_ssize_t n;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "batched takes no keyword arguments");
    return NULL;
  }

  if (! PyArg_UnpackTuple(args, "batched", 2, 2, &iterable, &count))
    return NULL;

  n = PyNumber_AsSsize_t(count, PyExc_OverflowError);
  if (n == -1 && PyErr_Occurred())
    return NULL;

  if (n < 1) {
    PyErr_SetString(PyExc_ValueError, "n must be at least one");
    return NULL;
  }

  b = (KosBatched *) type->tp_alloc(type, 0);
  if (! b)
    return NULL;

  if (kos_transform_init_one(&b->base, NULL, iterable)) {
    Py_DECREF(b);
    return NULL;
  }

  b->n = n;
  b->filled = 0;

  return (PyObject *) b;
}


KosEvaluatedTypeObject KosBatchedType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.batched",
    .tp_basicsize = sizeof(KosBatched),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosBatched(iterable, n) - native lazy batched; iterable "
              "both asynchronously (ambient await, koskari-flattened) "
              "and synchronously (demand path)",
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kosbatched_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = batched_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
