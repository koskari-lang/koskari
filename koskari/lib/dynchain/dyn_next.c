/* Included by dynchain.c — dyn_next */
/* === KOS_DYN_NEXT: standalone NEXT opcode boundary for Koskari async gen === */


void kos_dyn_next_free_payload(KosDynNode *node) {

  Py_CLEAR(KosDynNext_AGEN_OBJ(node));
  Py_CLEAR(KosDynNext_DEFAULT_VAL(node));
  kos_dynnode_return_to_freelist(node);
}


int kos_dyn_push_next_for_kos_agen(
    KosEvalState *es, PyObject *agen_obj, PyObject *default_val, int n) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  // CONTEXT: dirty, touches context directly
  KosDynNode *node = kos_dynnode_alloc();
  if (! node) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  kos_dynnode_init(
      node, KOS_DYN_NEXT, KosContext_DYN_TOP(ctx), KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY);
  Py_INCREF(agen_obj);
  KosDynNext_AGEN_OBJ(node) = agen_obj;
  if (default_val) {
    Py_INCREF(default_val);
    KosDynNext_DEFAULT_VAL(node) = default_val;
  } else {
    KosDynNext_DEFAULT_VAL(node) = NULL;
  }
  KosDynNext_N(node) = n;

  if (kos_dyn_push_noinstall(es, node) < 0) {
    kos_dyn_next_free_payload(node);
    return -1;
  }
  return 0;
}


void kos_dyn_next_abort_after_failed_attach(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (top && KosDynNode_IS_NEXT(top)) {
    KosDynNode *parent = KosDynNode_PARENT(top);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT(top) = NULL;
    kos_dyn_next_free_payload(top);
  } else
    KosEvalState_SyncFromContext(es);
}


KosDynForeach *kos_dyn_find_awaiting_foreach(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *top = KosContext_DYN_TOP(ctx);

  /*
   After freeze, dyn_top can be the FOREACH region (parent of the detached GEN)
   for ITER_NEXT over a Koskari async gen. The awaiting region is not always an
   ancestor of frame_top in the parent walk.
   */
  if (top && KosDynNode_IS_FOREACH(top)) {
    KosDynForeach *fe = (KosDynForeach *) top;
    if (KosDynForeach_ITER_STEP_PHASE(fe) == KOS_FOREACH_ITER_STEP_AWAITING_KOS_AGEN)
      return fe;
  }

  {
    KosDynFrame *cf = KosEvalState_FRAME(es);
    KosDynNode *n;
    if (! cf)
      return NULL;
    for (n = KosDynNode_PARENT((KosDynNode *) cf); n; n = KosDynNode_PARENT(n)) {
      if (KosDynNode_IS_FOREACH(n)) {
        KosDynForeach *fe = (KosDynForeach *) n;
        if (KosDynForeach_ITER_STEP_PHASE(fe) == KOS_FOREACH_ITER_STEP_AWAITING_KOS_AGEN)
          return fe;
      }
    }
  }
  return NULL;
}


KosDynNext *kos_dyn_find_next_above_frame(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *top = KosContext_DYN_TOP(ctx);

  /*
   After kos_dyn_gen_freeze_and_release, dyn_top is the KOS_DYN_NEXT node
   (parent of the detached GEN). The consumer frame is an ancestor of NEXT,
   not a descendant, so walking parent(frame_top) never reaches NEXT.
   */
  if (top && KosDynNode_IS_NEXT(top))
    return (KosDynNext *) top;

  /*
   While the gen frame is still installed, walk outward from the running
   frame: FRAME -> GEN -> NEXT.
   */
  {
    KosDynFrame *cf = KosEvalState_FRAME(es);
    KosDynNode *n;
    if (! cf)
      return NULL;
    for (n = KosDynNode_PARENT((KosDynNode *) cf); n; n = KosDynNode_PARENT(n)) {
      if (KosDynNode_IS_NEXT(n))
        return (KosDynNext *) n;
    }
  }
  return NULL;
}


void kos_dyn_unlink_next_node(KosEvalState *es, KosDynNext *next) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *node = (KosDynNode *) next;
  KosDynNode *p = KosDynNode_PARENT(node);

  KosEvalState_FlushLiveRegsToFrame(es);
  if (KosContext_DYN_TOP(ctx) == node) {
    kos_dyn_commit_dyn_top(es, p);
  } else {
    KosDynNode *c;
    for (c = KosContext_DYN_TOP(ctx); c; c = KosDynNode_PARENT(c)) {
      if (KosDynNode_PARENT(c) == node) {
        KosDynNode_PARENT(c) = p;
        break;
      }
    }
    KosContext_CHAIN_VERSION(ctx)++;
    KosEvalState_RefreshDynCaches(es);
  }

  KosDynNode_PARENT(node) = NULL;
  kos_dyn_next_free_payload(node);
}


int kos_dyn_next_deliver_yield(KosEvalState *es, struct KosEvent *out_ev) {

  KosDynNext *next = kos_dyn_find_next_above_frame(es);
  PyObject *payload;
  KosPyAsyncGen *agen;

  if (! next) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }
  payload = KosEvent_PAYLOAD(out_ev);
  if (KosEvalState_StackPushRef(es, payload) < 0) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }
  Py_DECREF(payload);
  KosEvent_PAYLOAD(out_ev) = NULL;
  KosEvent_KIND(out_ev) = KOS_EV_NONE;

  agen = (KosPyAsyncGen *) KosDynNext_AGEN_OBJ((KosDynNode *) next);
  agen->in_progress = 0;

  kos_dyn_unlink_next_node(es, next);
  return 0;
}


int kos_dyn_next_deliver_return(KosEvalState *es, struct KosEvent *out_ev) {

  KosDynNext *next = kos_dyn_find_next_above_frame(es);
  PyObject *val;
  KosPyAsyncGen *agen;
  int nn;

  if (! next) {
    KosEvalState_SyncFromContext(es);
    return -1;
  }
  val = KosEvent_PAYLOAD(out_ev);
  nn = KosDynNext_N((KosDynNode *) next);

  if (kos_dyn_gen_unwind_exhausted(es) < 0) {
    Py_DECREF(val);
    KosEvalState_SyncFromContext(es);
    return -1;
  }

  agen = (KosPyAsyncGen *) KosDynNext_AGEN_OBJ((KosDynNode *) next);
  agen->closed = 1;
  agen->in_progress = 0;

  if (nn == 2 && KosDynNext_DEFAULT_VAL((KosDynNode *) next)) {
    if (KosEvalState_StackPushRef(
            es,
            KosDynNext_DEFAULT_VAL((KosDynNode *) next)) < 0) {
      Py_DECREF(val);
      KosEvalState_SyncFromContext(es);
      return -1;
    }
    Py_DECREF(val);
  } else if (nn == 1) {
    Py_DECREF(val);
    PyErr_SetNone(PyExc_StopIteration);
    kos_dyn_unlink_next_node(es, next);
    return -1;
  } else {
    Py_DECREF(val);
  }

  KosEvent_PAYLOAD(out_ev) = NULL;
  KosEvent_KIND(out_ev) = KOS_EV_NONE;

  kos_dyn_unlink_next_node(es, next);
  return 0;
}


int kos_dyn_need_kos_agen_boundary_deliver(KosEvalState *es) {

  return kos_dyn_find_next_above_frame(es) != NULL
      || kos_dyn_find_awaiting_foreach(es) != NULL
      || kos_dyn_find_yield_from_iter_boundary(es) != NULL;
}


KosDynNext *kos_dyn_next_clone(KosDynNext *node) {

  KosDynNext *clone = (KosDynNext *) kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(
      (KosDynNode *) clone, KOS_DYN_NEXT, NULL, KosDynNode_CAPS((KosDynNode *) node));
  KosDynNode_FLAGS((KosDynNode *) clone) =
      KosDynNode_FLAGS((KosDynNode *) node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID((KosDynNode *) clone) = KosDynNode_NODE_ID((KosDynNode *) node);
  Py_XINCREF(KosDynNext_AGEN_OBJ((KosDynNode *) node));
  KosDynNext_AGEN_OBJ((KosDynNode *) clone) = KosDynNext_AGEN_OBJ((KosDynNode *) node);
  Py_XINCREF(KosDynNext_DEFAULT_VAL((KosDynNode *) node));
  KosDynNext_DEFAULT_VAL((KosDynNode *) clone) = KosDynNext_DEFAULT_VAL((KosDynNode *) node);
  KosDynNext_N((KosDynNode *) clone) = KosDynNext_N((KosDynNode *) node);
  return clone;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_NEXT: visit async gen object and optional default.
*/
static int dynnode_traverse_next(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynNext_AGEN_OBJ(node));
  Py_VISIT(KosDynNext_DEFAULT_VAL(node));
  return 0;
}
