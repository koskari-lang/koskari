/* Included by transform.c -- pairwise */


/**
   koskari/lib/transform/pairwise.c

   KosPairwise: a native lazy pairwise over one source, duck-typed
   against itertools.pairwise, implemented as a KosIter provider on
   the shared transform body (transform.c). This unit holds only the
   pairwise step and the resume chain it arms.

   The first provider with no callable at all: the argument tuple is
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
    KosEvalState *es, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult pairwise_resume_fetch(
    KosEvalState *es, void *state, PyObject *value);

KOS_RESUME_KIND(pairwise_resume_deliver_kind, "pairwise-step", pairwise_resume_deliver, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(pairwise_resume_fetch_kind, "pairwise-step", pairwise_resume_fetch, kos_transform_step_clear, NULL);


static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult pairwise_resume_deliver(
    KosEvalState *es, void *state, PyObject *element);


/**
   FETCH: request the next source element. Always entered with a NULL
   value -- either cold, from the driven-source install, or from
   pairwise_resume_deliver after the fetch that primed the pair.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult pairwise_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosTransform *t = (KosTransform *) state;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  if (t->src_sync) {
    PyObject *element = PyIter_Next(t->source);

    if (! element) {
      if (! PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }
    RESUME_HOP(&pairwise_resume_deliver_kind, es, t, element);
  }

  {
    KosOpResult r = KosEvalState_NextResume(es, t->source, NULL,
                                            &pairwise_resume_deliver_kind, t);
    RESULT_ENTRY(es, r, &pairwise_resume_deliver_kind, t);
  }
}


/**
   DELIVER: the first element of a cold step primes the pair and
   fetches again; any later one completes the step as the second half
   of a pair and becomes the held element. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult pairwise_resume_deliver(
    KosEvalState *es, void *state, PyObject *element) {

  KosPairwise *p = (KosPairwise *) state;
  KosTransform *t = &p->base;
  PyObject *row;

  if (! element)
    return EVAL_RAISE();

  if (! p->held) {
    p->held = element;  /* steals */
    RESUME_HOP(&pairwise_resume_fetch_kind, es, p, NULL);
  }

  row = PyTuple_Pack(2, p->held, element);
  if (! row) {
    Py_DECREF(element);
    return EVAL_RAISE();
  }

  Py_DECREF(p->held);
  p->held = element;  /* steals */

  return KosNativeFrame_ReturnPark(es, row, &t->cached_frame);
}


/* === the step === */


static KosIterStepResult pairwise_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosPairwise *p = (KosPairwise *) self;
  KosTransform *t = &p->base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  if (t->src_sync) {
    PyObject *element;
    PyObject *row;

    /* Prime the pair on the first step. A source of fewer than two
       elements never yields, exactly as Python's does. */
    if (! p->held) {
      p->held = PyIter_Next(t->source);
      if (! p->held) {
        if (PyErr_Occurred())
          return KOS_ITER_ERROR;
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }
    }

    element = PyIter_Next(t->source);
    if (! element) {
      if (PyErr_Occurred())
        return KOS_ITER_ERROR;
      Py_CLEAR(p->held);
      t->closed = 1;
      *out = NULL;
      return KOS_ITER_EXHAUSTED;
    }

    row = PyTuple_Pack(2, p->held, element);
    if (! row) {
      Py_DECREF(element);
      return KOS_ITER_ERROR;
    }

    Py_DECREF(p->held);
    p->held = element;  /* steals */

    *out = row;
    return KOS_ITER_VALUE;
  }

  /* Driven source: set the cold-start fetch resume and defer the
     whole step -- nothing is fetched yet to stage, and the request
     itself (which can suspend) must run from inside koskari_eval's
     own dispatch. */
  return kos_transform_stage(es, t, NULL,
                             &pairwise_resume_fetch_kind, pre_top);
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


/**
   Release the held element once the source is spent, so an exhausted
   pairwise keeps nothing alive.
*/
static void pairwise_iter_finish(PyObject *self, int exhausted) {

  KosPairwise *p = (KosPairwise *) self;

  kos_transform_finish(self, exhausted);
  if (exhausted)
    Py_CLEAR(p->held);
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


static void pairwise_dealloc(PyObject *self) {

  KosPairwise *p = (KosPairwise *) self;

  PyObject_GC_UnTrack(self);
  Py_CLEAR(p->held);

  kos_transform_dealloc(self);
}


KosEvaluatedTypeObject KosPairwiseType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.pairwise",
    .tp_basicsize = sizeof(KosPairwise),
    .tp_itemsize = 0,
    .tp_dealloc = pairwise_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosPairwise(iterable) - native lazy pairwise; iterable "
              "both asynchronously (ambient await, koskari-flattened) "
              "and synchronously (demand path)",
    .tp_traverse = pairwise_traverse,
    .tp_clear = pairwise_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kospairwise_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = pairwise_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = pairwise_iter_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
