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
    KosEvalState *es, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_fetch(
    KosEvalState *es, void *state, PyObject *value);

KOS_RESUME_KIND(compress_resume_deliver_kind, "compress-step",
                compress_resume_deliver, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(compress_resume_fetch_kind, "compress-step",
                compress_resume_fetch, kos_transform_step_clear, NULL);


/**
   FETCH: take a data element, then its selector, emitting the data
   when the selector is truthy and looping when it is not. Always
   entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosTransformMulti *c = (KosTransformMulti *) state;
  KosTransform *t = &c->base;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  for (;;) {
    PyObject *element;

    if (! ((c->sync_mask >> t->idx) & 1u)) {
      KosOpResult r = KosEvalState_NextResume(
          es, t->sources[t->idx], NULL, &compress_resume_deliver_kind, c);
      RESULT_ENTRY(es, r, &compress_resume_deliver_kind, c);
    }

    element = PyIter_Next(t->sources[t->idx]);
    if (! element) {
      /* Either source running out ends the compress. */
      if (! PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }

    if (t->idx == COMPRESS_DATA) {
      t->pending = element;  /* steals; held for the selector */
      t->idx = COMPRESS_SELECTOR;
      continue;
    }

    {
      int ok = compress_keeps(element);

      if (ok < 0)
        return EVAL_RAISE();

      t->idx = COMPRESS_DATA;
      if (ok) {
        PyObject *kept = t->pending;

        t->pending = NULL;
        return KosNativeFrame_ReturnToCache(es, kept, &t->cached_frame);
      }
      Py_CLEAR(t->pending);
    }
  }
}


/**
   DELIVER: a driven source produced its element for this row. File it
   and hop back into the row. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_deliver(
    KosEvalState *es, void *state, PyObject *element) {

  KosTransformMulti *c = (KosTransformMulti *) state;
  KosTransform *t = &c->base;

  if (! element)
    return EVAL_RAISE();

  if (t->idx == COMPRESS_DATA) {
    t->pending = element;  /* steals */
    t->idx = COMPRESS_SELECTOR;
    RESUME_HOP(&compress_resume_fetch_kind, es, c, NULL);
  }

  {
    int ok = compress_keeps(element);

    if (ok < 0)
      return EVAL_RAISE();

    t->idx = COMPRESS_DATA;
    if (ok) {
      PyObject *kept = t->pending;

      t->pending = NULL;
      return KosNativeFrame_ReturnToCache(es, kept, &t->cached_frame);
    }
    Py_CLEAR(t->pending);
  }

  RESUME_HOP(&compress_resume_fetch_kind, es, c, NULL);
}


/* === the step === */


static KosIterStepResult compress_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransformMulti *c = (KosTransformMulti *) self;
  KosTransform *t = &c->base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  t->idx = COMPRESS_DATA;

  if (c->all_sync) {
    /* Pure C: rejected rows never leave this loop. */
    for (;;) {
      PyObject *data = PyIter_Next(t->sources[COMPRESS_DATA]);
      PyObject *selector;
      int ok;

      if (! data) {
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }

      selector = PyIter_Next(t->sources[COMPRESS_SELECTOR]);
      if (! selector) {
        Py_DECREF(data);
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
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

  /* A driven source: defer the whole row. Nothing is fetched yet to
     stage, and a request that can suspend must run from inside
     koskari_eval's own dispatch. */
  return kos_transform_stage(es, t, NULL, &compress_resume_fetch_kind,
                             pre_top);
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
    .tp_dealloc = kos_transform_multi_dealloc,
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
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
