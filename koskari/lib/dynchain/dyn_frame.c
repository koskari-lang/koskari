/* Included by dynchain.c — dyn_frame */
/* === KosDynFrame implementation === */


KosDynFrame *kos_dynframe_new(
    KosDynNode *parent,
    KosCode *code,
    PyObject *ns,
    KosStackBlock *stack) {

  KosDynFrame *frame = kos_dynnode_alloc();
  if (! frame) {
    Py_XDECREF(ns);
    return NULL;
  }

  kos_dynnode_init(frame, KOS_DYN_FRAME, parent, 0);
  KosDynFrame_CODE(frame) = code;  /* borrowed */
  KosDynFrame_PC(frame) = 0;
  KosDynFrame_ACTING_PC(frame) = 0;
  KosDynFrame_STACK(frame) = stack;  /* ownership transferred */
  KosDynFrame_NS(frame) = ns;  /* ownership transferred (already stole ref) */
  KosDynFrame_FLAGS(frame) = 0;
  KosDynFrame_NATIVE_HANDLER(frame) = NULL;
  return frame;
}


KosDynFrame *kos_dynframe_clone(KosDynFrame *frame) {
  if (! frame)
    return NULL;

  KosStackBlock *cloned_stack = KosStackBlock_DeepCopy(KosDynFrame_STACK(frame));

  KosDynFrame *clone = kos_dynnode_alloc();
  if (! clone) {
    if (cloned_stack) {
      KosStackBlock *b = cloned_stack;
      while (b) {
        KosStackBlock *p = KosStackBlock_PARENT(b);
        KosStackBlock_Release(b);
        b = p;
      }
    }
    return NULL;
  }

  kos_dynnode_init(clone, KOS_DYN_FRAME, NULL, KosDynNode_CAPS(frame));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(frame) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(frame);
  KosDynFrame_CODE(clone) = KosDynFrame_CODE(frame);  /* borrowed */
  KosDynFrame_PC(clone) = KosDynFrame_PC(frame);
  KosDynFrame_ACTING_PC(clone) = KosDynFrame_ACTING_PC(frame);
  KosDynFrame_STACK(clone) = cloned_stack;
  KosDynFrame_NS(clone) = KosDynFrame_NS(frame);
  Py_XINCREF(KosDynFrame_NS(clone));
  KosDynFrame_FLAGS(clone) = KosDynFrame_FLAGS(frame);
  KosDynFrame_NATIVE_HANDLER(clone) = KosDynFrame_NATIVE_HANDLER(frame);
  return clone;
}


void kos_dynframe_free(KosDynFrame *frame) {
  if (! frame)
    return;

  /* Release the stack block chain */
  KosStackBlock *block = KosDynFrame_STACK(frame);
  while (block) {
    KosStackBlock *p = KosStackBlock_PARENT(block);
    KosStackBlock_Release(block);
    block = p;
  }

  Py_XDECREF(KosDynFrame_NS(frame));
  /* code is borrowed, don't decref */

  kos_dynnode_return_to_freelist(frame);
}


KosDynFrame *kos_dyn_push_frame(
    KosContext *ctx,
    KosCode *code,
    PyObject *ns) {

  KosDynFrame *frame = kos_dynframe_new(KosContext_DYN_TOP(ctx), code, ns, NULL);
  if (! frame)
    return NULL;

  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  KosContext_FRAME_TOP(ctx) = frame;
  KosContext_CHAIN_VERSION(ctx)++;

  return frame;
}


KosDynFrame *kos_dyn_push_frame_empty(KosContext *ctx) {
  return kos_dyn_push_frame(ctx, NULL, NULL);
}


void kos_dynframe_reset_for_tail(KosDynFrame *frame) {
  if (! frame)
    return;

  /* Release the stack block chain */
  KosStackBlock *block = KosDynFrame_STACK(frame);
  while (block) {
    KosStackBlock *p = KosStackBlock_PARENT(block);
    KosStackBlock_Release(block);
    block = p;
  }
  KosDynFrame_STACK(frame) = NULL;

  /* Replace ns with empty dict */
  Py_XDECREF(KosDynFrame_NS(frame));
  KosDynFrame_NS(frame) = NULL;

  KosDynFrame_CODE(frame) = NULL;
  KosDynFrame_PC(frame) = 0;
  KosDynFrame_ACTING_PC(frame) = 0;
  KosDynFrame_FLAGS(frame) = 0;
  KosDynFrame_NATIVE_HANDLER(frame) = NULL;
}


int kos_dyn_pop_frame(KosContext *ctx) {
  if (! KosContext_DYN_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no node to pop");
    return -1;
  }

  if (! KosDynNode_IS_FRAME(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }

  KosDynNode *old_top = KosContext_DYN_TOP(ctx);
  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(old_top);
  KosContext_CHAIN_VERSION(ctx)++;

  kos_dynnode_free(old_top);
  kos_update_frame_top(ctx);

  return 0;
}


/**
   Attach an existing frame to the context's dynamic chain (no allocation).

   Reparents the frame so its parent is KosContext_DYN_TOP(ctx), then sets it as
   dyn_top/frame_top. Used to run a generator frame in the current context
   (e.g. async gen step). Does not free the frame on detach.

   :param ctx: execution context
   :param frame: existing frame (not freed by this module)

   :return: 0 on success, -1 on error
*/
int kos_dyn_attach_frame(KosContext *ctx, KosDynFrame *frame) {
  if (! frame) {
    PyErr_SetString(PyExc_RuntimeError, "NULL frame for attach");
    return -1;
  }
  KosDynNode_PARENT(frame) = KosContext_DYN_TOP(ctx);
  KosContext_DYN_TOP(ctx) = (KosDynNode *) frame;
  KosContext_FRAME_TOP(ctx) = frame;
  KosContext_CHAIN_VERSION(ctx)++;
  return 0;
}


/**
   Detach the top frame from the context's dynamic chain without freeing it.

   Sets KosContext_DYN_TOP(ctx) to the frame's parent and updates frame_top. Clears the
   detached frame's parent link so it is fully disconnected from the chain.
   The frame is returned in *frame_out; caller may keep it (e.g. for async
   gen storage in agen->ctx).

   :param ctx: execution context
   :param frame_out: output; set to the detached frame (may be NULL)

   :return: 0 on success, -1 if top is not a frame
*/
int kos_dyn_detach_top_frame(KosContext *ctx, KosDynFrame **frame_out) {
  if (! KosContext_DYN_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no node to detach");
    return -1;
  }
  if (! KosDynNode_IS_FRAME(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }
  KosDynFrame *frame = (KosDynFrame *) KosContext_DYN_TOP(ctx);
  KosContext_DYN_TOP(ctx) = KosDynNode_PARENT(frame);
  KosContext_CHAIN_VERSION(ctx)++;
  kos_update_frame_top(ctx);
  KosDynNode_PARENT(frame) = NULL;
  if (frame_out)
    *frame_out = frame;
  return 0;
}
