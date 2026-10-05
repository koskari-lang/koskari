/* Included by transform.c -- zip */


/**
   koskari/lib/transform/zip.c

   KosZip: a native lazy zip over N sources, duck-typed against
   builtins.zip, implemented as a KosIter provider on the shared
   transform body (transform.c). A multi-source provider on the
   shared KosTransformMulti shape, with no fields of its own: the
   acquired iterators live in the tuple that shape owns, published to
   the base through the sources view so the base closes them without
   knowing where they are kept.

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
    KosEvalState *es, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_fetch(
    KosEvalState *es, void *state, PyObject *value);

KOS_RESUME_KIND(zip_resume_deliver_kind, "zip-step",
                zip_resume_deliver, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(zip_resume_fetch_kind, "zip-step",
                zip_resume_fetch, kos_transform_step_clear, NULL);


/**
   FETCH: walk the sources from base.idx, filing each element into the
   row. A sync source completes inside the loop; a driven one requests
   the next element and leaves, to be re-entered here by
   zip_resume_deliver. A completed row parks as the step's value.
   Always entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosTransformMulti *z = (KosTransformMulti *) state;
  KosTransform *t = &z->base;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  while (t->idx < t->nsrc) {
    PyObject *element;

    if (! kos_transform_multi_sync(z, t->idx)) {
      KosOpResult r = KosEvalState_NextResume(
          es, t->sources[t->idx], NULL, &zip_resume_deliver_kind, z);
      RESULT_ENTRY(es, r, &zip_resume_deliver_kind, z);
    }

    element = PyIter_Next(t->sources[t->idx]);
    if (! element) {
      /* A source is spent, so the zip is: let StopIteration reach the
         consumer's iteration node, which owns the conversion. The
         partial row is cleared by ki_finish. */
      if (! PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }

    PyTuple_SET_ITEM(t->pending, (Py_ssize_t) t->idx, element);
    t->idx++;
  }

  {
    PyObject *row = t->pending;

    t->pending = NULL;
    return KosNativeFrame_ReturnPark(es, row, &t->cached_frame);
  }
}


/**
   DELIVER: a driven source produced its element. File it at the
   position the walk stopped on and hop back into the walk. A tail
   call, so a row of driven sources stays flat. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult zip_resume_deliver(
    KosEvalState *es, void *state, PyObject *element) {

  KosTransformMulti *z = (KosTransformMulti *) state;
  KosTransform *t = &z->base;

  if (! element)
    return EVAL_RAISE();

  PyTuple_SET_ITEM(t->pending, (Py_ssize_t) t->idx, element);
  t->idx++;

  RESUME_HOP(&zip_resume_fetch_kind, es, z, NULL);
}


/* === the step === */


static KosIterStepResult zip_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransformMulti *z = (KosTransformMulti *) self;
  KosTransform *t = &z->base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;
  PyObject *row;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  /* zip over no sources is empty, as builtins.zip() is. */
  if (! t->nsrc) {
    t->closed = 1;
    *out = NULL;
    return KOS_ITER_EXHAUSTED;
  }

  row = PyTuple_New((Py_ssize_t) t->nsrc);
  if (! row)
    return KOS_ITER_ERROR;

  if (z->all_sync) {
    uint16_t i;

    for (i = 0; i < t->nsrc; i++) {
      PyObject *element = PyIter_Next(t->sources[i]);

      if (! element) {
        Py_DECREF(row);
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }
      PyTuple_SET_ITEM(row, (Py_ssize_t) i, element);
    }

    *out = row;
    return KOS_ITER_VALUE;
  }

  /* At least one driven source: defer the whole walk. Nothing is
     fetched yet to stage, and a request that can suspend must run
     from inside koskari_eval's own dispatch rather than from here
     (see stepper_step). */
  {
    KosIterStepResult staged = kos_transform_stage(
        es, t, NULL, &zip_resume_fetch_kind, pre_top);

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
    .tp_dealloc = kos_transform_multi_dealloc,
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
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
