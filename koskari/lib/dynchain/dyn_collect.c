/* Included by dynchain.c — dyn_collect */
/* === KosDynCollect implementation === */


static void kos_dyncollect_free(KosDynCollect *col_node);


/**
   COLLECT region: active collector scope namespace.
*/
static KosDynCollect *kos_dyncollect_new(
    KosDynNode *parent,
    PyObject *ns) {
  KosDynCollect *col_node = (KosDynCollect *) kos_dynnode_alloc();
  KosStackBlock *stack;

  if (! col_node) {
    Py_DECREF(ns);
    return NULL;
  }

  stack = KosStackBlock_NewEmpty();
  if (! stack) {
    kos_dynnode_return_to_freelist((KosDynNode *) col_node);
    Py_DECREF(ns);
    return NULL;
  }

  kos_dynnode_init((KosDynNode *) col_node, KOS_DYN_COLLECT, parent, 0);
  KosDynNode_NS((KosDynNode *) col_node) = ns;
  KosDynNode_STACK((KosDynNode *) col_node) = stack;
  return col_node;
}


/**
   Clone node namespace for continuation capture.
*/
static KosDynCollect *kos_dyncollect_clone(KosDynCollect *col_node) {
  if (! col_node)
    return NULL;

  PyObject *ns = KosDynNode_NS((KosDynNode *) col_node);
  Py_INCREF(ns);
  KosDynCollect *clone = kos_dyncollect_new(NULL, ns);
  if (! clone)
    return NULL;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID((KosDynNode *) col_node);
  if (KosDynNode_STACK((KosDynNode *) col_node)) {
    KosDynNode_STACK((KosDynNode *) clone) =
        KosStackBlock_DeepCopy(KosDynNode_STACK((KosDynNode *) col_node));
    if (! KosDynNode_STACK((KosDynNode *) clone)) {
      kos_dyncollect_free(clone);
      return NULL;
    }
  }
  return clone;
}


/**
   DECREF node namespace; return node to freelist.
*/
static void kos_dyncollect_free(KosDynCollect *col_node) {
  if (! col_node)
    return;

  Py_DECREF(KosDynNode_NS((KosDynNode *) col_node));
  kos_dynnode_return_to_freelist((KosDynNode *) col_node);
}


static int dynnode_to_dict_collect(KosDynNode *node, PyObject *data) {

  (void) node;
  (void) data;
  return 0;
}


/**
   Collector namespace unwind is implicit via cache refresh after node removal.
*/
void kos_dyn_collect_unwind(KosEvalState *es, KosDynCollect *node) {
  (void) es;
  (void) node;
}


/**
   Push COLLECT as DYN_TOP. Callers: PUSH_COLLECT.
*/
KosDynCollect *kos_dyn_push_collect(
    KosEvalState *es,
    PyObject *ns) {
  // CONTEXT: dirty, touches context directly
  KosDynCollect *col_node = kos_dyncollect_new(
      KosEvalState_DYN_TOP(es), ns);
  if (! col_node)
    return NULL;

  if (kos_dyn_push_noinstall(es, (KosDynNode *) col_node) < 0) {
    kos_dyncollect_free(col_node);
    return NULL;
  }
  return col_node;
}


/**
   Pop COLLECT when top. Callers: POP_COLLECT.
*/
int kos_dyn_pop_collect(KosEvalState *es, KosDynCollect *node) {
  if (! KosEvalState_DYN_TOP(es) ||
      KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "COLLECT node is not at top of chain");
    return -1;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dyncollect_free(node);
  }
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_COLLECT: visit node-owned namespace for collector.
*/
static int dynnode_traverse_collect(KosDynNode *node, visitproc visit, void *arg) {
  int tr = KosStackBlock_Traverse(KosDynNode_STACK(node), visit, arg);
  if (tr != 0)
    return tr;
  Py_VISIT(KosDynNode_NS(node));
  return 0;
}
