/* Included by transform.c -- compress */


/**
   koskari/lib/transform/compress.c

   KosCompress: a native lazy compress over a data source and a
   selector source, duck-typed against itertools.compress, implemented
   as a KosIter provider on the shared transform body (transform.c).
   This unit holds only the compress step and the resume chain it arms.

   Two sources, both live, and no callable: each row takes one element
   from each and emits the data element when its selector is truthy.
   Either source running out ends the compress, so exhaustion
   propagates and this provider needs none of the interception batched
   and zip-longest do.

   The data element waits in base.pending while its selector is
   fetched, which is within one step, so the per-step slot is its home.
   base.idx says which source the walk is on. A rejected row loops
   without leaving the step, so an arbitrarily long run of falsey
   selectors costs no frames on the sync path and one tail call each
   on the driven one.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#define COMPRESS_DATA 0
#define COMPRESS_SELECTOR 1


/**
   Is the data element kept? The selector is consumed either way.
*/
static int compress_keeps(PyObject *selector) {

  int ok = PyObject_IsTrue(selector);

  Py_DECREF(selector);
  return ok;
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   compress-step frame refuses by name (see the kos_transform_stage
   call site below and kos_dynframe_clone's resume refusal).

   The row is carried by base.idx rather than by a resume per source:
   FETCH runs the loop and only leaves it for a source that suspends,
   and DELIVER files that source's element and hops back in. */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_deliver(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_fetch(
    KosContext *ctx, void *state, PyObject *value);

KOS_TRANSFORM_STEP_KIND_AS_IS(compress_resume_deliver_kind, "compress-step",
    compress_resume_deliver);
KOS_TRANSFORM_STEP_KIND_AS_IS(compress_resume_fetch_kind, "compress-step",
    compress_resume_fetch);


/**
   FETCH: take a data element, then its selector, emitting the data
   when the selector is truthy and looping when it is not. Always
   entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransformMulti *c = (KosTransformMulti *) state;
  KosTransform *t = &c->base;

  (void) value;

  if (PyErr_Occurred())
    return kos_transform_decline(ctx, t);

  /* the element lands in compress_resume_deliver, which files it
     and hops back here; either source running out ends the
     compress through its StopIteration */
  {
    KosOpResult r = KosContext_NextResumeBound(
        ctx, &t->reqs[t->idx], &compress_resume_deliver_kind, c);
    RESULT_ENTRY(ctx, r, &compress_resume_deliver_kind, c);
  }
}


/**
   DELIVER: a driven source produced its element for this row. File it
   and hop back into the row. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_deliver(
    KosContext *ctx, void *state, PyObject *element) {

  KosTransformMulti *c = (KosTransformMulti *) state;
  KosTransform *t = &c->base;

  kos_transform_fetched(&t->reqs[t->idx], element);

  if (! element)
    return kos_transform_decline(ctx, t);

  if (t->idx == COMPRESS_DATA) {
    t->pending = element;  /* steals */
    t->idx = COMPRESS_SELECTOR;
    RESUME_HOP(&compress_resume_fetch_kind, ctx, c, NULL);
  }

  {
    int ok = compress_keeps(element);

    if (ok < 0)
      return EVAL_RAISE();

    t->idx = COMPRESS_DATA;
    if (ok) {
      PyObject *kept = t->pending;

      t->pending = NULL;
      RESULT_FRAME_RETURN(ctx, kept, &t->cached_frame);
    }
    Py_CLEAR(t->pending);
  }

  RESUME_HOP(&compress_resume_fetch_kind, ctx, c, NULL);
}


/* === the step === */


static KosIterStepResult compress_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransformMulti *c = (KosTransformMulti *) self;
  KosTransform *t = &c->base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);

  t->idx = COMPRESS_DATA;

  {
    /* Rejected rows never leave this loop while both sources answer
       without the evaluator; a driven source defers the rest to
       compress_resume_fetch, the data element pending when the
       selector is the one deferred. */
    for (;;) {
      PyObject *data;
      PyObject *selector;
      int ok;

      KOS_TRANSFORM_FETCH(ctx, t, &t->reqs[COMPRESS_DATA],
                          &compress_resume_fetch_kind, pre_top, out, data);

      switch (kos_transform_next_or_defer(
          ctx, t, &t->reqs[COMPRESS_SELECTOR], &compress_resume_fetch_kind,
          &selector)) {
        case KOS_IREQ_DELIVERED:
          break;
        case KOS_IREQ_EXHAUSTED:
          Py_DECREF(data);
          t->closed = 1;
          *out = NULL;
          return KOS_ITER_EXHAUSTED;
        case KOS_IREQ_CONTINUES:
          t->pending = data;  /* steals; held for the selector */
          t->idx = COMPRESS_SELECTOR;
          return KOS_ITER_INSTALLED_VALUE;
        default:
          Py_DECREF(data);
          return kos_transform_step_abort(ctx, t, pre_top);
      }

      ok = compress_keeps(selector);
      if (ok < 0) {
        Py_DECREF(data);
        return KOS_ITER_ERROR;
      }
      if (ok) {
        *out = data;
        return KOS_ITER_VALUE;
      }
      Py_DECREF(data);
    }
  }
}


static KosIterStepResult compress_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, compress_step_body);
}


/* === the type === */


static PyObject *koscompress_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *data = NULL;
  PyObject *selectors = NULL;
  KosTransformMulti *c;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "compress takes no keyword arguments");
    return NULL;
  }

  if (! PyArg_UnpackTuple(args, "compress", 2, 2, &data, &selectors))
    return NULL;

  c = (KosTransformMulti *) type->tp_alloc(type, 0);
  if (! c)
    return NULL;

  /* the two arguments are the two sources, data first */
  if (kos_transform_init_multi(c, args)) {
    Py_DECREF(c);
    return NULL;
  }

  return (PyObject *) c;
}


KosEvaluatedTypeObject KosCompressType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.compress",
    .tp_basicsize = sizeof(KosTransformMulti),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosCompress(data, selectors) - native lazy compress; "
              "iterable both asynchronously (ambient await, "
              "koskari-flattened) and synchronously (demand path)",
    .tp_traverse = kos_transform_multi_traverse,
    .tp_clear = kos_transform_multi_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = koscompress_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = compress_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
