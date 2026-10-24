/* Included by transform.c -- chain */


/**
   koskari/lib/transform/chain.c

   KosChain: a native lazy chain over a series of iterables,
   duck-typed against itertools.chain, implemented as a KosIter
   provider on the shared transform body (transform.c). This unit
   holds only the chain step and the resume chain it arms.

   One source is live at a time, so the base's single source slot is
   the whole of the view: nsrc stays 1 and close reaches whichever
   source is current. The iterables behind it are held unacquired
   until reached, since acquiring one has effects -- calling a
   generator function, opening a file in __iter__ -- that a chain
   which never gets that far must not cause. That laziness is the
   reason itertools.chain acquires as it goes, and the reason the
   koskari-written version it replaces did too.

   A chain can run a list into an async iterator, so each fetch asks
   the request entry which kind its current source is; nothing about
   a source is fixed for the transform's lifetime.

   Exhaustion of one source is not exhaustion of the chain, so this
   provider catches a source running out rather than propagating it --
   and, having more sources to fetch from, carries the step on by
   threading back into its own fetch.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosChain {
  KosTransform base;
  PyObject *series;        /* acquired iterator over the iterables not
                              yet reached; NULL once it runs out */
} KosChain;


/**
   Make the iterable the series just produced the live source,
   re-resolving whether it steps through tp_iternext alone. iterable
   is borrowed.

   :return: 0, or -1 with PyErr set when acquiring failed
*/
static int chain_take_source(KosChain *c, PyObject *iterable) {

  KosTransform *t = &c->base;
  PyObject *source = kos_iter_acquire(iterable);

  if (! source)
    return -1;

  t->source = source;

  return 0;
}




/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   chain-step frame refuses by name (see the kos_transform_stage call
   site below and kos_dynframe_clone's resume refusal). */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_deliver(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_fetch(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_series(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND(chain_resume_deliver_kind, "chain-step",
                chain_resume_deliver, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(chain_resume_fetch_kind, "chain-step",
                chain_resume_fetch, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(chain_resume_series_kind, "chain-step",
                chain_resume_series, kos_transform_step_clear, NULL);


/**
   FETCH: take from the live source, advancing through the series as
   each one runs out. A sync source completes inside the loop; a
   driven one requests its element and leaves, to be re-entered here
   by chain_resume_deliver. Always entered with a NULL value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosChain *c = (KosChain *) state;
  KosTransform *t = &c->base;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  for (;;) {
    if (! t->source) {
      if (! c->series) {
        PyErr_SetNone(PyExc_StopIteration);
        return EVAL_RAISE();
      }
      /* the next iterable is requested from the series; its
         delivery lands in chain_resume_series */
      KosOpResult r = KosContext_NextResume(ctx, c->series,
                                              &chain_resume_series_kind, c);
      RESULT_ENTRY(ctx, r, &chain_resume_series_kind, c);
    }

    /* the element lands in chain_resume_deliver; a spent source is
       released there and the walk re-enters here */
    {
      KosOpResult r = KosContext_NextResume(ctx, t->source,
                                              &chain_resume_deliver_kind, c);
      RESULT_ENTRY(ctx, r, &chain_resume_deliver_kind, c);
    }
  }
}


/**
   DELIVER: the live driven source produced an element, or ran out.
   Running out moves to the next iterable rather than ending the
   chain, so the pending StopIteration is handled here. element is
   consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_deliver(
    KosContext *ctx, void *state, PyObject *element) {

  KosChain *c = (KosChain *) state;
  KosTransform *t = &c->base;

  kos_transform_fetched(t->source, element);

  if (! element) {
    if (! kos_transform_source_spent())
      return EVAL_RAISE();

    PyErr_Clear();
    Py_CLEAR(t->source);

    RESUME_HOP(&chain_resume_fetch_kind, ctx, c, NULL);
  }

  return KosNativeFrame_ReturnToCache(ctx, element, &t->cached_frame);
}


/**
   SERIES: the driven series produced the next iterable, or ran out.
   The iterable becomes the live source and the fetch goes on; running
   out ends the chain. iterable is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult chain_resume_series(
    KosContext *ctx, void *state, PyObject *iterable) {

  KosChain *c = (KosChain *) state;
  int r;

  kos_transform_fetched(c->series, iterable);

  if (! iterable) {
    if (! kos_transform_source_spent())
      return EVAL_RAISE();

    /* the series is spent, and with it the chain: the StopIteration
       stands as the step's outcome */
    Py_CLEAR(c->series);
    return EVAL_RAISE();
  }

  r = chain_take_source(c, iterable);
  Py_DECREF(iterable);
  if (r < 0)
    return EVAL_RAISE();

  RESUME_HOP(&chain_resume_fetch_kind, ctx, c, NULL);
}


/* === the step === */


static KosIterStepResult chain_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosChain *c = (KosChain *) self;
  KosTransform *t = &c->base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);

  /* Walk the series here for as long as the series and the sources
     answer without the evaluator; the first driven one defers the
     rest of the walk to chain_resume_fetch. */
  for (;;) {
    PyObject *element;

    if (! t->source) {
      PyObject *iterable;
      int r;

      if (! c->series) {
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      }
      switch (KosContext_NextOrDefer(ctx, c->series,
                                       &chain_resume_fetch_kind, t,
                                       &t->cached_frame, &iterable)) {
        case KOS_IREQ_DELIVERED:
          break;
        case KOS_IREQ_EXHAUSTED:
          Py_CLEAR(c->series);
          t->closed = 1;
          *out = NULL;
          return KOS_ITER_EXHAUSTED;
        case KOS_IREQ_CONTINUES:
          return KOS_ITER_INSTALLED_VALUE;
        default:
          return kos_transform_step_abort(ctx, t, pre_top);
      }
      r = chain_take_source(c, iterable);
      Py_DECREF(iterable);
      if (r < 0)
        return KOS_ITER_ERROR;
      continue;
    }

    switch (KosContext_NextOrDefer(ctx, t->source,
                                     &chain_resume_fetch_kind, t,
                                     &t->cached_frame, &element)) {
      case KOS_IREQ_DELIVERED:
        *out = element;
        return KOS_ITER_VALUE;
      case KOS_IREQ_EXHAUSTED:
        /* this source is spent; the chain is not until the series is */
        Py_CLEAR(t->source);
        continue;
      case KOS_IREQ_CONTINUES:
        return KOS_ITER_INSTALLED_VALUE;
      default:
        return kos_transform_step_abort(ctx, t, pre_top);
    }
  }
}


static KosIterStepResult chain_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, chain_step_body);
}


/* === the type === */


/**
   Build a chain over the iterables that series produces: the argument
   tuple of the chain form, or whatever chain-from-iterable was given.
   The series is acquired here and pulled as the chain advances, so an
   iterable it has not produced yet is never touched. series is
   borrowed.
*/
static PyObject *chain_build(PyTypeObject *type, PyObject *series) {

  KosChain *c = (KosChain *) type->tp_alloc(type, 0);

  if (! c)
    return NULL;

  c->series = kos_iter_acquire(series);
  if (! c->series) {
    Py_DECREF(c);
    return NULL;
  }

  /* One source is live at a time, so the arity-one view serves: the
     step fills the slot as it advances and close reaches whichever
     source is current. A NULL slot is nothing to close. */
  kos_transform_init_view(&c->base, &c->base.source, 1);

  return (PyObject *) c;
}


static PyObject *koschain_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "chain takes no keyword arguments");
    return NULL;
  }

  return chain_build(type, args);
}


/**
   chain.from-iterable(iterables): the series comes from one iterable
   rather than from the argument list. It may be unbounded, and it may
   be driven: it is pulled one iterable at a time as the chain reaches
   the end of the one before.
*/
static PyObject *koschain_from_iterable(PyObject *self, PyObject *arg) {

  (void) self;

  return chain_build(KOS_EVALUATED_PYTYPE(KosChainType), arg);
}


static PyMethodDef koschain_from_iterable_def = {
  "chain-from-iterable", (PyCFunction) koschain_from_iterable, METH_O,
  "chain-from-iterable(iterables) - native lazy chain over a series "
  "taken from one iterable",
};


/**
   Close reaches the live source through the base, and the series
   after it: an iterable the series has not produced is never touched,
   but the series itself may hold state to release.
*/
static int koschain_close(PyObject *self) {

  KosChain *c = (KosChain *) self;
  int r = kos_transform_close(self);

  if (c->base.closed && ! c->base.busy && c->series) {
    kos_iter_close(c->series);
    Py_CLEAR(c->series);
  }

  return r;
}


static int koschain_traverse(PyObject *self, visitproc visit, void *arg) {

  KosChain *c = (KosChain *) self;

  Py_VISIT(c->series);
  return kos_transform_traverse(self, visit, arg);
}


static int koschain_clear(PyObject *self) {

  KosChain *c = (KosChain *) self;

  Py_CLEAR(c->series);
  return kos_transform_clear(self);
}


static void koschain_dealloc(PyObject *self) {

  KosChain *c = (KosChain *) self;

  PyObject_GC_UnTrack(self);
  Py_CLEAR(c->series);

  kos_transform_dealloc(self);
}


KosEvaluatedTypeObject KosChainType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.chain",
    .tp_basicsize = sizeof(KosChain),
    .tp_itemsize = 0,
    .tp_dealloc = koschain_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosChain(*iterables) - native lazy chain; each iterable "
              "is acquired only when the one before it runs out. "
              "Iterable both asynchronously (ambient await, "
              "koskari-flattened) and synchronously (demand path)",
    .tp_traverse = koschain_traverse,
    .tp_clear = koschain_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = koschain_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = chain_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = koschain_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
