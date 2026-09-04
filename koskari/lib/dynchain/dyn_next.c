/* Included by dynchain.c — dyn_next */
/* === KOS_DYN_NEXT: standalone NEXT opcode iteration node for Koskari async gen === */


void kos_dyn_next_free_payload(KosDynNode *node) {

  Py_CLEAR(KosDynNext_REQ(node)->iter);
  Py_CLEAR(KosDynNext_DEFAULT_VAL(node));
  kos_dynnode_return_to_freelist(node);
}


int kos_dyn_push_next_node(
    KosEvalState *es, PyObject *iter_obj, PyObject *default_val, int n) {

  // CONTEXT: dirty, touches context directly
  KosDynNode *node = kos_dynnode_alloc();
  if (! node) {
    return -1;
  }

  kos_dynnode_init(
      node, KOS_DYN_NEXT, KosEvalState_DYN_TOP(es), KOS_DYN_CAP_ITER_RECEIVER);
  Py_INCREF(iter_obj);
  kos_dyn_iter_request_bind_iter(KosDynNext_REQ(node), iter_obj);
  KosDynNext_REQ(node)->mode = KOS_IREQ_IDLE;
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

  KosDynNode *top = KosEvalState_DYN_TOP(es);
  if (top && KosDynNode_IS_NEXT(top)) {
    KosDynNode *parent = KosDynNode_PARENT(top);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT(top) = NULL;
    kos_dyn_next_free_payload(top);
  } else {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_next_abort_after_failed_attach: top is not NEXT");
  }
}


void kos_dyn_unlink_next_node(KosEvalState *es, KosDynNext *next) {

  KosDynNode *node = (KosDynNode *) next;
  KosDynNode *p = KosDynNode_PARENT(node);

  if (KosEvalState_DYN_TOP(es) == node) {
    kos_dyn_commit_dyn_top(es, p);
  } else {
    KosDynNode *c;
    for (c = KosEvalState_DYN_TOP(es); c; c = KosDynNode_PARENT(c)) {
      if (KosDynNode_PARENT(c) == node) {
        KosDynNode_PARENT(c) = p;
        break;
      }
    }
    kos_dyn_resync_chain(es);
  }

  KosDynNode_PARENT(node) = NULL;
  kos_dyn_next_free_payload(node);
}


int kos_dyn_next_deliver_yield(
    KosEvalState *es, KosDynNext *next, PyObject *val) {

  if (KosEvalState_StackPUSH(es, val) < 0)
    return -1;  /* the push consumed val, failure included */

  kos_dyn_iter_request_finish_req(KosDynNext_REQ(next), 0);

  kos_dyn_unlink_next_node(es, next);
  return 0;
}


/**
   Exhaustion tail shared by the return delivery (GEN just unwound) and
   the raise-walk StopIteration conversion (GEN already finished): mark
   the iterator closed, then honor the NEXT arity -- 2-arg pushes the
   default, 1-arg re-raises StopIteration to the caller. val consumed.
*/
int kos_dyn_next_deliver_exhausted(
    KosEvalState *es,
    KosDynNext *next,
    PyObject *val) {

  int nn = KosDynNext_N((KosDynNode *) next);

  kos_dyn_iter_request_finish_req(KosDynNext_REQ(next), 1);

  if (nn == 2 && KosDynNext_DEFAULT_VAL((KosDynNode *) next)) {
    if (KosEvalState_StackPushRef(
            es,
            KosDynNext_DEFAULT_VAL((KosDynNode *) next)) < 0) {
      Py_XDECREF(val);
      return -1;
    }
    Py_XDECREF(val);
  } else if (nn == 1) {
    Py_XDECREF(val);
    PyErr_SetNone(PyExc_StopIteration);
    kos_dyn_unlink_next_node(es, next);
    return -1;
  } else {
    Py_XDECREF(val);
  }

  kos_dyn_unlink_next_node(es, next);
  return 0;
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
  Py_XINCREF(KosDynNext_REQ(node)->iter);
  *KosDynNext_REQ(clone) = *KosDynNext_REQ(node);
  Py_XINCREF(KosDynNext_DEFAULT_VAL((KosDynNode *) node));
  KosDynNext_DEFAULT_VAL((KosDynNode *) clone) = KosDynNext_DEFAULT_VAL((KosDynNode *) node);
  KosDynNext_N((KosDynNode *) clone) = KosDynNext_N((KosDynNode *) node);
  return clone;
}


static int dynnode_to_dict_next(KosDynNode *node, PyObject *data) {

  if (snapshot_set_obj(data, "iter", KosDynNext_REQ(node)->iter) < 0)
    return -1;
  if (snapshot_set_uint(data, "req_mode", KosDynNext_REQ(node)->mode) < 0)
    return -1;
  if (snapshot_set_obj(data, "default_val", KosDynNext_DEFAULT_VAL(node)) < 0)
    return -1;
  if (snapshot_set_int(data, "n", KosDynNext_N(node)) < 0)
    return -1;
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_NEXT: visit async gen object and optional default.
*/
static int dynnode_traverse_next(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynNext_REQ(node)->iter);
  Py_VISIT(KosDynNext_DEFAULT_VAL(node));
  return 0;
}
