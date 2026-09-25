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

  kos_dynnode_init(wind, KOS_DYN_WIND, parent,
                   KOS_DYN_CAP_CONTROL_HOOK | KOS_DYN_CAP_HOLDS_OBLIGATION);
  KosDynWind_BEFORE_THUNK(wind) = before_thunk;
  if (before_thunk)
    Py_INCREF(before_thunk);
  KosDynWind_AFTER_THUNK(wind) = after_thunk;
  if (after_thunk)
    Py_INCREF(after_thunk);
  KosDynWind_OB(wind)->kind = KOS_OB_NONE;
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

  /* A thunk in flight, or a value, exception or return parked for its
     completion, belongs to this node's one run and to no template. */
  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_BEGIN
      || KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_AFTER
      || KosDynWind_OB(wind)->kind != KOS_OB_NONE) {
    PyErr_SetString(PyExc_RuntimeError,
                    "cannot capture a continuation across a dyn-wind whose"
                    " thunk is in flight");
    return NULL;
  }

  KosDynWind *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_WIND, NULL, KosDynNode_CAPS(wind));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(wind) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = kos_dynnode_id(wind);
  KosDynWind_BEFORE_THUNK(clone) = KosDynWind_BEFORE_THUNK(wind);
  if (KosDynWind_BEFORE_THUNK(clone))
    Py_INCREF(KosDynWind_BEFORE_THUNK(clone));
  KosDynWind_AFTER_THUNK(clone) = KosDynWind_AFTER_THUNK(wind);
  if (KosDynWind_AFTER_THUNK(clone))
    Py_INCREF(KosDynWind_AFTER_THUNK(clone));
  KosDynWind_OB(clone)->kind = KOS_OB_NONE;
  KosDynWind_ENTERED(clone) = 0;
  KosDynWind_PHASE(clone) = KOS_WIND_PHASE_IDLE;
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
  kos_dyn_ob_clear(KosDynWind_OB(wind));
  kos_dynnode_return_to_freelist(wind);
}


static int dynnode_to_dict_wind(KosDynNode *node, PyObject *data) {

  if (snapshot_set_int(data, "entered", KosDynWind_ENTERED((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "phase", (int) KosDynWind_PHASE((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "before_thunk",
      KosDynWind_BEFORE_THUNK((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "after_thunk",
      KosDynWind_AFTER_THUNK((KosDynWind *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "obligation",
      (int) KosDynWind_OB((KosDynWind *) node)->kind) < 0)
    return -1;
  return 0;
}


/**
   Fire thunk with no arguments and classify what happened. The phase
   has already been set to the pending value by the caller.

   :return: EVAL_RAISE; EVAL_AWAIT with the awaitable; EVAL_REPROLOGUE
     when the thunk's frame is installed; EVAL_NORMAL when the thunk ran
     to completion in hand, its result already dropped
*/
static KosOpResult kos_dyn_wind_fire(KosEvalState *es, PyObject *thunk) {
  KosOpResult wr = KosEvalState_CallResult(es, thunk, NULL, 0, NULL, 0,
                                           NULL, NULL);

  /* A value in hand is settled on the block, or awaited there; settled,
     it is not the wind's business. */
  if (wr.kind == KOS_RES_RETURN) {
    wr = KosEvalState_PushOrAwait(es, wr.val);
    if (wr.kind == KOS_RES_NORMAL) {
      PyObject *val = KosEvalState_StackPopRef(es);
      if (! val)
        return EVAL_RAISE();
      Py_DECREF(val);
      return EVAL_NORMAL();
    }
  }

  if (wr.kind == KOS_RES_RAISE || wr.kind == KOS_RES_AWAIT)
    return wr;
  return EVAL_REPROLOGUE();
}


/**
   Enter: run the before-thunk once. See the extent contract in
   dynchain.h.
*/
static KosOpResult kos_dyn_wind_enter(KosEvalState *es, KosDynWind *node) {

  if (KosDynWind_ENTERED(node))
    return EVAL_NORMAL();
  if (KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_BEGIN)
    return EVAL_REPROLOGUE();  /* before-thunk in flight */

  if (! KosDynWind_BEFORE_THUNK(node)) {
    KosDynWind_ENTERED(node) = 1;
    return EVAL_NORMAL();
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_BEGIN;
  {
    KosOpResult wr = kos_dyn_wind_fire(es, KosDynWind_BEFORE_THUNK(node));
    if (wr.kind != KOS_RES_NORMAL)
      return wr;
  }
  KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_BEGIN;
  KosDynWind_ENTERED(node) = 1;
  return EVAL_NORMAL();
}


/**
   Leave: run the after-thunk once. See the extent contract in
   dynchain.h. The body result is not delivered here; the unlink does
   that after the node is gone.
*/
static KosOpResult kos_dyn_wind_leave(
    KosEvalState *es,
    KosDynWind *node,
    KosXferEvent ev) {

  (void) ev;

  switch (KosDynWind_PHASE(node)) {
    case KOS_WIND_PHASE_POST_AFTER:
      return EVAL_NORMAL();

    case KOS_WIND_PHASE_PRE_AFTER:
    case KOS_WIND_PHASE_PRE_BEGIN:
      /* A thunk in flight sits in a frame above this node. If this
         node is dyn_top, that frame is gone (it raised, or the walk
         crossed it) and the thunk is dead: nothing to wait for and,
         for a before-thunk, never entered. */
      if (KosEvalState_DYN_TOP(es) != (KosDynNode *) node)
        return EVAL_REPROLOGUE();
      KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
      return EVAL_NORMAL();

    default:
      break;
  }

  if (! KosDynWind_ENTERED(node) || ! KosDynWind_AFTER_THUNK(node)) {
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
    return EVAL_NORMAL();
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_AFTER;
  {
    KosOpResult wr = kos_dyn_wind_fire(es, KosDynWind_AFTER_THUNK(node));
    if (wr.kind != KOS_RES_NORMAL)
      return wr;
  }
  KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
  return EVAL_NORMAL();
}


/**
   The pending thunk returned; its result is the top of the live block.
*/
static int kos_dyn_wind_settle(KosEvalState *es, KosDynWind *node) {

  if (KosDynWind_PHASE(node) != KOS_WIND_PHASE_PRE_BEGIN
      && KosDynWind_PHASE(node) != KOS_WIND_PHASE_PRE_AFTER)
    return 0;

  if (KosEvalState_BLOCK(es) && *KosEvalState_BLOCK(es)
      && KosSegment_LEN(*KosEvalState_BLOCK(es))) {
    PyObject *val = KosEvalState_StackPopRef(es);
    if (! val)
      return -1;
    Py_DECREF(val);
  }

  if (KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_BEGIN) {
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_BEGIN;
    KosDynWind_ENTERED(node) = 1;
  } else {
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
  }
  return 0;
}


/**
   The pending thunk died; nothing to take.
*/
static void kos_dyn_wind_abort(KosDynWind *node) {

  if (KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_BEGIN
      || KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_AFTER)
    KosDynWind_PHASE(node) = KOS_WIND_PHASE_POST_AFTER;
}


/**
   Allocate and link wind as DYN_TOP (caller runs wind enter separately in
   some paths). Callers: opcode PUSH_WIND.
*/
KosDynWind *kos_dyn_push_wind(
    KosEvalState *es,
    PyObject *before_thunk,
    PyObject *after_thunk) {

  KosDynWind *wind = kos_dynwind_new(KosEvalState_DYN_TOP(es),
                                     before_thunk, after_thunk);
  if (! wind)
    return NULL;

  if (kos_dyn_push(es, (KosDynNode *) wind) < 0) {
    kos_dynwind_free(wind);
    return NULL;
  }
  return wind;
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
  return kos_dyn_ob_traverse(KosDynWind_OB(wind), visit, arg);
}


