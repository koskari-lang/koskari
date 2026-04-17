/* Included by dynchain.c — dyn_yield_from */
/* === KosDynYieldFrom implementation === */


/**
   YIELD_FROM node: delegate iterator, phase state for close/send. Callers:
   kos_dyn_push_yield_from.
*/
KosDynYieldFrom *kos_dynyieldfrom_new(
    KosDynNode *parent,
    PyObject *delegate,
    uint8_t kind) {

  KosDynYieldFrom *node = (KosDynYieldFrom *) kos_dynnode_alloc();
  uint32_t caps;

  if (! node) {
    return NULL;
  }

  caps = KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP;
  /* KOS_YF_KOS_ASYNC_GEN (4): flattened iterator boundary with main eval loop */
  if (kind == (uint8_t) 4)
    caps |= KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_YIELD_FROM, parent, caps);
  Py_INCREF(delegate);
  KosDynYieldFrom_DELEGATE(node) = delegate;
  KosDynYieldFrom_KIND(node) = kind;
  KosDynYieldFrom_PENDING_ACTION(node) = 0;
  KosDynYieldFrom_PHASE_SET(node, KOS_YF_PHASE_DRIVING);
  KosDynYieldFrom_PENDING_ARG(node) = NULL;
  KosDynYieldFrom_PENDING_ARG_TYPE(node) = NULL;
  KosDynYieldFrom_INFLIGHT(node) = NULL;
  KosDynYieldFrom_ROLLBACK_PC(node) = 0;
  KosDynYieldFrom_DELEGATING_FRAME_SET(node, NULL);
  return node;
}


/**
   DECREF delegate and pending refs; return to freelist.
*/
static void kos_dynyieldfrom_free(KosDynYieldFrom *node) {
  if (! node)
    return;

  Py_XDECREF(KosDynYieldFrom_DELEGATE(node));
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG(node));
  Py_XDECREF(KosDynYieldFrom_PENDING_ARG_TYPE(node));
  Py_XDECREF(KosDynYieldFrom_INFLIGHT(node));
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


/**
   Clone delegate and pending state for continuation capture.
*/
static KosDynYieldFrom *kos_dynyieldfrom_clone(KosDynYieldFrom *node) {

  KosDynYieldFrom *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;

  kos_dynnode_init((KosDynNode *) clone, KOS_DYN_YIELD_FROM, NULL,
                   KosDynNode_CAPS(node));

  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = KosDynNode_NODE_ID(node);

  KosDynYieldFrom_DELEGATE(clone) = KosDynYieldFrom_DELEGATE(node);
  Py_XINCREF(KosDynYieldFrom_DELEGATE(clone));

  KosDynYieldFrom_KIND(clone) = KosDynYieldFrom_KIND(node);
  KosDynYieldFrom_PENDING_ACTION(clone) = KosDynYieldFrom_PENDING_ACTION(node);
  KosDynYieldFrom_PHASE_SET(clone, KosDynYieldFrom_PHASE(node));
  KosDynYieldFrom_PENDING_ARG(clone) = KosDynYieldFrom_PENDING_ARG(node);
  KosDynYieldFrom_PENDING_ARG_TYPE(clone) = KosDynYieldFrom_PENDING_ARG_TYPE(node);

  if (KosDynYieldFrom_PENDING_ARG(clone))
    Py_INCREF(KosDynYieldFrom_PENDING_ARG(clone));
  if (KosDynYieldFrom_PENDING_ARG_TYPE(clone))
    Py_INCREF(KosDynYieldFrom_PENDING_ARG_TYPE(clone));

  KosDynYieldFrom_INFLIGHT(clone) = KosDynYieldFrom_INFLIGHT(node);
  if (KosDynYieldFrom_INFLIGHT(clone))
    Py_INCREF(KosDynYieldFrom_INFLIGHT(clone));

  KosDynYieldFrom_ROLLBACK_PC(clone) = KosDynYieldFrom_ROLLBACK_PC(node);
  KosDynYieldFrom_DELEGATING_FRAME_SET(
      clone, KosDynYieldFrom_DELEGATING_FRAME(node));

  return clone;
}


/**
   After freeze, dyn_top is KOS_DYN_YIELD_FROM with RECEIVES_ITER_BOUNDARY.
*/
KosDynYieldFrom *kos_dyn_find_yield_from_iter_boundary(KosContext *ctx) {

  KosDynNode *top = KosContext_DYN_TOP(ctx);

  if (top && KosDynNode_IS_YIELD_FROM(top)
      && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY)
      && KosDynYieldFrom_DELEGATING_FRAME((KosDynYieldFrom *) top))
    return (KosDynYieldFrom *) top;
  return NULL;
}


/**
   When the running activation is a frame inside a flattened yield-from
   delegate, walk outward from that frame to find the YIELD_FROM node.
   dyn_top must be a FRAME (not the YIELD_FROM frozen head).
*/
KosDynYieldFrom *kos_dyn_find_yield_from_for_inner_await(KosContext *ctx) {

  KosDynNode *top = KosContext_DYN_TOP(ctx);
  KosDynNode *n;

  if (! top || ! KosDynNode_IS_FRAME(top))
    return NULL;

  for (n = KosDynNode_PARENT(top); n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_IS_YIELD_FROM(n)
        && KosDynNode_HAS_CAP(n, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY)
        && KosDynYieldFrom_DELEGATING_FRAME((KosDynYieldFrom *) n))
      return (KosDynYieldFrom *) n;
  }
  return NULL;
}


/**
   Inner Koskari async gen yielded: push through to delegating frame and
   surface KOS_EV_YIELD to the outer koskari_eval caller.
*/
int kos_dyn_yield_from_deliver_inner_yield(
    KosContext *ctx, KosDynYieldFrom *yf, KosEvent *out_ev) {

  PyObject *payload = KosEvent_PAYLOAD(out_ev);
  KosDynFrame *frame = KosDynYieldFrom_DELEGATING_FRAME(yf);
  KosPyAsyncGen *agen;
  PyObject *const *tos;

  if (! frame)
    frame = KosContext_FRAME_TOP(ctx);
  if (! frame || ! payload)
    return -1;

  if (KosStackBlock_PushRef(&KosDynFrame_STACK(frame), payload) < 0)
    return -1;
  Py_DECREF(payload);
  KosEvent_PAYLOAD(out_ev) = NULL;
  KosEvent_KIND(out_ev) = KOS_EV_NONE;

  agen = (KosPyAsyncGen *) KosDynYieldFrom_DELEGATE(yf);
  agen->in_progress = 0;

  KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_EXPECT_SEND);
  KosDynFrame_PC(frame) = KosDynYieldFrom_ROLLBACK_PC(yf);

  tos = KosStackBlock_TailPtr(KosDynFrame_STACK(frame), 1);
  if (! tos)
    return -1;

  KosEvent_KIND(out_ev) = KOS_EV_YIELD;
  KosEvent_PAYLOAD(out_ev) = *tos;
  Py_INCREF(KosEvent_PAYLOAD(out_ev));

  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 0;
}


/**
   Inner Koskari async gen returned: pop yield-from (void completion).
   Advance delegating frame PC past the YIELD_FROM instruction.
*/
int kos_dyn_yield_from_deliver_inner_return(
    KosContext *ctx, KosDynYieldFrom *yf, KosEvent *out_ev) {

  PyObject *val = KosEvent_PAYLOAD(out_ev);
  KosDynFrame *df = KosDynYieldFrom_DELEGATING_FRAME(yf);
  uint32_t rpc = KosDynYieldFrom_ROLLBACK_PC(yf);
  KosCode *code;
  KosPyAsyncGen *delegate_agen;

  if (val)
    Py_DECREF(val);
  KosEvent_PAYLOAD(out_ev) = NULL;
  KosEvent_KIND(out_ev) = KOS_EV_NONE;

  /*
   Match kos_dyn_next_deliver_return: dyn_top must be the delegate gen frame;
   free it and the GEN node, restore dyn_top to the YIELD_FROM consumer.
   */
  if (kos_dyn_gen_unwind_exhausted(ctx) < 0)
    return -1;

  delegate_agen = (KosPyAsyncGen *) KosDynYieldFrom_DELEGATE(yf);
  if (delegate_agen)
    delegate_agen->in_progress = 0;

  code = df ? KosDynFrame_CODE(df) : NULL;
  if (df && code && (uint32_t) rpc + 1u < KosCode_CODE_LEN(code)) {
    KosDynFrame_PC(df) = rpc + 1u;
    KosDynFrame_ACTING_PC(df) = rpc + 1u;
  }

  KosDynYieldFrom_DELEGATING_FRAME_SET(yf, NULL);
  kos_dyn_pop_yield_from(ctx, yf);
  return 0;
}


/**
   Close delegate on pop (uninstall path). Callers: kos_dyn_uninstall_handler.
*/
void kos_dyn_yieldfrom_unwind(KosContext *ctx, KosDynYieldFrom *node) {
  (void) ctx;
  if (node)
    kos_yieldfrom_close_delegate(node);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_YIELD_FROM: visit the delegate subiterator and
   any pending or in-flight value references.
*/
static int dynnode_traverse_yield_from(KosDynNode *node, visitproc visit, void *arg) {
  KosDynYieldFrom *yf = (KosDynYieldFrom *) node;
  Py_VISIT(KosDynYieldFrom_DELEGATE(yf));
  Py_VISIT(KosDynYieldFrom_PENDING_ARG(yf));
  Py_VISIT(KosDynYieldFrom_PENDING_ARG_TYPE(yf));
  Py_VISIT(KosDynYieldFrom_INFLIGHT(yf));
  return 0;
}

