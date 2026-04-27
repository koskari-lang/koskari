/* Included by dynchain.c — dyn_foreach */
/* === KosDynForeach === */


/**
   FOREACH region: loop PCs, iterator, exhausted flag, for/else PC.
*/
KosDynForeach *kos_dynforeach_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  KosStackBlock *stack = KosStackBlock_NewEmpty();
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
        KosStackBlock_DeepCopy(KosDynNode_STACK((KosDynNode *) node));
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


/**
   Push FOREACH as DYN_TOP. Callers: opcode PUSH_FOREACH.
*/
KosDynForeach *kos_dyn_push_foreach(KosEvalState *es, uint32_t break_pc, uint32_t continue_pc) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynForeach *node = kos_dynforeach_new(KosContext_DYN_TOP(ctx), break_pc, continue_pc);
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
void kos_dyn_pop_foreach(KosEvalState *es, KosDynForeach *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    KosEvalState_SyncFromContext(es);
    return;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynforeach_free(node);
  }
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FOREACH: visit result and iterator refs.
*/
static int dynnode_traverse_foreach(KosDynNode *node, visitproc visit, void *arg) {
  int tr = KosStackBlock_Traverse(KosDynNode_STACK(node), visit, arg);
  if (tr != 0)
    return tr;
  Py_VISIT(KosDynForeach_RESULT((KosDynForeach *) node));
  Py_VISIT(KosDynForeach_ITER((KosDynForeach *) node));
  return 0;
}
