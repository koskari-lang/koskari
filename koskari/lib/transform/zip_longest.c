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
  KosTransformMulti multi;  /* a spent source's slot in multi.srcs is
                               cleared to NULL in place -- the tuple is
                               private to this object and its item
                               block never moves */
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

  PyObject *spent = z->multi.base.sources[i];

  z->multi.base.sources[i] = NULL;
  Py_XDECREF(spent);
  z->live--;

  return z->live != 0;
}


/**
   File the fill value at position i of the row under construction.
*/
static void zip_longest_fill(KosZipLongest *z, uint16_t i) {

  Py_INCREF(z->fill);
  PyTuple_SET_ITEM(z->multi.base.pending, (Py_ssize_t) i, z->fill);
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
    KosEvalState *es, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_longest_resume_fetch(
    KosEvalState *es, void *state, PyObject *value);

KOS_RESUME_KIND(zip_longest_resume_deliver_kind, "zip-longest-step",
                zip_longest_resume_deliver, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(zip_longest_resume_fetch_kind, "zip-longest-step",
                zip_longest_resume_fetch, kos_transform_step_clear, NULL);


/**
   FETCH: walk the sources from base.idx, filing each element into the
   row. A spent source contributes its fill value without a fetch; a
   sync source completes inside the loop; a driven one requests the
   next element and leaves, to be re-entered here by
   zip_longest_resume_deliver. Always entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_longest_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosZipLongest *z = (KosZipLongest *) state;
  KosTransform *t = &z->multi.base;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  while (t->idx < t->nsrc) {
    PyObject *element;

    if (! t->sources[t->idx]) {
      zip_longest_fill(z, t->idx);
      t->idx++;
      continue;
    }

    if (! kos_transform_multi_sync(&z->multi, t->idx)) {
      KosOpResult r = KosEvalState_NextResume(
          es, t->sources[t->idx], NULL, &zip_longest_resume_deliver_kind, z);
      RESULT_ENTRY(es, r, &zip_longest_resume_deliver_kind, z);
    }

    element = PyIter_Next(t->sources[t->idx]);
    if (! element) {
      if (PyErr_Occurred())
        return EVAL_RAISE();

      /* The last source to run out ends the whole thing, and the row
         it ran out on is not emitted. */
      if (! zip_longest_retire(z, t->idx)) {
        PyErr_SetNone(PyExc_StopIteration);
        return EVAL_RAISE();
      }
      zip_longest_fill(z, t->idx);
      t->idx++;
      continue;
    }

    PyTuple_SET_ITEM(t->pending, (Py_ssize_t) t->idx, element);
    t->idx++;
  }

  {
    PyObject *row = t->pending;

    t->pending = NULL;
    return KosNativeFrame_ReturnToCache(es, row, &t->cached_frame);
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
    KosEvalState *es, void *state, PyObject *element) {

  KosZipLongest *z = (KosZipLongest *) state;
  KosTransform *t = &z->multi.base;

  if (! element) {
    if (! kos_transform_source_spent())
      return EVAL_RAISE();

    if (! zip_longest_retire(z, t->idx))
      return EVAL_RAISE();  /* the StopIteration already pending ends it */

    PyErr_Clear();
    zip_longest_fill(z, t->idx);
    t->idx++;

    RESUME_HOP(&zip_longest_resume_fetch_kind, es, z, NULL);
  }

  PyTuple_SET_ITEM(t->pending, (Py_ssize_t) t->idx, element);
  t->idx++;

  RESUME_HOP(&zip_longest_resume_fetch_kind, es, z, NULL);
}


/* === the step === */


static KosIterStepResult zip_longest_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosZipLongest *z = (KosZipLongest *) self;
  KosTransform *t = &z->multi.base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;
  PyObject *row;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  /* zip-longest over no sources is empty, as itertools' is. */
  if (! t->nsrc || ! z->live) {
    t->closed = 1;
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  row = PyTuple_New((Py_ssize_t) t->nsrc);
  if (! row)
    return KOS_ITER_ERROR;

  if (z->multi.all_sync) {
    uint16_t i;

    t->pending = row;  /* steals; zip_longest_fill files into it */

    for (i = 0; i < t->nsrc; i++) {
      PyObject *element;

      if (! t->sources[i]) {
        zip_longest_fill(z, i);
        continue;
      }

      element = PyIter_Next(t->sources[i]);
      if (! element) {
        if (PyErr_Occurred()) {
          Py_CLEAR(t->pending);
          return KOS_ITER_ERROR;
        }
        if (! zip_longest_retire(z, i)) {
          Py_CLEAR(t->pending);
          t->closed = 1;
          *out = NULL;
          return KOS_ITER_EXHAUSTED;
        }
        zip_longest_fill(z, i);
        continue;
      }

      PyTuple_SET_ITEM(t->pending, (Py_ssize_t) i, element);
    }

    *out = t->pending;
    t->pending = NULL;
    return KOS_ITER_VALUE;
  }

  /* At least one driven source: defer the whole walk. Nothing is
     fetched yet to stage, and a request that can suspend must run
     from inside koskari_eval's own dispatch. */
  {
    KosIterStepResult staged = kos_transform_stage(
        es, t, NULL, &zip_longest_resume_fetch_kind, pre_top);

    if (staged != KOS_ITER_INSTALLED_VALUE) {
      Py_DECREF(row);
      return staged;
    }

    t->idx = 0;
    t->pending = row;  /* steals */
    return staged;
  }
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


static void koszip_longest_dealloc(PyObject *self) {

  KosZipLongest *z = (KosZipLongest *) self;

  PyObject_GC_UnTrack(self);
  Py_CLEAR(z->fill);

  kos_transform_multi_dealloc(self);
}


KosEvaluatedTypeObject KosZipLongestType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.zip-longest",
    .tp_basicsize = sizeof(KosZipLongest),
    .tp_itemsize = 0,
    .tp_dealloc = koszip_longest_dealloc,
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
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
