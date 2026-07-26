/* Included by dynchain.c — dyn_delim */
/* === KosDynDelim implementation === */


/**
   Allocate DELIM marker for delimited return bookkeeping.
*/
KosDynDelim *kos_dyndelim_new(
    KosDynNode *parent,
    uint8_t mode,
    KosDynNode *chain,
    uint32_t pc,
    KosDynFrame *frame,
    KosDynNode *node) {

  KosDynDelim *delim = kos_dynnode_alloc();
  if (! delim)
    return NULL;

  kos_dynnode_init(delim, KOS_DYN_DELIM, parent,
                   mode == KOS_DYN_DELIM_MODE_INPLACE
                       ? KOS_DYN_CAP_INTERCEPTS_RAISE : 0);
  KosDynDelim_MODE(delim) = mode;
  KosDynDelim_RETURN_CHAIN(delim) = chain;
  KosDynDelim_RETURN_PC(delim) = pc;
  KosDynDelim_RETURN_FRAME_NODE(delim) = (KosDynNode *) frame;
  KosDynDelim_RETURN_NODE(delim) = node;
  KosDynDelim_SAVED_STACK(delim) = NULL;
  KosDynDelim_SAVED_PC(delim) = 0;
  KosDynDelim_SAVED_ACTING_PC(delim) = 0;
  return delim;
}


/**
   Clone DELIM payload.
*/
static KosDynDelim *kos_dyndelim_clone(KosDynDelim *delim) {
  if (! delim)
    return NULL;

  KosDynDelim *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_DELIM, NULL, KosDynNode_CAPS(delim));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(delim) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(delim);
  KosDynDelim_MODE(clone) = KosDynDelim_MODE(delim);
  KosDynDelim_RETURN_CHAIN(clone) = NULL;
  KosDynDelim_RETURN_PC(clone) = KosDynDelim_RETURN_PC(delim);
  KosDynDelim_RETURN_FRAME_NODE(clone) = KosDynDelim_RETURN_FRAME_NODE(delim);
  /* RETURN_NODE is a self-reference meaning "unwind to me", so copying it
     verbatim leaves the clone pointing at the original -- a pointer out of
     a cloned template and into the live chain, the same hazard
     kos_chain_detach_template guards against for parent. An unwind then
     targets the original, passes through the clone, and frees it while
     POP_PROMPT is still holding it. Re-point self-references at the
     clone; anything else is a genuine external target and is copied. */
  KosDynDelim_RETURN_NODE(clone) =
      (KosDynDelim_RETURN_NODE(delim) == (KosDynNode *) delim)
          ? (KosDynNode *) clone
          : KosDynDelim_RETURN_NODE(delim);
  KosDynDelim_SAVED_STACK(clone) = NULL;
  KosDynDelim_SAVED_PC(clone) = KosDynDelim_SAVED_PC(delim);
  KosDynDelim_SAVED_ACTING_PC(clone) = KosDynDelim_SAVED_ACTING_PC(delim);
  return clone;
}


/**
   Free DELIM payload and node.
*/
void kos_dyndelim_free(KosDynDelim *delim) {
  if (! delim)
    return;

  if (KosDynDelim_RETURN_CHAIN(delim)) {
    kos_dynchain_free(KosDynDelim_RETURN_CHAIN(delim), NULL);
    KosDynDelim_RETURN_CHAIN(delim) = NULL;
  }

  if (KosDynDelim_SAVED_STACK(delim)) {
    KosStackBlock_ReleaseChain(KosDynDelim_SAVED_STACK(delim));
    KosDynDelim_SAVED_STACK(delim) = NULL;
  }

  kos_dynnode_return_to_freelist(delim);
}


static int dynnode_to_dict_delim(KosDynNode *node, PyObject *data) {

  KosDynDelim *delim = (KosDynDelim *) node;
  if (snapshot_set_int(data, "mode", (int) KosDynDelim_MODE(delim)) < 0)
    return -1;
  if (snapshot_set_uint(data, "pc", KosDynDelim_RETURN_PC(delim)) < 0)
    return -1;
  if (snapshot_set_uint(data, "frame_node_id",
      KosDynDelim_RETURN_FRAME_NODE(delim)
          ? KosDynNode_NODE_ID(KosDynDelim_RETURN_FRAME_NODE(delim))
          : 0) < 0)
    return -1;
  if (snapshot_set_uint(data, "return_node_id",
      KosDynDelim_RETURN_NODE(delim)
          ? KosDynNode_NODE_ID(KosDynDelim_RETURN_NODE(delim))
          : 0) < 0)
    return -1;
  if (snapshot_set_uint(data, "saved_pc", KosDynDelim_SAVED_PC(delim)) < 0)
    return -1;
  if (snapshot_set_int(data, "has_saved_stack",
      KosDynDelim_SAVED_STACK(delim) ? 1 : 0) < 0)
    return -1;
  return 0;
}


/**
   Push DELIM marker via kos_dyn_push.
*/
KosDynDelim *kos_dyn_push_delim(
    KosEvalState *es,
    uint8_t mode,
    KosDynNode *chain,
    uint32_t pc,
    KosDynFrame *frame,
    KosDynNode *node) {

  // CONTEXT: dirty, touches context directly
  KosDynDelim *delim = kos_dyndelim_new(KosEvalState_DYN_TOP(es),
                                        mode,
                                        chain,
                                        pc,
                                        frame,
                                        node);
  if (! delim)
    return NULL;

  if (kos_dyn_push(es, (KosDynNode *) delim) < 0) {
    kos_dyndelim_free(delim);
    return NULL;
  }

  return delim;
}


/**
   Pop DELIM marker from top of chain.
*/
int kos_dyn_pop_delim(KosEvalState *es, KosDynDelim *delim) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) delim) {
    PyErr_SetString(PyExc_RuntimeError, "DELIM node is not at top of chain");
    return -1;
  }

  kos_dyn_uninstall_handler(es, (KosDynNode *) delim, KOS_XFER_EVENT_NONE);
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) delim);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) delim) = NULL;
    kos_dyndelim_free(delim);
  }
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_DELIM: visit owned return chain and displaced
   stack payloads if present.
*/
static int dynnode_traverse_delim(KosDynNode *node, visitproc visit, void *arg) {
  KosDynNode *chain = KosDynDelim_RETURN_CHAIN((KosDynDelim *) node);
  KosStackBlock *saved = KosDynDelim_SAVED_STACK((KosDynDelim *) node);

  if (saved) {
    int tr = KosStackBlock_Traverse(saved, visit, arg);
    if (tr != 0)
      return tr;
  }
  if (! chain)
    return 0;
  return kos_dynchain_traverse(chain, NULL, visit, arg);
}
