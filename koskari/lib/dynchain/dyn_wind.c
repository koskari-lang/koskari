/* Included by dynchain.c — dyn_wind */
/* === KosDynWind implementation === */


/**
   Allocate dynamic-wind node; caps derived from which thunks are present.
   Callers: kos_dyn_push_wind.
*/
KosDynWind *kos_dynwind_new(
    KosDynNode *parent,
    PyObject *before_thunk,
    PyObject *after_thunk) {

  KosDynWind *wind = kos_dynnode_alloc();
  if (! wind)
    return NULL;

  uint32_t wind_caps = 0;
  if (after_thunk)
    wind_caps |= KOS_DYN_CAP_NEEDS_UNWIND_ACTION;
  if (before_thunk)
    wind_caps |= KOS_DYN_CAP_NEEDS_WIND_BEFORE_ACTION;
  if (before_thunk || after_thunk)
    wind_caps |= KOS_DYN_CAP_AWAITS_THUNK_RESULT;
  kos_dynnode_init(wind, KOS_DYN_WIND, parent, wind_caps);
  KosDynWind_BEFORE_THUNK(wind) = before_thunk;
  if (before_thunk)
    Py_INCREF(before_thunk);
  KosDynWind_AFTER_THUNK(wind) = after_thunk;
  if (after_thunk)
    Py_INCREF(after_thunk);
  KosDynWind_ENTERED(wind) = 0;
  KosDynWind_PHASE(wind) = KOS_WIND_PHASE_IDLE;
  return wind;
}


/**
   Clone thunks and phase for continuation segments.
*/
KosDynWind *kos_dynwind_clone(KosDynWind *wind) {
  if (! wind)
    return NULL;

  KosDynWind *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_WIND, NULL, KosDynNode_CAPS(wind));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(wind) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(wind);
  KosDynWind_BEFORE_THUNK(clone) = KosDynWind_BEFORE_THUNK(wind);
  if (KosDynWind_BEFORE_THUNK(clone))
    Py_INCREF(KosDynWind_BEFORE_THUNK(clone));
  KosDynWind_AFTER_THUNK(clone) = KosDynWind_AFTER_THUNK(wind);
  if (KosDynWind_AFTER_THUNK(clone))
    Py_INCREF(KosDynWind_AFTER_THUNK(clone));
  KosDynWind_BODY_RESULT(clone) = NULL;
  KosDynWind_ENTERED(clone) = KosDynWind_ENTERED(wind);
  KosDynWind_PHASE(clone) = KosDynWind_PHASE(wind);
  return clone;
}


/**
   DECREF thunks and body_result; return to freelist.
*/
void kos_dynwind_free(KosDynWind *wind) {
  if (! wind)
    return;

  Py_XDECREF(KosDynWind_BEFORE_THUNK(wind));
  KosDynWind_BEFORE_THUNK(wind) = NULL;
  Py_XDECREF(KosDynWind_AFTER_THUNK(wind));
  KosDynWind_AFTER_THUNK(wind) = NULL;
  Py_XDECREF(KosDynWind_BODY_RESULT(wind));
  KosDynWind_BODY_RESULT(wind) = NULL;
  kos_dynnode_return_to_freelist(wind);
}


/**
   Run before-thunk (enter path); may suspend async. Callers: kos_dyn_wind,
   install wind span.
*/
int kos_dyn_wind_wind(KosContext *ctx, KosDynWind *node) {
  if (! node || ! KosDynWind_BEFORE_THUNK(node))
    return 0;

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_BEGIN;
  if (KosContext_CallNoArgs(ctx, NULL, KosDynWind_BEFORE_THUNK(node), 0) < 0) {
    return -1;
  }

  return KOS_DYN_UNWIND_ASYNC_STARTED;
}


/**
   Run after-thunk or push body_result when leaving extent. Callers:
   kos_dyn_unwind, uninstall_handler.
*/
int kos_dyn_wind_unwind(KosContext *ctx, KosDynWind *node) {
  if (! node || ! KosDynWind_ENTERED(node))
    return 0;

  if (! KosDynWind_AFTER_THUNK(node)) {
    /* No after-thunk: push stored body result and finish uninstall */
    PyObject *body_result = KosDynWind_BODY_RESULT(node);
    KosDynWind_BODY_RESULT(node) = NULL;
    if (KosContext_STACK_HEAD(ctx) && body_result) {
      KosEvalState es;
      KosEvalState_Init(&es, ctx);
      if (KosEvalState_StackPushRef(&es, body_result) < 0) {
        KosDynWind_BODY_RESULT(node) = body_result;
        return -1;
      }
    }
    if (body_result)
      Py_DECREF(body_result);
    return 0;
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_AFTER;
  {
    PyObject *args = PyTuple_New(0);
    if (! args)
      return -1;
    if (KosContext_CallArgs(ctx, NULL, KosDynWind_AFTER_THUNK(node), args, NULL, 0) < 0) {
      Py_DECREF(args);
      return -1;
    }
    Py_DECREF(args);
  }
  return KOS_DYN_UNWIND_ASYNC_STARTED;
}


/**
   Resume after async before/after thunk: advance phase, pop junk, maybe unlink.
*/
static int kos_dyn_wind_on_control_returned(KosContext *ctx, KosDynWind *wind) {
  if (! KosContext_STACK_HEAD(ctx))
    return 0;

  KosEvalState es;
  KosEvalState_Init(&es, ctx);

  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_BEGIN) {
    KosDynWind_PHASE(wind) = KOS_WIND_PHASE_POST_BEGIN;
    KosDynWind_ENTERED(wind) = 1;
    PyObject *val = KosEvalState_StackPopRef(&es);
    if (val)
      Py_DECREF(val);
    return 0;
  }
  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_AFTER) {
    PyObject *thunk_ret = KosEvalState_StackPopRef(&es);
    if (thunk_ret)
      Py_DECREF(thunk_ret);

    PyObject *body_result = KosDynWind_BODY_RESULT(wind);
    KosDynWind_BODY_RESULT(wind) = NULL;

    KosDynWind_PHASE(wind) = KOS_WIND_PHASE_POST_AFTER;
    KosContext_UNWIND_TARGET_FRAME(ctx) = NULL;
    KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(wind);
    KosContext_CHAIN_VERSION(ctx)++;
    KosDynNode_PARENT(wind) = NULL;
    kos_dynwind_free(wind);
    kos_ctx_refresh_dyn_caches(ctx);

    if (body_result) {
      if (KosEvalState_StackPushRef(&es, body_result) < 0) {
        Py_DECREF(body_result);
        return -1;
      }
      Py_DECREF(body_result);
    }
    return 0;
  }
  return 0;
}


/**
   Allocate and link wind as DYN_TOP (caller runs wind enter separately in
   some paths). Callers: opcode PUSH_WIND.
*/
KosDynWind *kos_dyn_push_wind(
    KosContext *ctx,
    PyObject *before_thunk,
    PyObject *after_thunk) {

  KosDynWind *wind = kos_dynwind_new(KosContext_DYN_TOP(ctx),
                                     before_thunk, after_thunk);
  if (! wind)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) wind;
  KosContext_CHAIN_VERSION(ctx)++;

  return wind;
}


/**
   Unlink wind without running full uninstall (legacy fast pop). Prefer
   kos_dyn_uninstall for extent teardown when thunks must run.
*/
int kos_dyn_pop_wind(KosContext *ctx, KosDynWind *wind) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) wind) {
    PyErr_SetString(PyExc_RuntimeError, "wind node is not at top of chain");
    return -1;
  }

  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(wind);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_dynwind_free(wind);

  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_WIND: visit the before and after thunk callables,
   and the stored body result.
*/
static int dynnode_traverse_wind(KosDynNode *node, visitproc visit, void *arg) {
  KosDynWind *wind = (KosDynWind *) node;
  Py_VISIT(KosDynWind_BEFORE_THUNK(wind));
  Py_VISIT(KosDynWind_AFTER_THUNK(wind));
  Py_VISIT(KosDynWind_BODY_RESULT(wind));
  return 0;
}


