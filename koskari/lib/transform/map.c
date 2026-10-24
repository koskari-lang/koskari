/* Included by transform.c -- map */


/**
   koskari/lib/transform/map.c

   KosMap: a native lazy map over one source, duck-typed against
   builtins.map, implemented as a KosIter provider on the shared
   transform body (transform.c). This unit holds only the map step and
   the resume chain it arms.

   The fast path is pure C: a sync source element and a non-suspending,
   direct-return-guaranteed koskari function complete as KOS_ITER_VALUE
   or a graft install with the evaluator otherwise untouched (cs_direct,
   still unconverted below pending a benchmark-driven decision -- see
   design/archive/cps-consumer-api.md). Anything else -- a driven source, or a
   FRAME-shaped or awaitable-returning function without that guarantee
   -- runs under one CPS resume-carrying frame: FETCH requests the next
   source element via KosContext_NextResume, INVOKE applies the
   function via KosContext_Call, and completion delivers the
   element to the consumer's step resume. Exhaustion and errors leave
   the frame as exceptions; the consumer's step resume converts
   StopIteration per its own rules, which also gives a mapped
   function's StopIteration builtins.map truncation parity.

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

KOS_RESUME_KIND_AS_IS(map_resume_after_fetch_kind, "map-step",
                map_resume_after_fetch, kos_transform_step_clear, NULL);
KOS_RESUME_KIND(map_resume_after_result_kind, "map-step",
                map_resume_after_result, kos_transform_step_clear, NULL);
KOS_RESUME_KIND_AS_IS(map_resume_start_fetch_kind, "map-step",
                map_resume_start_fetch, kos_transform_step_clear, NULL);


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
   INVOKE: apply func to element. Every calling shape goes through the
   one entry -- KosContext_Call resolves the callable and prepares
   (or declines to prepare) a frame per its own suite, so a callable
   that wants no frame still runs in this evaluator rather than
   through the Python boundary. element is consumed.
*/
static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult map_resume_after_fetch(
    KosContext *ctx, void *state, PyObject *element) {

  KosTransform *m = (KosTransform *) state;
  KosOpResult r;

  kos_transform_fetched(m->source, element);

  if (! element)
    return kos_transform_decline(ctx, m);

  r = KosContext_Call(
      ctx, m->fn.cs_callable, &element, 1, NULL, 0,
      &map_resume_after_result_kind, m);
  Py_DECREF(element);
  RESULT_ENTRY(ctx, r, &map_resume_after_result_kind, m);
}


/**
   Cold-start kickoff for a driven source: the very first NextResume
   call for a freshly installed step. map_iter_step cannot make this
   call itself -- it runs outside koskari_eval, and koskari_eval runs
   as a separate step after install (see stepper_step), never nested
   inside map_iter_step's own C call -- so it just sets this resume and
   lets the trampoline invoke it, from inside koskari_eval, once the
   frame is actually dispatched. Always invoked with NULL.
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

  r = KosContext_NextResume(
      ctx, t->source, &map_resume_after_fetch_kind, t);
  RESULT_ENTRY(ctx, r, &map_resume_after_fetch_kind, t);
}


/* === the step === */


static KosIterStepResult map_step_body(
    KosContext *ctx,
    PyObject *self,
    PyObject *send_val,
    PyObject **out) {

  KosTransform *m = (KosTransform *) self;
  KosDynNode *pre_top = KosContext_DYN_TOP(ctx);

  {
    PyObject *element;

    KOS_TRANSFORM_FETCH(ctx, m, m->source, &map_resume_start_fetch_kind, pre_top, out,
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


static const KosTransformSpec map_spec = {
  .has_callable = 1,
  .none_means = KOS_TF_NONE_REJECT,
};


static PyObject *kosmap_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  return kos_transform_new(type, args, kwds, &map_spec);
}


KosEvaluatedTypeObject KosMapType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.map",
    .tp_basicsize = sizeof(KosTransform),
    .tp_itemsize = 0,
    .tp_dealloc = kos_transform_dealloc,
    .tp_repr = kos_transform_repr,
    .tp_as_async = &kos_transform_async_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "KosMap(func, iterable) - native lazy map; iterable both "
              "asynchronously (ambient await, koskari-flattened) and "
              "synchronously (demand path)",
    .tp_traverse = kos_transform_traverse,
    .tp_clear = kos_transform_clear,
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
