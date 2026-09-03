/* Included by dynchain.c — dyn_foreach */
/* === KosDynForeach === */


/**
   FOREACH region: loop PCs, iterator, exhausted flag, for/else PC.
*/
KosDynForeach *kos_dynforeach_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  kos_dynnode_init(node, KOS_DYN_FOREACH, parent, 0);
  KosDynForeach_BREAK_PC(node) = break_pc;
  KosDynForeach_CONTINUE_PC(node) = continue_pc;
  KosDynForeach_ENABLED(node) = 1;
  KosDynForeach_RESULT(node) = NULL;
  KosDynForeach_ITER(node) = NULL;
  KosDynForeach_EXHAUSTED(node) = 0;
  KosDynForeach_ITER_NEXT_ELSE_PC(node) = 0;
  KosDynForeach_REQ(node)->iter = NULL;
  KosDynForeach_REQ(node)->mode = KOS_IREQ_IDLE;
  kos_dynnode_use_inline_segment((KosDynNode *) node);
  return node;
}


/**
   Clone iterator and result refs for continuation capture.
*/
KosDynForeach *kos_dynforeach_clone(KosDynForeach *node) {
  KosDynForeach *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_FOREACH, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);
  KosDynForeach_BREAK_PC(clone) = KosDynForeach_BREAK_PC(node);
  KosDynForeach_CONTINUE_PC(clone) = KosDynForeach_CONTINUE_PC(node);
  KosDynForeach_ENABLED(clone) = KosDynForeach_ENABLED(node);
  KosDynForeach_RESULT(clone) = KosDynForeach_RESULT(node);
  Py_XINCREF(KosDynForeach_RESULT(clone));
  KosDynForeach_ITER(clone) = KosDynForeach_ITER(node);
  Py_XINCREF(KosDynForeach_ITER(clone));
  KosDynForeach_EXHAUSTED(clone) = KosDynForeach_EXHAUSTED(node);
  KosDynForeach_ITER_NEXT_ELSE_PC(clone) = KosDynForeach_ITER_NEXT_ELSE_PC(node);
  *KosDynForeach_REQ(clone) = *KosDynForeach_REQ(node);
  Py_XINCREF(KosDynForeach_REQ(clone)->iter);
  return clone;
}


/**
   DECREF result and iterator; return node to freelist.
*/
void kos_dynforeach_free(KosDynForeach *node) {
  Py_XDECREF(KosDynForeach_RESULT(node));
  Py_XDECREF(KosDynForeach_ITER(node));
  Py_XDECREF(KosDynForeach_REQ(node)->iter);
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int dynnode_to_dict_foreach(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "break_pc",
                        KosDynForeach_BREAK_PC((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "continue_pc",
                        KosDynForeach_CONTINUE_PC((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "enabled",
                       KosDynForeach_ENABLED((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "result",
      KosDynForeach_RESULT((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "iter",
      KosDynForeach_ITER((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "exhausted",
      KosDynForeach_EXHAUSTED((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "iter_next_else_pc",
      KosDynForeach_ITER_NEXT_ELSE_PC((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "req_mode",
      KosDynForeach_REQ(node)->mode) < 0)
    return -1;
  return 0;
}


/**
   Push FOREACH as DYN_TOP. Callers: opcode PUSH_FOREACH.
*/
KosDynForeach *kos_dyn_push_foreach(KosEvalState *es, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynforeach_new(KosEvalState_DYN_TOP(es), break_pc, continue_pc);
  if (! node)
    return NULL;
  if (kos_dyn_push_noinstall(es, (KosDynNode *) node) < 0) {
    kos_dynforeach_free(node);
    return NULL;
  }
  return node;
}


/**
   Pop FOREACH when top. Callers: abort_push_region, kos_dyn_unwind_to_loop.
*/
int kos_dyn_pop_foreach(KosEvalState *es, KosDynForeach *node) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "FOREACH node is not at top of chain");
    return -1;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynforeach_free(node);
  }
  return 0;
}


int kos_dyn_foreach_deliver_yield_kos_agen(
    KosEvalState *es,
    KosDynForeach *fe,
    PyObject *val) {

  if (KosEvalState_StackPushRef(es, val) < 0) {
    return -1;
  }
  Py_DECREF(val);

  kos_dyn_iter_request_finish((KosDynNode *) fe, 0);
  kos_dyn_iter_request_set_mode((KosDynNode *) fe, KOS_IREQ_IDLE);
  return 0;
}


/**
   Exhaustion tail shared by the return delivery (GEN just unwound) and
   the raise-walk StopIteration conversion (GEN already finished): mark
   the iterator closed, flag the loop exhausted, and resume at the
   else pc. val consumed.
*/
int kos_dyn_foreach_deliver_exhausted(
    KosEvalState *es,
    KosDynForeach *fe,
    PyObject *val) {

  kos_dyn_iter_request_finish((KosDynNode *) fe, 1);

  KosDynForeach_EXHAUSTED(fe) = 1;
  KosEvalState_SET_PC(es, KosDynForeach_ITER_NEXT_ELSE_PC(fe));
  kos_dyn_iter_request_set_mode((KosDynNode *) fe, KOS_IREQ_IDLE);

  Py_XDECREF(val);
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FOREACH: visit result and iterator refs.
*/
static int dynnode_traverse_foreach(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynForeach_RESULT((KosDynForeach *) node));
  Py_VISIT(KosDynForeach_ITER((KosDynForeach *) node));
  Py_VISIT(KosDynForeach_REQ(node)->iter);
  return 0;
}
