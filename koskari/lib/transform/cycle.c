/* Included by transform.c -- cycle */


/**
   koskari/lib/transform/cycle.c

   KosCycle: a native endless cycle over one source, duck-typed
   against itertools.cycle, implemented as a KosIter provider on the
   shared transform body (transform.c). This unit holds only the cycle
   step and the resume chain it arms.

   The first pass yields each element and keeps a copy. When the
   source runs out, the copies become the source: the iterator is
   released and every later step reads the buffer directly, so no
   iterator is re-acquired per lap and an async original replays in
   pure C with the evaluator untouched.

   The provider catches a source running out instead of propagating
   it -- here exhaustion means start replaying, and only an empty
   buffer means the cycle is empty. A source of no elements is
   exhausted immediately rather than looping forever over nothing.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


typedef struct KosCycle {
  KosTransform base;
  PyObject *buf;    /* list of everything the first pass produced */
  Py_ssize_t pos;   /* read position within buf once replaying */
  uint8_t filled;   /* the source is spent; buf is the source now */
} KosCycle;


/**
   Take the next element from the buffer, wrapping at its end. Only
   called once the buffer is known non-empty.

   :return: a new reference
*/
static PyObject *cycle_replay(KosCycle *c) {

  PyObject *item = PyList_GET_ITEM(c->buf, c->pos);

  c->pos++;
  if (c->pos >= PyList_GET_SIZE(c->buf))
    c->pos = 0;

  Py_INCREF(item);
  return item;
}


/**
   The source is spent. Release the iterator -- the base's close loop
   reads a NULL slot as nothing to close -- and switch to replaying.

   :return: 1 when there is anything to replay, 0 for an empty cycle
*/
static int cycle_switch_to_buffer(KosCycle *c) {

  Py_CLEAR(c->base.source);
  c->filled = 1;
  c->pos = 0;

  return PyList_GET_SIZE(c->buf) != 0;
}


/**
   Keep an element for later laps and hand back the caller's own
   reference to it. element is consumed.

   :return: element, or NULL with PyErr set
*/
static PyObject *cycle_keep(KosCycle *c, PyObject *element) {

  if (PyList_Append(c->buf, element) < 0) {
    Py_DECREF(element);
    return NULL;
  }
  return element;
}


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   cycle-step frame refuses by name (see the kos_transform_stage call
   site below and kos_dynframe_clone's resume refusal). */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult cycle_resume_deliver(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult cycle_resume_fetch(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND(cycle_resume_deliver_kind, "cycle-step",
                cycle_resume_deliver, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(cycle_resume_fetch_kind, "cycle-step",
                cycle_resume_fetch, kos_transform_step_clear, NULL);


/**
   DELIVER: the first pass produced an element, or ran out. Running
   out is not a failure -- it is the switch to replaying -- so the
   pending StopIteration is handled here rather than propagated.
   element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult cycle_resume_deliver(
    KosContext *ctx, void *state, PyObject *element) {

  KosCycle *c = (KosCycle *) state;
  KosTransform *t = &c->base;

  kos_transform_fetched(t->source, element);

  if (! element) {
    if (! kos_transform_source_spent())
      return EVAL_RAISE();

    PyErr_Clear();
    if (! cycle_switch_to_buffer(c)) {
      t->closed = 1;
      PyErr_SetNone(PyExc_StopIteration);
      return EVAL_RAISE();
    }

    {
      PyObject *item = cycle_replay(c);

      return KosNativeFrame_ReturnToCache(ctx, item, &t->cached_frame);
    }
  }

  element = cycle_keep(c, element);
  if (! element)
    return EVAL_RAISE();

  return KosNativeFrame_ReturnToCache(ctx, element, &t->cached_frame);
}


/**
   Cold-start kickoff for a driven source, made from inside
   koskari_eval's own dispatch rather than from the step. Always
   invoked with NULL.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult cycle_resume_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosOpResult r;

  (void) value;

  if (PyErr_Occurred())
    return EVAL_RAISE();

  r = KosContext_NextResume(
      ctx, ((KosTransform *) state)->source,
      &cycle_resume_deliver_kind, state);
  RESULT_ENTRY(ctx, r, &cycle_resume_deliver_kind, state);
}


/* === the step === */


static KosIterStepResult cycle_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosCycle *c = (KosCycle *) self;
  KosTransform *t = &c->base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);

  /* Replaying: pure C from here on, whatever the original source
     was. */
  if (c->filled) {
    *out = cycle_replay(c);
    return KOS_ITER_VALUE;
  }

  {
    PyObject *element;

    switch (KosContext_NextOrDefer(ctx, t->source, &cycle_resume_fetch_kind,
                                     t, &t->cached_frame, &element)) {
      case KOS_IREQ_DELIVERED:
        *out = cycle_keep(c, element);
        return *out ? KOS_ITER_VALUE : KOS_ITER_ERROR;
      case KOS_IREQ_EXHAUSTED:
        /* the source is spent: replay from the buffer, if any */
        if (! cycle_switch_to_buffer(c)) {
          t->closed = 1;
          *out = NULL;
          return KOS_ITER_EXHAUSTED;
        }
        *out = cycle_replay(c);
        return KOS_ITER_VALUE;
      case KOS_IREQ_CONTINUES:
        return KOS_ITER_INSTALLED_VALUE;
      default:
        return kos_transform_step_abort(ctx, t, pre_top);
    }
  }
}


static KosIterStepResult cycle_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, cycle_step_body);
}


/* === the type === */


static const KosTransformSpec cycle_spec = {
  .has_callable = 0,
  .none_means = KOS_TF_NONE_REJECT,
};


static PyObject *koscycle_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *self = kos_transform_new(type, args, kwds, &cycle_spec);
  KosCycle *c;

  if (! self)
    return NULL;

  c = (KosCycle *) self;
  c->buf = PyList_New(0);
  if (! c->buf) {
    Py_DECREF(self);
    return NULL;
  }
  c->pos = 0;
  c->filled = 0;

  return self;
}


static int koscycle_traverse(PyObject *self, visitproc visit, void *arg) {

  KosCycle *c = (KosCycle *) self;

  Py_VISIT(c->buf);
  return kos_transform_traverse(self, visit, arg);
}


static int koscycle_clear(PyObject *self) {

  KosCycle *c = (KosCycle *) self;

  Py_CLEAR(c->buf);
  return kos_transform_clear(self);
}


static void koscycle_dealloc(PyObject *self) {

  KosCycle *c = (KosCycle *) self;

  PyObject_GC_UnTrack(self);
  Py_CLEAR(c->buf);

  kos_transform_dealloc(self);
}


KosEvaluatedTypeObject KosCycleType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.cycle",
    .tp_basicsize = sizeof(KosCycle),
    .tp_itemsize = 0,
    .tp_dealloc = koscycle_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosCycle(iterable) - native endless cycle; iterable "
              "both asynchronously (ambient await, koskari-flattened) "
              "and synchronously (demand path)",
    .tp_traverse = koscycle_traverse,
    .tp_clear = koscycle_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = koscycle_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = cycle_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
