/* Included by transform.c -- zip_longest */


/**
   koskari/lib/transform/zip_longest.c

   KosZipLongest: a native lazy zip-longest over N sources, duck-typed
   against itertools.zip_longest, implemented as a KosIter provider on
   the shared transform body (transform.c). This unit holds only the
   zip-longest step and the resume chain it arms.

   Where zip ends at its first spent source, this one replaces that
   source with the fill value and carries on until every source is
   spent. A spent source's view slot is cleared to NULL, which the
   base's close loop skips, so the provider tracks only how many
   remain live.

   The provider catches a source running out rather than propagating
   it: exhaustion arrives at the deliver resume as a NULL with
   StopIteration pending, and the resume files the fill value and
   carries the row on. The row itself is the flattened walk zip uses --
   one loop over the sources that leaves only for a fetch that suspends
   and re-enters at base.idx -- with the partial row in base.pending,
   which ki_finish and the step abort already clear.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosZipLongest {
  KosTransformMulti multi;  /* a spent source's request in multi.srcs
                               has its iterator released in place; the
                               array is private to this object and
                               never moves */
  PyObject *fill;      /* value standing in for a spent source */
  uint16_t live;       /* sources not yet spent */
} KosZipLongest;


/**
   Retire source i: drop the iterator, clear its view slot, and say
   whether any source is still live. A cleared slot reads as spent on
   every later row and is skipped by the base's close loop.

   :return: 1 while a source remains live, 0 once every one is spent
*/
static int zip_longest_retire(KosZipLongest *z, uint16_t i) {

  Py_CLEAR(z->multi.base.reqs[i].iter);
  z->live--;

  return z->live != 0;
}


/**
   File the fill value at position i of row.
*/
static void zip_longest_fill(KosZipLongest *z, PyObject *row, uint16_t i) {

  Py_INCREF(z->fill);
  PyTuple_SET_ITEM(row, (Py_ssize_t) i, z->fill);
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   zip-longest-step frame refuses by name (see the kos_transform_stage
   call site below and kos_dynframe_clone's resume refusal).

   The row walk is carried by base.idx rather than by a resume per
   source: FETCH runs the loop and only leaves it for a source that
   suspends, and DELIVER files that source's element -- or its fill
   value, when the source ran out -- and hops straight back in. */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_longest_resume_deliver(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_longest_resume_fetch(
    KosContext *ctx, void *state, PyObject *value);

KOS_TRANSFORM_STEP_KIND_AS_IS(zip_longest_resume_deliver_kind,
    "zip-longest-step", zip_longest_resume_deliver);
KOS_TRANSFORM_STEP_KIND_AS_IS(zip_longest_resume_fetch_kind,
    "zip-longest-step", zip_longest_resume_fetch);


/**
   FETCH: walk the sources from base.idx, filing each element into the
   row. A spent source contributes its fill value without a fetch;
   a live one is requested, its element filed by
   zip_longest_resume_deliver, which re-enters here. Always entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_longest_resume_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosZipLongest *z = (KosZipLongest *) state;
  KosTransform *t = &z->multi.base;

  (void) value;

  if (PyErr_Occurred())
    return kos_transform_decline(ctx, t);

  while (t->idx < t->nsrc) {
    if (! t->reqs[t->idx].iter) {
      zip_longest_fill(z, t->pending, t->idx);
      t->idx++;
      continue;
    }

    /* the element, or the source's exhaustion, goes to
       zip_longest_resume_deliver, which files it and hops back here */
    {
      KosOpResult r = KosContext_NextResumeBound(
          ctx, &t->reqs[t->idx], &zip_longest_resume_deliver_kind, z);
      RESULT_ENTRY(ctx, r, &zip_longest_resume_deliver_kind, z);
    }
  }

  {
    PyObject *row = t->pending;

    t->pending = NULL;
    RESULT_FRAME_RETURN(ctx, row, &t->cached_frame);
  }
}


/**
   DELIVER: a driven source either produced its element for this row,
   or ran out. Running out is not a failure of this step while another
   source is live -- the fill value stands in its place -- so the
   pending StopIteration is handled here rather than propagated.
   element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_longest_resume_deliver(
    KosContext *ctx, void *state, PyObject *element) {

  KosZipLongest *z = (KosZipLongest *) state;
  KosTransform *t = &z->multi.base;

  kos_transform_fetched(&t->reqs[t->idx], element);

  if (! element) {
    if (! kos_transform_source_spent())
      return kos_transform_decline(ctx, t);

    /* the StopIteration already pending ends it */
    if (! zip_longest_retire(z, t->idx))
      return kos_transform_decline(ctx, t);

    PyErr_Clear();
    zip_longest_fill(z, t->pending, t->idx);
    t->idx++;

    RESUME_HOP(&zip_longest_resume_fetch_kind, ctx, z, NULL);
  }

  PyTuple_SET_ITEM(t->pending, (Py_ssize_t) t->idx, element);
  t->idx++;

  RESUME_HOP(&zip_longest_resume_fetch_kind, ctx, z, NULL);
}


/* === the step === */


static KosIterStepResult zip_longest_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosZipLongest *z = (KosZipLongest *) self;
  KosTransform *t = &z->multi.base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);
  PyObject *row;

  /* zip-longest over no sources is empty, as itertools' is. */
  if (! t->nsrc || ! z->live) {
    t->closed = 1;
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  row = PyTuple_New((Py_ssize_t) t->nsrc);
  if (! row)
    return KOS_ITER_ERROR;

  {
    uint16_t i;

    /* The row stays local while the sources answer without the
       evaluator: a fetch runs a source's __next__, which may step
       this zip-longest again, and an inner step would hand out and
       clear a row kept in t->pending. The first driven source
       defers the rest of the walk to zip_longest_resume_fetch, and
       only then is the row pending, from that position. */
    for (i = 0; i < t->nsrc; i++) {
      PyObject *element;

      if (! t->reqs[i].iter) {
        zip_longest_fill(z, row, i);
        continue;
      }

      switch (kos_transform_next_or_defer(
          ctx, t, &t->reqs[i], &zip_longest_resume_fetch_kind, &element)) {
        case KOS_IREQ_DELIVERED:
          PyTuple_SET_ITEM(row, (Py_ssize_t) i, element);
          continue;
        case KOS_IREQ_EXHAUSTED:
          if (! zip_longest_retire(z, i)) {
            Py_DECREF(row);
            t->closed = 1;
            *out = NULL;
            return KOS_ITER_EXHAUSTED;
          }
          zip_longest_fill(z, row, i);
          continue;
        case KOS_IREQ_CONTINUES:
          t->idx = i;
          t->pending = row;  /* steals */
          return KOS_ITER_INSTALLED_VALUE;
        default:
          Py_DECREF(row);
          return kos_transform_step_abort(ctx, t, pre_top);
      }
    }

    *out = row;
    return KOS_ITER_VALUE;
  }
}


static KosIterStepResult zip_longest_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, zip_longest_step_body);
}


/* === the type === */


static PyObject *koszip_longest_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  Py_ssize_t n = PyTuple_GET_SIZE(args);
  PyObject *fill = Py_None;
  KosZipLongest *z;

  if (kwds && PyDict_Size(kwds)) {
    if (PyDict_Size(kwds) != 1)
      goto bad_kwds;

    fill = PyDict_GetItemString(kwds, "fillvalue");
    if (! fill) {
      if (PyErr_Occurred())
        return NULL;
      goto bad_kwds;
    }
  }

  if (n > UINT16_MAX) {
    PyErr_SetString(PyExc_TypeError,
                    "zip-longest takes at most 65535 iterables");
    return NULL;
  }

  z = (KosZipLongest *) type->tp_alloc(type, 0);
  if (! z)
    return NULL;

  if (kos_transform_init_multi(&z->multi, args)) {
    Py_DECREF(z);
    return NULL;
  }

  Py_INCREF(fill);
  z->fill = fill;
  z->live = (uint16_t) n;

  return (PyObject *) z;

 bad_kwds:
  PyErr_SetString(PyExc_TypeError,
                  "zip-longest takes only a fillvalue keyword argument");
  return NULL;
}


static int koszip_longest_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosZipLongest *z = (KosZipLongest *) self;

  Py_VISIT(z->fill);
  return kos_transform_multi_traverse(self, visit, arg);
}


static int koszip_longest_clear(PyObject *self) {

  KosZipLongest *z = (KosZipLongest *) self;

  z->live = 0;
  Py_CLEAR(z->fill);

  return kos_transform_multi_clear(self);
}


KosEvaluatedTypeObject KosZipLongestType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.zip-longest",
    .tp_basicsize = sizeof(KosZipLongest),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosZipLongest(*iterables, fillvalue=None) - native lazy "
              "zip-longest; iterable both asynchronously (ambient "
              "await, koskari-flattened) and synchronously (demand "
              "path)",
    .tp_traverse = koszip_longest_traverse,
    .tp_clear = koszip_longest_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = koszip_longest_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = zip_longest_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
