/* Included by dynchain.c — dyn_foreach */
/* === KosDynForeach === */


/**
   FOREACH region: loop PCs, iterator, exhausted flag, for/else PC.
*/
KosDynForeach *kos_dynforeach_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  KosSegment *stack = KosStack_NewEmpty();
  if (! stack) {
    kos_dynnode_return_to_freelist((KosDynNode *) node);
    return NULL;
  }

  kos_dynnode_init(node, KOS_DYN_FOREACH, parent, 0);
  KosDynForeach_BREAK_PC(node) = break_pc;
  KosDynForeach_CONTINUE_PC(node) = continue_pc;
  KosDynForeach_ENABLED(node) = 1;
  KosDynForeach_RESULT(node) = NULL;
  KosDynForeach_ITER(node) = NULL;
  KosDynForeach_EXHAUSTED(node) = 0;
  KosDynForeach_ITER_NEXT_ELSE_PC(node) = 0;
  KosDynForeach_ITER_STEP_PHASE(node) = KOS_FOREACH_ITER_STEP_IDLE;
  KosDynNode_STACK((KosDynNode *) node) = stack;
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
  KosDynForeach_ITER_STEP_PHASE(clone) = KosDynForeach_ITER_STEP_PHASE(node);
  if (KosDynNode_STACK((KosDynNode *) node)) {
    KosDynNode_STACK((KosDynNode *) clone) =
        KosStack_DeepCopy(KosDynNode_STACK((KosDynNode *) node));
    if (! KosDynNode_STACK((KosDynNode *) clone)) {
      kos_dynforeach_free(clone);
      return NULL;
    }
  }
  return clone;
}


/**
   DECREF result and iterator; return node to freelist.
*/
void kos_dynforeach_free(KosDynForeach *node) {
  Py_XDECREF(KosDynForeach_RESULT(node));
  Py_XDECREF(KosDynForeach_ITER(node));
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
  if (snapshot_set_uint(data, "iter_step_phase",
      KosDynForeach_ITER_STEP_PHASE((KosDynForeach *) node)) < 0)
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
   Pop FOREACH when top. Callers: abort_push_region, kos_dyn_transfer_to_loop.
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

  PyObject *iter;

  if (KosEvalState_StackPushRef(es, val) < 0) {
    return -1;
  }
  Py_DECREF(val);

  kos_dyn_foreach_set_iter_step_phase(fe, KOS_FOREACH_ITER_STEP_IDLE);
  iter = KosDynForeach_ITER(fe);
  if (KosPyAsyncGen_Check(iter))
    ((KosPyAsyncGen *) iter)->in_progress = 0;
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

  PyObject *iter = KosDynForeach_ITER(fe);
  if (KosPyAsyncGen_Check(iter)) {
    ((KosPyAsyncGen *) iter)->closed = 1;
    ((KosPyAsyncGen *) iter)->in_progress = 0;
  }

  KosDynForeach_EXHAUSTED(fe) = 1;
  KosEvalState_SET_PC(es, KosDynForeach_ITER_NEXT_ELSE_PC(fe));
  kos_dyn_foreach_set_iter_step_phase(fe, KOS_FOREACH_ITER_STEP_IDLE);

  Py_XDECREF(val);
  return 0;
}


int kos_dyn_foreach_deliver_return_kos_agen(
    KosEvalState *es,
    KosDynForeach *fe,
    PyObject *val) {

  if (kos_dyn_gen_unwind_exhausted(es) < 0) {
    Py_DECREF(val);
    return -1;
  }

  return kos_dyn_foreach_deliver_exhausted(es, fe, val);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FOREACH: visit result and iterator refs.
*/
static int dynnode_traverse_foreach(KosDynNode *node, visitproc visit, void *arg) {
  int tr = KosStack_Traverse(KosDynNode_STACK(node), visit, arg);
  if (tr != 0)
    return tr;
  Py_VISIT(KosDynForeach_RESULT((KosDynForeach *) node));
  Py_VISIT(KosDynForeach_ITER((KosDynForeach *) node));
  return 0;
}
