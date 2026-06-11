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
  Py_XDECREF(KosDynWind_BODY_RESULT(wind));
  KosDynWind_BODY_RESULT(wind) = NULL;
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
  if (snapshot_set_obj(data, "body_result",
      KosDynWind_BODY_RESULT((KosDynWind *) node)) < 0)
    return -1;
  return 0;
}


/**
   Run before-thunk (enter path); may suspend async. Callers: kos_dyn_wind,
   install wind span.
*/
int kos_dyn_wind_wind(KosEvalState *es, KosDynWind *node) {
  if (! node || ! KosDynWind_BEFORE_THUNK(node)) {
    return 0;
  }

  if (KosDynWind_ENTERED(node)) {
    return 0;
  }

  KosDynWind_PHASE(node) = KOS_WIND_PHASE_PRE_BEGIN;
  if (KosEvalState_CallNoArgs(es, NULL, KosDynWind_BEFORE_THUNK(node), 0) < 0) {
    return -1;
  }

  return KOS_DYN_UNWIND_ASYNC_STARTED;
}


/**
   Run after-thunk or push body_result when leaving extent. Callers:
   kos_dyn_unwind, uninstall_handler.
*/
int kos_dyn_wind_unwind(KosEvalState *es, KosDynWind *node) {
  if (! node || ! KosDynWind_ENTERED(node)) {
    return 0;
  }

  /* After-thunk is already executing (PRE_AFTER); treat the node as already
     exiting so a nested transfer plan does not re-fire the thunk. */
  if (KosDynWind_PHASE(node) == KOS_WIND_PHASE_PRE_AFTER) {
    return 0;
  }

  if (! KosDynWind_AFTER_THUNK(node)) {
    /* No after-thunk: push stored body result and finish uninstall */
    PyObject *body_result = KosDynWind_BODY_RESULT(node);
    KosDynWind_BODY_RESULT(node) = NULL;
    if ((KosEvalState_BLOCK(es) && *KosEvalState_BLOCK(es)
             ? *KosEvalState_BLOCK(es)
             : NULL)
        && body_result) {
      if (KosEvalState_StackPushRef(es, body_result) < 0) {
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
    if (! args) {
      return -1;
    }
    if (KosEvalState_CallArgs(es, NULL, KosDynWind_AFTER_THUNK(node), args, NULL, 0) < 0) {
      Py_DECREF(args);
      return -1;
    }
    Py_DECREF(args);
  }
  return KOS_DYN_UNWIND_ASYNC_STARTED;
}


static int kos_dyn_wind_is_on_chain(KosEvalState *es, KosDynWind *wind) {

  for (KosDynNode *n = KosEvalState_DYN_TOP(es); n; n = KosDynNode_PARENT(n)) {
    if (n == (KosDynNode *) wind)
      return 1;
  }
  return 0;
}


/**
   Resume after async before/after thunk: advance phase, pop junk, maybe unlink.
*/
static int kos_dyn_wind_on_control_returned(KosEvalState *es, KosDynWind *wind) {

  /* A nested transfer can replace the chain while a wind thunk is still
     unwinding; ignore stale control-returned for a detached wind node. */
  if (! kos_dyn_wind_is_on_chain(es, wind))
    return 0;

  if (! (KosEvalState_BLOCK(es) && *KosEvalState_BLOCK(es)
             ? *KosEvalState_BLOCK(es)
             : NULL)) {
    return 0;
  }

  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_BEGIN) {
    if (! KosDynWind_BEFORE_THUNK(wind))
      return 0;

    if (KosEvalState_PLAN(es)) {
      KosTransferPlan *plan = KosEvalState_PLAN(es);
      size_t idx = KosTransferPlan_ACTION_INDEX(plan);
      if (idx < KosTransferPlan_ACTION_COUNT(plan)) {
        KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[idx];
        if (KosTransferAction_STEP(a) != KOS_XFER_EXTENT_ACTION
            || KosTransferAction_EXTENT_NODE(a) != (KosDynNode *) wind
            || KosTransferAction_EXTENT_PHASE(a) != KOS_XFER_EXTENT_PHASE_WIND)
          return 0;
      }
    }

    KosDynWind_PHASE(wind) = KOS_WIND_PHASE_POST_BEGIN;
    KosDynWind_ENTERED(wind) = 1;
    if (KosEvalState_BLOCK(es) && *KosEvalState_BLOCK(es)
        && KosStackBlock_LEN(*KosEvalState_BLOCK(es))) {
      PyObject *val = KosEvalState_StackPopRef(es);
      if (val)
        Py_DECREF(val);
    }
    /* The EXTENT_ACTION that fired this before-thunk returned
       KOS_XFER_SUSPENDED_AWAIT without advancing action_index; advance it
       now that the thunk has completed. */
    if (KosEvalState_PLAN(es)) {
      KosTransferPlan *plan = KosEvalState_PLAN(es);
      size_t idx = KosTransferPlan_ACTION_INDEX(plan);
      if (idx < KosTransferPlan_ACTION_COUNT(plan)) {
        KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[idx];
        if (KosTransferAction_STEP(a) == KOS_XFER_EXTENT_ACTION
            && KosTransferAction_EXTENT_NODE(a) == (KosDynNode *) wind
            && KosTransferAction_EXTENT_PHASE(a) == KOS_XFER_EXTENT_PHASE_WIND)
          KosTransferPlan_ACTION_INDEX(plan)++;
      }
    }
    return 0;
  }
  if (KosDynWind_PHASE(wind) == KOS_WIND_PHASE_PRE_AFTER) {
    /* When a wind thunk tail-invokes a continuation, a new transfer plan
       replaces the EXTENT_ACTION that started this thunk; defer cleanup
       until that nested transfer finishes and replans. */
    if (KosEvalState_PLAN(es)) {
      KosTransferPlan *plan = KosEvalState_PLAN(es);
      size_t idx = KosTransferPlan_ACTION_INDEX(plan);
      if (idx < KosTransferPlan_ACTION_COUNT(plan)) {
        KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[idx];
        if (KosTransferAction_STEP(a) != KOS_XFER_EXTENT_ACTION
            || KosTransferAction_EXTENT_NODE(a) != (KosDynNode *) wind)
          return 0;
      }
    }

    if (KosEvalState_BLOCK(es) && *KosEvalState_BLOCK(es)
        && KosStackBlock_LEN(*KosEvalState_BLOCK(es))) {
      PyObject *thunk_ret = KosEvalState_StackPopRef(es);
      if (thunk_ret)
        Py_DECREF(thunk_ret);
    }

    PyObject *body_result = KosDynWind_BODY_RESULT(wind);
    KosDynWind_BODY_RESULT(wind) = NULL;

    KosDynWind_PHASE(wind) = KOS_WIND_PHASE_POST_AFTER;
    KosEvalState_SetUnwindTargetFrame(es, NULL);
    {
      KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) wind);

      kos_dyn_commit_dyn_top(es, parent);
      KosDynNode_PARENT((KosDynNode *) wind) = NULL;
      kos_dynwind_free(wind);
    }

    /* The EXTENT_ACTION that fired this after-thunk returned
       KOS_XFER_SUSPENDED_AWAIT without advancing action_index; advance it
       now that the thunk has completed. */
    if (KosEvalState_PLAN(es)) {
      KosTransferPlan *plan = KosEvalState_PLAN(es);
      size_t idx = KosTransferPlan_ACTION_INDEX(plan);
      if (idx < KosTransferPlan_ACTION_COUNT(plan)) {
        KosTransferAction *a = &KosTransferPlan_ACTIONS(plan)[idx];
        if (KosTransferAction_STEP(a) == KOS_XFER_EXTENT_ACTION
            && KosTransferAction_EXTENT_NODE(a) == (KosDynNode *) wind
            && KosTransferAction_EXTENT_PHASE(a) == KOS_XFER_EXTENT_PHASE_UNWIND)
          KosTransferPlan_ACTION_INDEX(plan)++;
      }
    }

    if (body_result) {
      if (KosEvalState_StackPushRef(es, body_result) < 0) {
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
    KosEvalState *es,
    PyObject *before_thunk,
    PyObject *after_thunk) {

  KosDynWind *wind = kos_dynwind_new(KosEvalState_DYN_TOP(es),
                                     before_thunk, after_thunk);
  if (! wind)
    return NULL;

  if (kos_dyn_push_noinstall(es, (KosDynNode *) wind) < 0) {
    kos_dynwind_free(wind);
    return NULL;
  }
  return wind;
}


/**
   Unlink wind without running full uninstall (legacy fast pop). Prefer
   kos_dyn_uninstall for extent teardown when thunks must run.
*/
int kos_dyn_pop_wind(KosEvalState *es, KosDynWind *wind) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) wind) {
    PyErr_SetString(PyExc_RuntimeError, "wind node is not at top of chain");
    return -1;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) wind);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) wind) = NULL;
    kos_dynwind_free(wind);
  }

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


