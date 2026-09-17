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


typedef struct KosCompress {
  KosTransform base;
  PyObject *srcs;     /* 2-tuple of acquired iterators; the storage the
                         base's sources view points into */
  uint8_t all_sync;   /* neither source needs the evaluator */
  uint8_t sync_mask;  /* bit i: source i steps through tp_iternext */
} KosCompress;


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
   name and lifecycle; no clear (the owner keeps its record) and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_deliver(
    KosEvalState *es, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_fetch(
    KosEvalState *es, void *state, PyObject *value);

KOS_RESUME_KIND(compress_resume_deliver_kind, "compress-step", compress_resume_deliver, NULL, NULL);
KOS_RESUME_KIND(compress_resume_fetch_kind, "compress-step", compress_resume_fetch, NULL, NULL);


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_deliver(
    KosEvalState *es, void *state, PyObject *element);


/**
   FETCH: take a data element, then its selector, emitting the data
   when the selector is truthy and looping when it is not. Always
   entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult compress_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosCompress *c = (KosCompress *) state;
  KosTransform *t = &c->base;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  for (;;) {
    PyObject *element;

    if (! ((c->sync_mask >> t->idx) & 1u)) {
      KosOpResult r = KosEvalState_NextResume(es, t->sources[t->idx], NULL,
                                              &compress_resume_deliver_kind, c);
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
        return KosNativeFrame_ReturnPark(es, kept, &t->cached_frame);
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

  KosCompress *c = (KosCompress *) state;
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
      return KosNativeFrame_ReturnPark(es, kept, &t->cached_frame);
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

  KosCompress *c = (KosCompress *) self;
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
  return kos_transform_stage(es, t, NULL, &compress_resume_fetch_kind, pre_top);
}


/* === the type === */


static PyObject *koscompress_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *data = NULL;
  PyObject *selectors = NULL;
  PyObject *srcs;
  KosCompress *c;
  Py_ssize_t i;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "compress takes no keyword arguments");
    return NULL;
  }

  if (! PyArg_UnpackTuple(args, "compress", 2, 2, &data, &selectors))
    return NULL;

  srcs = PyTuple_New(2);
  if (! srcs)
    return NULL;

  {
    PyObject *given[2] = { data, selectors };

    for (i = 0; i < 2; i++) {
      PyObject *source = kos_iter_acquire(given[i]);

      if (! source) {
        Py_DECREF(srcs);
        return NULL;
      }
      PyTuple_SET_ITEM(srcs, i, source);
    }
  }

  c = (KosCompress *) type->tp_alloc(type, 0);
  if (! c) {
    Py_DECREF(srcs);
    return NULL;
  }

  c->srcs = srcs;
  c->sync_mask = 0;
  c->all_sync = 1;

  for (i = 0; i < 2; i++) {
    if (KosIter_CheckSync(PyTuple_GET_ITEM(srcs, i)))
      c->sync_mask |= (uint8_t) (1u << i);
    else
      c->all_sync = 0;
  }

  c->base.fn.cs_callable = NULL;
  c->base.fn.cs_slots = NULL;
  c->base.fn.cs_kind = KOS_CALL_SHAPE_PLAIN;

  c->base.source = NULL;
  c->base.sources = PySequence_Fast_ITEMS(srcs);
  c->base.nsrc = 2;
  c->base.idx = COMPRESS_DATA;

  c->base.pending = NULL;
  c->base.cached_frame = NULL;
  c->base.ctx = NULL;
  c->base.src_sync = 0;
  c->base.closed = 0;
  c->base.busy = 0;

  return (PyObject *) c;
}


static int koscompress_traverse(PyObject *self, visitproc visit, void *arg) {

  KosCompress *c = (KosCompress *) self;

  Py_VISIT(c->srcs);
  return kos_transform_traverse(self, visit, arg);
}


/**
   Retract the view before the storage it points into goes: tp_clear
   runs while the object is still alive, and the base reads the view
   whenever it closes.
*/
static int koscompress_clear(PyObject *self) {

  KosCompress *c = (KosCompress *) self;

  c->base.sources = NULL;
  c->base.nsrc = 0;
  Py_CLEAR(c->srcs);

  return kos_transform_clear(self);
}


static void koscompress_dealloc(PyObject *self) {

  KosCompress *c = (KosCompress *) self;

  PyObject_GC_UnTrack(self);
  c->base.sources = NULL;
  c->base.nsrc = 0;
  Py_CLEAR(c->srcs);

  kos_transform_dealloc(self);
}


KosEvaluatedTypeObject KosCompressType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.compress",
    .tp_basicsize = sizeof(KosCompress),
    .tp_itemsize = 0,
    .tp_dealloc = koscompress_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosCompress(data, selectors) - native lazy compress; "
              "iterable both asynchronously (ambient await, "
              "koskari-flattened) and synchronously (demand path)",
    .tp_traverse = koscompress_traverse,
    .tp_clear = koscompress_clear,
    .tp_iter = KosIterAdapt_DemandIter,
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
