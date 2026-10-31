/* Included by transform.c -- map */


/**
   koskari/lib/transform/map.c

   KosMap: a native lazy map over one or more sources, duck-typed
   against builtins.map, implemented as a KosIter provider on the
   shared transform body (transform.c). This unit holds only the map
   step and the resume chain it arms.

   Every map is the KosTransformMulti shape. With one source the
   request lives in the base's own slot and the step is the one-source
   step. With several, the requests live in the array the shape owns,
   and each step fills the shape's row, a tuple private to the map
   that is the call's argument vector: one element per source, taken
   left to right, the first spent source ending the map without a
   later one being asked, as builtins.map stops at its shortest
   iterable.

   The fast path is pure C: sync source elements and a plain python
   function complete as KOS_ITER_VALUE with the evaluator untouched,
   and a direct-return koskari function (cs_direct: its install fills
   a frame whose return IS the element) installs through the generic
   call entry with no helper frame. Anything else -- a driven source,
   a FRAME-shaped or awaitable-returning function without that
   guarantee -- runs under one CPS resume-carrying frame: FETCH
   requests the next source element via KosContext_NextResume, INVOKE
   applies the function via KosContext_Call, and completion delivers
   the element to the consumer's step resume. The several-source walk
   is carried by base.idx, one loop over the sources that leaves only
   for a source that suspends and re-enters at the same position, so
   there is no frame per source. Exhaustion and errors leave the frame
   as exceptions; the consumer's step resume converts StopIteration
   per its own rules, which also gives a mapped function's
   StopIteration builtins.map truncation parity.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


/* === the step's CPS resumes ===

   No frame-carrying capture: replaying a captured step would
   re-consume source elements, so continuation capture across a live
   map-step frame refuses by name (see the kos_transform_stage call
   sites below and kos_dynframe_clone's resume refusal). */


/* the resume kinds: one static record per resume function, its
   name and lifecycle; the step clear releases the record's busy mark
   and pending value when the frame is torn down mid-step, and no
   clone (a step in flight refuses capture by name) */
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_after_fetch(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_after_result(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_start_fetch(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_fetch_row(
    KosContext *ctx, void *state, PyObject *value);
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_deliver_row(
    KosContext *ctx, void *state, PyObject *value);

KOS_RESUME_KIND_AS_IS(map_resume_after_fetch_kind, "map-step",
                map_resume_after_fetch, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(map_resume_after_result_kind, "map-step",
                map_resume_after_result, kos_transform_step_clear, NULL);
KOS_RESUME_KIND_AS_IS(map_resume_start_fetch_kind, "map-step",
                map_resume_start_fetch, kos_transform_step_clear, NULL);
KOS_RESUME_KIND_AS_IS(map_resume_fetch_row_kind, "map-step",
                map_resume_fetch_row, kos_transform_step_clear, NULL);
KOS_RESUME_KIND_AS_IS(map_resume_deliver_row_kind, "map-step",
                map_resume_deliver_row, kos_transform_step_clear, NULL);


/**
   The invoke completed. The value is settled by contract, so the step
   completes with it directly. value is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_after_result(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransform *m = (KosTransform *) state;

  /* The raise walk is propagating an exception: let it, so the frame
     pops and the consumer's step resume owns the conversion
     (StopIteration truncation parity included). */
  if (! value)
    return kos_transform_decline(ctx, m);

  RESULT_FRAME_RETURN(ctx, value, &m->cached_frame);
}


/**
   INVOKE, one source: apply func to element. Every calling shape goes
   through the one entry -- KosContext_Call resolves the callable and
   prepares (or declines to prepare) a frame per its own suite, so a
   callable that wants no frame still runs in this evaluator rather
   than through the Python boundary. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_after_fetch(
    KosContext *ctx, void *state, PyObject *element) {

  KosTransform *m = (KosTransform *) state;
  KosOpResult r;

  kos_transform_fetched(&m->src, element);

  if (! element)
    return kos_transform_decline(ctx, m);

  r = KosContext_Call(
      ctx, m->fn.cs_callable, &element, 1, NULL, 0,
      &map_resume_after_result_kind, m);
  Py_DECREF(element);
  RESULT_ENTRY(ctx, r, &map_resume_after_result_kind, m);
}


/**
   Cold-start kickoff for a driven source, one source: the very first
   NextResume call for a freshly installed step. map_iter_step cannot
   make this call itself -- it runs outside koskari_eval, and
   koskari_eval runs as a separate step after install (see
   stepper_step), never nested inside map_iter_step's own C call -- so
   it just sets this resume and lets the trampoline invoke it, from
   inside koskari_eval, once the frame is actually dispatched. Always
   invoked with NULL.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_start_fetch(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransform *t = (KosTransform *) state;
  KosOpResult r;

  (void) value;

  /* The raise walk is propagating an exception: let it, as every
     resume does. */
  if (PyErr_Occurred())
    return kos_transform_decline(ctx, t);

  r = KosContext_NextResumeBound(
      ctx, &t->src, &map_resume_after_fetch_kind, t);
  RESULT_ENTRY(ctx, r, &map_resume_after_fetch_kind, t);
}


/**
   FETCH, several sources: request the source at base.idx; its element
   is filed by map_resume_deliver_row, which re-enters here for the
   next. At base.idx == nsrc the row is complete and func is applied
   to it, every calling shape through the one entry as the one-source
   INVOKE does; the step that staged the frame with the row already
   complete enters here the same way. Always entered with a NULL
   value.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_fetch_row(
    KosContext *ctx, void *state, PyObject *value) {

  KosTransformMulti *z = (KosTransformMulti *) state;
  KosTransform *t = &z->base;
  KosOpResult r;

  (void) value;

  if (PyErr_Occurred())
    return kos_transform_decline(ctx, t);

  if (t->idx < t->nsrc) {
    /* a spent source's StopIteration reaches the consumer's step
       resume from the deliver and ends the map */
    r = KosContext_NextResumeBound(
        ctx, &t->reqs[t->idx], &map_resume_deliver_row_kind, z);
    RESULT_ENTRY(ctx, r, &map_resume_deliver_row_kind, z);
  }

  r = KosContext_Call(
      ctx, t->fn.cs_callable, PySequence_Fast_ITEMS(z->row),
      (Py_ssize_t) t->nsrc, NULL, 0, &map_resume_after_result_kind, z);
  RESULT_ENTRY(ctx, r, &map_resume_after_result_kind, z);
}


/**
   DELIVER, several sources: a driven source produced its element.
   File it at the position the walk stopped on and hop back into the
   walk. A tail call, so a row of driven sources stays flat. element is
   consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_deliver_row(
    KosContext *ctx, void *state, PyObject *element) {

  KosTransformMulti *z = (KosTransformMulti *) state;
  KosTransform *t = &z->base;

  kos_transform_fetched(&t->reqs[t->idx], element);

  if (! element)
    return kos_transform_decline(ctx, t);

  kos_transform_row_set(z->row, (Py_ssize_t) t->idx, element);
  t->idx++;

  RESUME_HOP(&map_resume_fetch_row_kind, ctx, z, NULL);
}


/* === the step === */


/**
   The several-source step: fill the row from the sources in order,
   then apply func to it. The row fills here for as long as the
   sources answer without the evaluator; the first driven source
   defers the rest of the walk to map_resume_fetch_row, from its
   position. A row that stops short at a spent source keeps the items
   of the last complete row in its later slots until the map is
   cleared; nothing reads them.
*/
static KosIterStepResult map_step_row(
    KosContext *ctx,
    KosTransformMulti *z,
    PyObject **out,
    KosDynNode *pre_top) {

  KosTransform *t = &z->base;
  PyObject *row = z->row;
  uint16_t i;

  for (i = 0; i < t->nsrc; i++) {
    PyObject *element;

    switch (KosContext_NextOrDefer(ctx, &t->reqs[i],
                                     &map_resume_fetch_row_kind, z,
                                     &t->cached_frame, &element)) {
      case KOS_IREQ_DELIVERED:
        kos_transform_row_set(row, (Py_ssize_t) i, element);
        continue;
      case KOS_IREQ_EXHAUSTED:
        t->closed = 1;
        *out = NULL;
        return KOS_ITER_EXHAUSTED;
      case KOS_IREQ_CONTINUES:
        t->idx = i;
        return KOS_ITER_INSTALLED_VALUE;
      default:
        return kos_transform_step_abort(ctx, t, pre_top);
    }
  }

  /* A direct-delivery koskari function installs through the generic
     call entry with the row's items as the argument vector and no
     helper frame; the row keeps the items alive until the next fill. */
  if (t->fn.cs_kind == KOS_CALL_SHAPE_FRAME && t->fn.cs_direct) {
    KosOpResult r = KosContext_Call(
        ctx, t->fn.cs_callable, PySequence_Fast_ITEMS(row),
        (Py_ssize_t) t->nsrc, NULL, 0, NULL, NULL);
    if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE)
      return KOS_ITER_INSTALLED_VALUE;
    return kos_transform_step_abort(ctx, t, pre_top);
  }

  /* Any other KosCallable: the call must run inside koskari_eval's own
     dispatch, so the step stages a frame with nothing pushed and
     base.idx at the row's end, and map_resume_fetch_row makes the
     call from the completed row. */
  if (t->fn.cs_kind != KOS_CALL_SHAPE_PLAIN) {
    t->idx = t->nsrc;
    return kos_transform_stage(ctx, t, NULL, &map_resume_fetch_row_kind,
                               pre_top);
  }

  /* A plain python callable needs no evaluator at all. */
  {
    PyObject *result = PyObject_Vectorcall(
        t->fn.cs_callable, PySequence_Fast_ITEMS(row),
        (size_t) t->nsrc, NULL);

    if (! result)
      return KOS_ITER_ERROR;

    switch (KosEval_ResultIsValue(result)) {
      case 1:
        *out = result;
        return KOS_ITER_VALUE;
      case 0:
        /* Awaitable function result: stage it, unclassified, and
           defer to map_resume_after_result -- the await it needs must
           happen from inside koskari_eval's own dispatch. */
        return kos_transform_stage(ctx, t, result,
                                   &map_resume_after_result_kind, pre_top);
      default:
        Py_DECREF(result);
        return KOS_ITER_ERROR;
    }
  }
}


static KosIterStepResult map_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransformMulti *z = (KosTransformMulti *) self;
  KosTransform *m = &z->base;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);

  if (m->nsrc != 1)
    return map_step_row(ctx, z, out, pre_top);

  {
    PyObject *element;

    KOS_TRANSFORM_FETCH(ctx, m, &m->src, &map_resume_start_fetch_kind, pre_top, out,
                        element);

    /* A direct-delivery koskari function (cs_direct: its install
       fills a frame whose return IS the element) installs through the
       generic call entry -- cached frame, the element's own address
       as the argument vector -- with no helper frame. */
    if (m->fn.cs_kind == KOS_CALL_SHAPE_FRAME && m->fn.cs_direct) {
      KosOpResult r = KosContext_Call(
          ctx, m->fn.cs_callable, &element, 1, NULL, 0, NULL, NULL);
      Py_DECREF(element);
      if (r.kind == KOS_RES_NORMAL || r.kind == KOS_RES_REPROLOGUE)
        return KOS_ITER_INSTALLED_VALUE;
      return kos_transform_step_abort(ctx, m, pre_top);
    }

    /* Any other KosCallable -- a values or bound-method FRAME shape
       without the frame-return guarantee, or one wanting no frame at
       all: stage the element on a resume-carrying frame and defer the
       call to map_resume_after_fetch, which runs it in this evaluator.
       map_iter_step runs before koskari_eval drives this chain (see
       stepper_step), so the call itself -- which can install a callee
       or need an await -- must happen from inside that later dispatch,
       not here. */
    if (m->fn.cs_kind != KOS_CALL_SHAPE_PLAIN)
      return kos_transform_stage(ctx, m, element,
                                 &map_resume_after_fetch_kind, pre_top);

    /* A plain python callable needs no evaluator at all. */
    {
      PyObject *result = PyObject_CallOneArg(m->fn.cs_callable, element);
      Py_DECREF(element);
      if (! result)
        return KOS_ITER_ERROR;

      switch (KosEval_ResultIsValue(result)) {
        case 1:
          *out = result;
          return KOS_ITER_VALUE;
        case 0:
          /* Awaitable function result: stage it, unclassified, and
             defer to map_resume_after_result the same way -- the
             await it needs must happen from inside koskari_eval's
             own dispatch, not from map_iter_step. */
          return kos_transform_stage(ctx, m, result,
                                     &map_resume_after_result_kind, pre_top);
        default:
          Py_DECREF(result);
          return KOS_ITER_ERROR;
      }
    }
  }

}


static KosIterStepResult map_iter_step(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  return kos_transform_step(ctx, self, send_val, out, map_step_body);
}


/* === the type === */


/**
   map(func, iterable, *iterables): one source goes into the base's
   own slot, as kos_transform_new would place it; several go into the
   shape's request array, with the row allocated here at their count.
   Every source is acquired eagerly, so a bad argument raises where
   the map is written.
*/
static PyObject *kosmap_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  Py_ssize_t n = PyTuple_GET_SIZE(args);
  PyObject *func;
  KosTransformMulti *z;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "map takes no keyword arguments");
    return NULL;
  }

  if (n < 2) {
    PyErr_SetString(PyExc_TypeError,
                    "map() must have at least two arguments.");
    return NULL;
  }

  if (n - 1 > UINT16_MAX) {
    PyErr_SetString(PyExc_TypeError, "map takes at most 65535 iterables");
    return NULL;
  }

  func = PyTuple_GET_ITEM(args, 0);
  if (! PyCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError, "map func must be callable");
    return NULL;
  }

  z = (KosTransformMulti *) type->tp_alloc(type, 0);
  if (! z)
    return NULL;

  if (n == 2) {
    if (kos_transform_init_one(&z->base, func, PyTuple_GET_ITEM(args, 1)))
      goto fail;
    return (PyObject *) z;
  }

  {
    PyObject *iterables = PyTuple_GetSlice(args, 1, n);
    int failed;

    if (! iterables)
      goto fail;
    failed = kos_transform_init_multi(z, iterables);
    Py_DECREF(iterables);
    if (failed)
      goto fail;
  }

  /* the multi init leaves the callable unset; the row is the call's
     argument vector for the map's life */
  KosCallShape_Init(&z->base.fn, func);
  z->row = PyTuple_New(n - 1);
  if (! z->row)
    goto fail;

  return (PyObject *) z;

fail:
  Py_DECREF(z);
  return NULL;
}


KosEvaluatedTypeObject KosMapType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.map",
    .tp_basicsize = sizeof(KosTransformMulti),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosMap(func, iterable, *iterables) - native lazy map; func "
              "takes one element from each iterable per step and the "
              "shortest iterable ends it; iterable both asynchronously "
              "(ambient await, koskari-flattened) and synchronously "
              "(demand path)",
    .tp_traverse = kos_transform_multi_traverse,
    .tp_clear = kos_transform_multi_clear,
    .tp_iter = KosIterAdapt_PyIter,
    .tp_methods = kos_transform_methods,
    .tp_new = kosmap_new,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_iter = {
    .ki_step = map_iter_step,
    .ki_throw = kos_transform_throw,
    .ki_close = kos_transform_close,
    .ki_finish = kos_transform_finish,
    .ki_pyctx = kos_transform_pyctx,
    .ki_busy = kos_transform_busy,
    .ki_pyctx_recyclable = 1,
  },
};


// The end.
