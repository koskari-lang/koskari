/* Included by transform.c -- accumulate */


/**
   koskari/lib/transform/accumulate.c

   KosAccumulate: a native lazy accumulate over one source, duck-typed
   against itertools.accumulate, implemented as a KosIter provider on
   the shared transform body (transform.c). This unit holds only the
   accumulate step and the resume chain it arms.

   The running total outlives every step, so it is this provider's own
   field rather than base.pending, which ki_finish clears at each step
   completion. Two shapes have no call at all: the first element is
   the first total, emitted as it arrives, and an absent function adds
   with PyNumber_Add rather than resolving a callable to do it -- the
   same thing itertools does with a NULL binop. An initial seed is
   emitted before the source is touched, so a seeded accumulate over
   an empty source still yields once.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosAccumulate {
  KosTransform base;
  PyObject *total;    /* running value, NULL until the first element
                         or the seed primes it; outlives each step */
  PyObject *initial;  /* seed awaiting its own first emission, else
                         NULL */
} KosAccumulate;


/**
   Combine the running total with element under whatever this
   accumulate folds by. element is borrowed.

   :return: a new total, or NULL with PyErr set
*/
static PyObject *accumulate_fold(KosAccumulate *a, PyObject *element) {

  return PyNumber_Add(a->total, element);
}


/**
   Emit the running total, which the provider keeps a reference to.
*/
static KosOpResult accumulate_emit(KosEvalState *es, KosAccumulate *a) {

  Py_INCREF(a->total);
  return KosNativeFrame_ReturnPark(es, a->total, &a->base.cached_frame);
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   accumulate-step frame refuses by name (see the kos_transform_stage
   call sites below and kos_dynframe_clone's resume refusal). */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult accumulate_resume_fetch(
    KosEvalState *es, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult accumulate_resume_fold(
    KosEvalState *es, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult accumulate_resume_result(
    KosEvalState *es, void *state, PyObject *value);

KOS_RESUME_KIND(accumulate_resume_fetch_kind, "accumulate-step",
                accumulate_resume_fetch, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(accumulate_resume_fold_kind, "accumulate-step",
                accumulate_resume_fold, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(accumulate_resume_result_kind, "accumulate-step",
                accumulate_resume_result, kos_transform_step_clear, NULL);


/**
   RESULT: the fold settled. It becomes the running total and the
   step's value. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult accumulate_resume_result(
    KosEvalState *es, void *state, PyObject *value) {

  KosAccumulate *a = (KosAccumulate *) state;

  if (! value)
    return EVAL_RAISE();

  Py_CLEAR(a->base.pending);
  Py_SETREF(a->total, value);

  return accumulate_emit(es, a);
}


/**
   FOLD: combine the delivered element into the running total. The
   first element primes the total and is emitted as it stands.
   element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult accumulate_resume_fold(
    KosEvalState *es, void *state, PyObject *element) {

  KosAccumulate *a = (KosAccumulate *) state;
  KosTransform *t = &a->base;

  if (! element)
    return EVAL_RAISE();

  if (! a->total) {
    a->total = element;  /* steals */
    return accumulate_emit(es, a);
  }

  if (! t->fn.cs_callable) {
    PyObject *folded = accumulate_fold(a, element);

    Py_DECREF(element);
    if (! folded)
      return EVAL_RAISE();

    Py_SETREF(a->total, folded);
    return accumulate_emit(es, a);
  }

  /* pending holds the element across the call; the argument vector
     borrows both it and the total for the call's duration. */
  t->pending = element;
  {
    PyObject *argv[2] = { a->total, element };
    KosOpResult r = KosEvalState_CallResult(es, t->fn.cs_callable, argv, 2,
                                            NULL, 0,
                                            &accumulate_resume_result_kind, a);
    RESULT_ENTRY(es, r, &accumulate_resume_result_kind, a);
  }
}


/**
   Cold-start kickoff for a driven source, made from inside
   koskari_eval's own dispatch rather than from the step. Always
   invoked with NULL.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult accumulate_resume_fetch(
    KosEvalState *es, void *state, PyObject *value) {

  KosOpResult r;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  r = KosEvalState_NextResume(
      es, ((KosTransform *) state)->source, NULL,
      &accumulate_resume_fold_kind, state);
  RESULT_ENTRY(es, r, &accumulate_resume_fold_kind, state);
}


/* === the step === */


static KosIterStepResult accumulate_iter_step(
    KosEvalState *es,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosAccumulate *a = (KosAccumulate *) self;
  KosTransform *t = &a->base;
  KosDynNode *pre_top = KosEvalState_DYN_TOP(es);
  KosIterStepResult early;

  if (! kos_transform_step_ready(t, send_val, out, &early))
    return early;

  /* A seed is the first value and costs no fetch, so a seeded
     accumulate over an empty source still yields once. */
  if (a->initial) {
    a->total = a->initial;
    a->initial = NULL;
    Py_INCREF(a->total);
    *out = a->total;
    return KOS_ITER_VALUE;
  }

  if (t->src_sync) {
    PyObject *element = PyIter_Next(t->source);

    if (! element) {
      if (PyErr_Occurred())
        return KOS_ITER_ERROR;
      t->closed = 1;
      *out = NULL;
      return KOS_ITER_EXHAUSTED;
    }

    if (! a->total) {
      a->total = element;  /* steals */
      Py_INCREF(a->total);
      *out = a->total;
      return KOS_ITER_VALUE;
    }

    if (! t->fn.cs_callable) {
      PyObject *folded = accumulate_fold(a, element);

      Py_DECREF(element);
      if (! folded)
        return KOS_ITER_ERROR;

      Py_SETREF(a->total, folded);
      Py_INCREF(a->total);
      *out = a->total;
      return KOS_ITER_VALUE;
    }

    /* A KosCallable fold's result must come back to this step, so a
       resume-carrying frame installs here and the element is staged
       raw for accumulate_resume_fold, which makes the call in this
       evaluator. */
    if (t->fn.cs_kind != KOS_CALL_SHAPE_PLAIN)
      return kos_transform_stage(es, t, element,
                                 &accumulate_resume_fold_kind, pre_top);

    /* A plain python function needs no evaluator at all. */
    {
      PyObject *argv[2] = { a->total, element };
      PyObject *result = PyObject_Vectorcall(t->fn.cs_callable, argv, 2,
                                             NULL);

      Py_DECREF(element);
      if (! result)
        return KOS_ITER_ERROR;

      switch (KosEval_ResultIsValue(result)) {
        case 1:
          Py_SETREF(a->total, result);
          Py_INCREF(a->total);
          *out = a->total;
          return KOS_ITER_VALUE;
        case 0:
          /* Awaitable fold result: stage it, unclassified, and defer
             to accumulate_resume_result -- the await it needs must
             happen from inside koskari_eval's own dispatch. */
          return kos_transform_stage(es, t, result,
                                     &accumulate_resume_result_kind, pre_top);
        default:
          Py_DECREF(result);
          return KOS_ITER_ERROR;
      }
    }
  }

  /* Driven source: set the cold-start kickoff resume and defer the
     first fetch entirely. */
  return kos_transform_stage(es, t, NULL, &accumulate_resume_fetch_kind,
                             pre_top);
}


/* === the type === */


static PyObject *kosaccumulate_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *iterable = NULL;
  PyObject *func = NULL;
  PyObject *initial = NULL;
  KosAccumulate *a;

  if (kwds && PyDict_Size(kwds)) {
    if (PyDict_Size(kwds) != 1)
      goto bad_kwds;

    initial = PyDict_GetItemString(kwds, "initial");
    if (! initial) {
      if (PyErr_Occurred())
        return NULL;
      goto bad_kwds;
    }
    if (initial == Py_None)
      initial = NULL;
  }

  if (! PyArg_UnpackTuple(args, "accumulate", 1, 2, &iterable, &func))
    return NULL;

  /* None is the default fold, not the absence of one: an accumulate
     with no function adds, which the step does with PyNumber_Add. */
  if (func == Py_None)
    func = NULL;

  if (func && ! PyCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError,
                    "accumulate func must be callable or None");
    return NULL;
  }

  a = (KosAccumulate *) type->tp_alloc(type, 0);
  if (! a)
    return NULL;

  if (kos_transform_init_one(&a->base, func, iterable)) {
    Py_DECREF(a);
    return NULL;
  }

  a->total = NULL;
  Py_XINCREF(initial);
  a->initial = initial;

  return (PyObject *) a;

 bad_kwds:
  PyErr_SetString(PyExc_TypeError,
                  "accumulate takes only an initial keyword argument");
  return NULL;
}


static int kosaccumulate_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosAccumulate *a = (KosAccumulate *) self;

  Py_VISIT(a->total);
  Py_VISIT(a->initial);
  return kos_transform_traverse(self, visit, arg);
}


static int kosaccumulate_clear(PyObject *self) {

  KosAccumulate *a = (KosAccumulate *) self;

  Py_CLEAR(a->total);
  Py_CLEAR(a->initial);

  return kos_transform_clear(self);
}


static void kosaccumulate_dealloc(PyObject *self) {

  KosAccumulate *a = (KosAccumulate *) self;

  PyObject_GC_UnTrack(self);
  Py_CLEAR(a->total);
  Py_CLEAR(a->initial);

  kos_transform_dealloc(self);
}


KosEvaluatedTypeObject KosAccumulateType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.accumulate",
    .tp_basicsize = sizeof(KosAccumulate),
    .tp_itemsize = 0,
    .tp_dealloc = kosaccumulate_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosAccumulate(iterable, func=None, *, initial=None) - "
              "native lazy accumulate; iterable both asynchronously "
              "(ambient await, koskari-flattened) and synchronously "
              "(demand path)",
    .tp_traverse = kosaccumulate_traverse,
    .tp_clear = kosaccumulate_clear,
    .tp_iter = KosIterAdapt_DemandIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kosaccumulate_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = accumulate_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
