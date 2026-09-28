/* Included by dynchain.c — dyn_try */
/* === KosDynTry (try region) === */


/**
   Allocate TRY with handler_pc; HANDLES_EXCEPTIONS cap for kos_dyn_handle.
*/
KosDynTry *kos_dyntry_new(KosDynNode *parent, uint32_t handler_pc) {
  KosDynTry *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  kos_dynnode_init(node, KOS_DYN_TRY, parent,
                   KOS_DYN_CAP_HANDLES_EXCEPTIONS
                   | KOS_DYN_CAP_INTERCEPTS_RAISE);
  KosDynTry_HANDLER_PC(node) = handler_pc;
  KosDynTry_ENABLED(node) = 1;
  KosDynTry_EXC_SAVED(node) = 0;
  KosExc_Init(KosDynTry_SAVED(node));
  kos_dynnode_use_inline_segment((KosDynNode *) node);
  return node;
}


/**
   Clone enabled flag and handler_pc for continuation capture.
*/
KosDynTry *kos_dyntry_clone(KosDynTry *node) {
  KosDynTry *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_TRY, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = kos_dynnode_id(node);
  KosDynTry_HANDLER_PC(clone) = KosDynTry_HANDLER_PC(node);
  KosDynTry_ENABLED(clone) = KosDynTry_ENABLED(node);
  KosDynTry_EXC_SAVED(clone) = KosDynTry_EXC_SAVED(node);
  KosExc_Init(KosDynTry_SAVED(clone));
  KosExc_Copy(KosDynTry_SAVED(clone), KosDynTry_SAVED(node));
  return clone;
}


/**
   Release the saved exception state; return TRY node to freelist.
*/
void kos_dyntry_free(KosDynTry *node) {
  KosExc_Clear(KosDynTry_SAVED(node));
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int dynnode_to_dict_try(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "handler_pc",
                        KosDynTry_HANDLER_PC((KosDynTry *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "enabled",
                       KosDynTry_ENABLED((KosDynTry *) node)) < 0)
    return -1;
  return 0;
}


/**
   Push TRY as DYN_TOP. Callers: opcode PUSH_TRY.
*/
KosDynTry *kos_dyn_push_try(KosEvalState *es, uint32_t handler_pc) {
  KosDynTry *node = kos_dyntry_new(KosEvalState_DYN_TOP(es), handler_pc);
  if (! node)
    return NULL;
  if (kos_dyn_push(es, (KosDynNode *) node) < 0) {
    kos_dyntry_free(node);
    return NULL;
  }
  return node;
}


/**
   Pop TRY when it is top (fast path). Callers: abort_push_region, POP_TRY.
*/
int kos_dyn_pop_try(KosEvalState *es, KosDynTry *node) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "TRY node is not at top of chain");
    return -1;
  }

  KOS_DYN_POP_REQUIRE_KIND(node, KOS_DYN_TRY, "POP_TRY", "TRY");
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_region_leave_exc((KosDynNode *) node);
    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dyntry_free(node);
  }
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_TRY: the saved exception state.
*/
static int dynnode_traverse_try(KosDynNode *node, visitproc visit, void *arg) {
  return KosExc_Traverse(KosDynTry_SAVED((KosDynTry *) node), visit, arg);
}
