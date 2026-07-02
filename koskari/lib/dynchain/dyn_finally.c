/* Included by dynchain.c — dyn_finally */
/* === KosDynFinally (finally region) === */


/**
   Allocate FINALLY with handler_pc; handles RAISE and RETURN when enabled.
*/
KosDynFinally *kos_dynfinally_new(KosDynNode *parent, uint32_t handler_pc) {
  KosDynFinally *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  KosStackBlock *stack = KosStackBlock_NewEmpty();
  if (! stack) {
    kos_dynnode_return_to_freelist((KosDynNode *) node);
    return NULL;
  }

  kos_dynnode_init(node, KOS_DYN_FINALLY, parent,
                   KOS_DYN_CAP_HANDLES_EXCEPTIONS
                   | KOS_DYN_CAP_INTERCEPTS_RAISE);
  KosDynFinally_HANDLER_PC(node) = handler_pc;
  KosDynFinally_ENABLED(node) = 1;
  KosDynFinally_HANDLING_RAISE(node) = 0;
  KosDynFinally_HANDLING_RETURN(node) = 0;
  KosDynFinally_HANDLING_RESULT(node) = NULL;
  KosDynNode_STACK((KosDynNode *) node) = stack;
  return node;
}


/**
   Clone finally state for continuation templates.
*/
KosDynFinally *kos_dynfinally_clone(KosDynFinally *node) {
  KosDynFinally *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_FINALLY, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynFinally_HANDLER_PC(clone) = KosDynFinally_HANDLER_PC(node);
  KosDynFinally_ENABLED(clone) = KosDynFinally_ENABLED(node);
  KosDynFinally_HANDLING_RAISE(clone) = KosDynFinally_HANDLING_RAISE(node);
  KosDynFinally_HANDLING_RETURN(clone) = KosDynFinally_HANDLING_RETURN(node);
  KosDynFinally_HANDLING_RESULT(clone) = KosDynFinally_HANDLING_RESULT(node);
  Py_XINCREF(KosDynFinally_HANDLING_RESULT(clone));
  if (KosDynNode_STACK((KosDynNode *) node)) {
    KosDynNode_STACK((KosDynNode *) clone) =
        KosStackBlock_DeepCopy(KosDynNode_STACK((KosDynNode *) node));
    if (! KosDynNode_STACK((KosDynNode *) clone)) {
      kos_dynfinally_free(clone);
      return NULL;
    }
  }
  return clone;
}


/**
   DECREF handling result; return node to freelist.
*/
void kos_dynfinally_free(KosDynFinally *node) {
  Py_XDECREF(KosDynFinally_HANDLING_RESULT(node));
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int dynnode_to_dict_finally(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "handler_pc",
                        KosDynFinally_HANDLER_PC((KosDynFinally *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "enabled",
                       KosDynFinally_ENABLED((KosDynFinally *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "handling_raise",
      KosDynFinally_HANDLING_RAISE((KosDynFinally *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "handling_return",
      KosDynFinally_HANDLING_RETURN((KosDynFinally *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "handling_result",
      KosDynFinally_HANDLING_RESULT((KosDynFinally *) node)) < 0)
    return -1;
  return 0;
}


/**
   Push FINALLY as DYN_TOP. Callers: opcode PUSH_FINALLY.
*/
KosDynFinally *kos_dyn_push_finally(KosEvalState *es, uint32_t handler_pc) {
  KosDynFinally *node = kos_dynfinally_new(KosEvalState_DYN_TOP(es), handler_pc);
  if (! node)
    return NULL;
  if (kos_dyn_push_noinstall(es, (KosDynNode *) node) < 0) {
    kos_dynfinally_free(node);
    return NULL;
  }
  return node;
}


/**
   Pop FINALLY when top. Callers: abort_push_region, POP_FINALLY fast path.
*/
int kos_dyn_pop_finally(KosEvalState *es, KosDynFinally *node) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "FINALLY node is not at top of chain");
    return -1;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynfinally_free(node);
  }
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FINALLY: visit handling result when present.
*/
static int dynnode_traverse_finally(KosDynNode *node, visitproc visit, void *arg) {
  int tr = KosStackBlock_Traverse(KosDynNode_STACK(node), visit, arg);
  if (tr != 0)
    return tr;
  Py_VISIT(KosDynFinally_HANDLING_RESULT((KosDynFinally *) node));
  return 0;
}
