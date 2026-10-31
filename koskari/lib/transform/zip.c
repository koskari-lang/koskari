/* Included by transform.c -- zip */


/**
   koskari/lib/transform/zip.c

   KosZip: a native lazy zip over N sources, duck-typed against
   builtins.zip, implemented as a KosIter provider on the shared
   transform body (transform.c). A multi-source provider on the
   shared KosTransformMulti shape: the acquired iterators live in the
   request array that shape owns, published to the base through the
   sources view so the base closes them without knowing where they are
   kept, and the last row emitted is kept in that shape's row field, to
   be refilled in place for the next element while zip alone holds it.

   The fast path is pure C: when every source steps through
   tp_iternext the whole row is built in one loop with the evaluator
   untouched. When any source is driven, the row walk runs under one
   resume-carrying frame and is flattened -- a single loop over the
   sources, requesting a resume only for a source that actually
   suspends, and re-entering that same loop at base.idx afterwards.
   There is no provider per source, no frame per source, and no
   bounce through the trampoline per element. The partial row lives in
   base.pending, which ki_finish and the step abort already clear, so
   a suspension mid-row and an exhaustion mid-row both unwind with no
   code of this unit's own.

   Sources are acquired eagerly at construction: acquisition never
   suspends for any of the three input shapes, every source is reached
   on the first row, and a bad argument then raises where it is
   written rather than inside whatever drives the zip.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   zip-step frame refuses by name (see the kos_transform_stage call
   site below and kos_dynframe_clone's resume refusal).

   The row walk is carried by base.idx rather than by a resume per
   source: FETCH runs the loop and only leaves it for a source that
   suspends, and DELIVER files that source's element and hops straight
   back into the loop. */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_deliver(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_fetch(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND_AS_IS(zip_resume_deliver_kind, "zip-step",
                zip_resume_deliver, kos_transform_step_clear, NULL);
KOS_RESUME_KIND_AS_IS(zip_resume_fetch_kind, "zip-step",
                zip_resume_fetch, kos_transform_step_clear, NULL);


/**
   FETCH: request the source at base.idx; its element is filed by
   zip_resume_deliver, which re-enters here for the next, until the
   row is complete and delivered as the step's value.
   Always entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransformMulti *z = (KosTransformMulti *) state;
  KosTransform *t = &z->base;

  (void) value;

  if (PyErr_Occurred())
    return kos_transform_decline(ctx, t);

  if (t->idx < t->nsrc) {
    /* the element lands in zip_resume_deliver, which files it and
       hops back here; a spent source's StopIteration reaches the
       consumer's step resume, and the partial row is cleared by
       ki_finish */
    KosOpResult r = KosContext_NextResumeBound(
        ctx, &t->reqs[t->idx], &zip_resume_deliver_kind, z);
    RESULT_ENTRY(ctx, r, &zip_resume_deliver_kind, z);
  }

  {
    PyObject *row = t->pending;

    t->pending = NULL;
    kos_transform_row_retrack(row);
    RESULT_FRAME_RETURN(ctx, row, &t->cached_frame);
  }
}


/**
   DELIVER: a driven source produced its element. File it at the
   position the walk stopped on and hop back into the walk. A tail
   call, so a row of driven sources stays flat. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_deliver(
    KosContext *ctx, void *state, PyObject *element) {

  KosTransformMulti *z = (KosTransformMulti *) state;
  KosTransform *t = &z->base;

  kos_transform_fetched(&t->reqs[t->idx], element);

  if (! element)
    return kos_transform_decline(ctx, t);

  kos_transform_row_set(t->pending, (Py_ssize_t) t->idx, element);
  t->idx++;

  RESUME_HOP(&zip_resume_fetch_kind, ctx, z, NULL);
}


/* === the step === */


static KosIterStepResult zip_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransformMulti *z = (KosTransformMulti *) self;
  KosTransform *t = &z->base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);
  PyObject *row;

  /* zip over no sources is empty, as builtins.zip() is. */
  if (! t->nsrc) {
    t->closed = 1;
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  /* The row: the last one emitted, refilled in place while zip alone
     holds it, which is the case once the consumer has read and
     released it; a row the consumer kept is left to it and a fresh one
     takes its place. The step holds one reference to the row it
     fills, z->row the other. */
  row = z->row;
  if (row && Py_REFCNT(row) == 1) {
    Py_INCREF(row);
  } else {
    row = PyTuple_New((Py_ssize_t) t->nsrc);
    if (! row)
      return KOS_ITER_ERROR;
    Py_INCREF(row);
    Py_XSETREF(z->row, row);
  }

  {
    uint16_t i;

    /* The row fills here for as long as the sources answer without
       the evaluator; the first driven source defers the rest of the
       walk to zip_resume_fetch, with the row pending from its
       position. A reused row that stops short keeps a mix of old and
       new items until the next refill or its release. */
    for (i = 0; i < t->nsrc; i++) {
      PyObject *element;

      switch (KosContext_NextOrDefer(ctx, &t->reqs[i],
                                       &zip_resume_fetch_kind, z,
                                       &t->cached_frame, &element)) {
        case KOS_IREQ_DELIVERED:
          kos_transform_row_set(row, (Py_ssize_t) i, element);
          continue;
        case KOS_IREQ_EXHAUSTED:
          Py_DECREF(row);
          t->closed = 1;
          *out = NULL;
          return KOS_ITER_EXHAUSTED;
        case KOS_IREQ_CONTINUES:
          t->idx = i;
          t->pending = row;  /* steals */
          return KOS_ITER_INSTALLED_VALUE;
        default:
          Py_DECREF(row);
          return kos_transform_step_abort(ctx, t, pre_top);
      }
    }

    kos_transform_row_retrack(row);
    *out = row;
    return KOS_ITER_VALUE;
  }
}


static KosIterStepResult zip_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, zip_step_body);
}


/* === the type === */


static PyObject *koszip_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  KosTransformMulti *z;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "zip takes no keyword arguments");
    return NULL;
  }

  if (PyTuple_GET_SIZE(args) > UINT16_MAX) {
    PyErr_SetString(PyExc_TypeError, "zip takes at most 65535 iterables");
    return NULL;
  }

  z = (KosTransformMulti *) type->tp_alloc(type, 0);
  if (! z)
    return NULL;

  if (kos_transform_init_multi(z, args)) {
    Py_DECREF(z);
    return NULL;
  }

  return (PyObject *) z;
}


KosEvaluatedTypeObject KosZipType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.zip",
    .tp_basicsize = sizeof(KosTransformMulti),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosZip(*iterables) - native lazy zip; iterable both "
              "asynchronously (ambient await, koskari-flattened) and "
              "synchronously (demand path)",
    .tp_traverse = kos_transform_multi_traverse,
    .tp_clear = kos_transform_multi_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = koszip_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = zip_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
