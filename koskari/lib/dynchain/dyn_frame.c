/* Included by dynchain.c — dyn_frame */
/* === KosDynFrame implementation === */


/**
  Allocate a FRAME node: code, namespace, stack ownership per caller.
  Frame takes a strong reference to code.
   Callers: internal frame construction, kos_dyn_push_frame.
*/
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

  if (! stack) {
    stack = KosStackBlock_NewEmpty();
    if (! stack) {
      Py_XDECREF(ns);
      kos_dynnode_return_to_freelist(frame);
      return NULL;
    }
  }

  kos_dynnode_init(frame, KOS_DYN_FRAME, parent, 0);
  Py_XINCREF(code);
  KosDynFrame_CODE(frame) = code;  /* owned */
  KosDynFrame_PC(frame) = 0;
  KosDynFrame_ACTING_PC(frame) = 0;
  KosDynFrame_STACK(frame) = stack;  /* ownership transferred */
  KosDynFrame_NS(frame) = ns;  /* ownership transferred (already stole ref) */
  KosDynFrame_FLAGS(frame) = 0;
  KosDynFrame_NATIVE_HANDLER(frame) = NULL;
  return frame;
}


/**
   Deep-copy stack for continuation capture / INSTALL. Callers: kos_dyn_clone.
*/
KosDynFrame *kos_dynframe_clone(KosDynFrame *frame) {
  if (! frame)
    return NULL;

  KosDynFrame *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init(clone, KOS_DYN_FRAME, NULL, KosDynNode_CAPS(frame));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(frame) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(frame);

  KosStackBlock *cst = KosStackBlock_DeepCopy(KosDynFrame_STACK(frame));
  if (! cst) {
    kos_dynnode_return_to_freelist(clone);
    return NULL;
  }
  KosDynFrame_STACK(clone) = cst;
  KosDynFrame_CODE(clone) = KosDynFrame_CODE(frame);  /* owned */
  Py_XINCREF(KosDynFrame_CODE(clone));
  KosDynFrame_PC(clone) = KosDynFrame_PC(frame);
  KosDynFrame_ACTING_PC(clone) = KosDynFrame_ACTING_PC(frame);

  KosDynFrame_NS(clone) = KosDynFrame_NS(frame);
  Py_XINCREF(KosDynFrame_NS(clone));

  KosDynFrame_FLAGS(clone) = KosDynFrame_FLAGS(frame);
  KosDynFrame_NATIVE_HANDLER(clone) = KosDynFrame_NATIVE_HANDLER(frame);
  return clone;
}


/**
   Release stack chain and namespace; return node to freelist. Callers:
   kos_dynnode_free, chain teardown.
*/
void kos_dynframe_free(KosDynFrame *frame) {
  if (! frame)
    return;

  Py_XDECREF((PyObject *) KosDynFrame_CODE(frame));
  KosDynFrame_CODE(frame) = NULL;
  Py_CLEAR(KosDynFrame_NS(frame));

  kos_dynnode_return_to_freelist(frame);
}


static int dynnode_to_dict_frame(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "pc", KosDynFrame_PC((KosDynFrame *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "acting_pc",
                        KosDynFrame_ACTING_PC((KosDynFrame *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "frame_flags",
                        KosDynFrame_FLAGS((KosDynFrame *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "code", (PyObject *) KosDynFrame_CODE((KosDynFrame *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "has_native_handler",
                       KosDynFrame_NATIVE_HANDLER((KosDynFrame *) node) ? 1 : 0) < 0)
    return -1;
  return 0;
}


/**
   Push a new frame with code and ns as DYN_TOP and FRAME_TOP. Callers: eval
   when entering a Python function.
*/
KosDynFrame *kos_dyn_push_frame(
    KosEvalState *es,
    KosCode *code,
    PyObject *ns) {

  KosContext *ctx = KosEvalState_CONTEXT(es);
  // CONTEXT: dirty, touches context directly
  KosDynFrame *frame = kos_dynframe_new(KosContext_DYN_TOP(ctx), code, ns, NULL);
  if (! frame)
    return NULL;

  if (kos_dyn_push_noinstall(es, (KosDynNode *) frame) < 0) {
    kos_dynframe_free(frame);
    return NULL;
  }
  return frame;
}


/**
   Push frame with NULL code/ns (placeholder). Callers: setup paths needing a
   frame before code is bound.
*/
KosDynFrame *kos_dyn_push_frame_empty(KosEvalState *es) {
  return kos_dyn_push_frame(es, NULL, NULL);
}


/**
   Clear stack and metadata for tail-call reuse of the same frame object.
*/
void kos_dynframe_reset_for_tail(KosDynFrame *frame) {
  if (! frame)
    return;

  KosStackBlock_ClearBlock(&KosDynFrame_STACK(frame));

  /* Replace ns with empty dict */
  Py_XDECREF(KosDynFrame_NS(frame));
  KosDynFrame_NS(frame) = NULL;

  Py_XDECREF((PyObject *) KosDynFrame_CODE(frame));
  KosDynFrame_CODE(frame) = NULL;
  KosDynFrame_PC(frame) = 0;
  KosDynFrame_ACTING_PC(frame) = 0;
  KosDynFrame_FLAGS(frame) = 0;
  KosDynFrame_NATIVE_HANDLER(frame) = NULL;
}


/**
   Pop top if it is a FRAME; refresh frame_top. Does not run full uninstall
   (no extent teardown). Callers: simple frame exit when no extra nodes on top.
*/
int kos_dyn_pop_frame(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! KosContext_DYN_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no node to pop");
    return -1;
  }

  if (! KosDynNode_IS_FRAME(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }

  {
    KosDynNode *old_top = KosContext_DYN_TOP(ctx);
    KosDynNode *parent = KosDynNode_PARENT(old_top);

    kos_dyn_commit_dyn_top(es, parent);
    kos_dynnode_free(old_top);
  }

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
int kos_dyn_attach_frame(KosEvalState *es, KosDynFrame *frame) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! frame) {
    PyErr_SetString(PyExc_RuntimeError, "NULL frame for attach");
    return -1;
  }
  KosDynNode_PARENT(frame) = KosContext_DYN_TOP(ctx);
  if (kos_dyn_push_noinstall(es, (KosDynNode *) frame) < 0)
    return -1;
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
int kos_dyn_detach_top_frame(KosEvalState *es, KosDynFrame **frame_out) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  if (! KosContext_DYN_TOP(ctx)) {
    PyErr_SetString(PyExc_RuntimeError, "no node to detach");
    return -1;
  }
  if (! KosDynNode_IS_FRAME(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "top node is not a frame");
    return -1;
  }
  KosDynFrame *frame = (KosDynFrame *) KosContext_DYN_TOP(ctx);
  KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) frame);

  kos_dyn_commit_dyn_top(es, parent);
  KosDynNode_PARENT(frame) = NULL;
  if (frame_out)
    *frame_out = frame;
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FRAME: visit the frame's namespace and every
   object on the frame's stack block chain.  Used by kos_dynnode_traverse.
*/
static int dynnode_traverse_frame(KosDynNode *node, visitproc visit, void *arg) {
  KosDynFrame *frame = (KosDynFrame *) node;
  Py_VISIT(KosDynFrame_CODE(frame));
  Py_VISIT(KosDynFrame_NS(frame));
  {
    int tr = KosStackBlock_Traverse(KosDynFrame_STACK(frame), visit, arg);
    if (tr != 0)
      return tr;
  }
  return 0;
}
