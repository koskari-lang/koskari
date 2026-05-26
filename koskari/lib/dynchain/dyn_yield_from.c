/* Included by dynchain.c — dyn_yield_from */
/* === KosDynYieldFrom implementation === */


static PyObject *kos_dynyieldfrom_str_close = NULL;
static PyObject *kos_dynyieldfrom_str_aclose = NULL;


static int kos_dynyieldfrom_strings_init(void) {

  STR_CONST(kos_dynyieldfrom_str_close, "close");
  STR_CONST(kos_dynyieldfrom_str_aclose, "aclose");
  if (! kos_dynyieldfrom_str_close || ! kos_dynyieldfrom_str_aclose)
    return -1;
  return 0;
}


static void kos_dynyieldfrom_strings_clear(void) {

  Py_CLEAR(kos_dynyieldfrom_str_close);
  Py_CLEAR(kos_dynyieldfrom_str_aclose);
}


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


static int dynnode_to_dict_yield_from(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "kind",
                        KosDynYieldFrom_KIND((KosDynYieldFrom *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "pending_action",
      KosDynYieldFrom_PENDING_ACTION((KosDynYieldFrom *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "phase",
      KosDynYieldFrom_PHASE((KosDynYieldFrom *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "delegate",
      KosDynYieldFrom_DELEGATE((KosDynYieldFrom *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "pending_arg",
      KosDynYieldFrom_PENDING_ARG((KosDynYieldFrom *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "pending_arg_type",
      KosDynYieldFrom_PENDING_ARG_TYPE((KosDynYieldFrom *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "inflight",
      KosDynYieldFrom_INFLIGHT((KosDynYieldFrom *) node)) < 0)
    return -1;
  return 0;
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

  return clone;
}


/**
   Locate flattened yield-from iterator-boundary consumer from current chain
   state.  Works both after freeze (dyn_top is YIELD_FROM) and while running
   in the delegate frame (walk ancestors from FRAME).
*/
KosDynYieldFrom *kos_dyn_find_yield_from_iter_boundary(KosEvalState *es) {

  KosDynNode *top = KosEvalState_DYN_TOP(es);
  KosDynNode *n;

  if (top && KosDynNode_IS_YIELD_FROM(top)
      && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY))
    return (KosDynYieldFrom *) top;
  if (! top || ! KosDynNode_IS_FRAME(top))
    return NULL;

  for (n = KosDynNode_PARENT(top); n; n = KosDynNode_PARENT(n)) {
    if (KosDynNode_IS_YIELD_FROM(n)
        && KosDynNode_HAS_CAP(n, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY))
      return (KosDynYieldFrom *) n;
  }
  return NULL;
}


/**
   Inner Koskari async gen yielded: push through to delegating frame and
   surface KOS_EV_YIELD to the outer koskari_eval caller.
*/
int kos_dyn_yield_from_deliver_inner_yield(
    KosEvalState *es, KosDynYieldFrom *yf, KosEvent *out_ev) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  PyObject *payload = KosEvent_PAYLOAD(out_ev);
  KosDynFrame *frame = KosEvalState_FRAME(es);
  PyObject *const *tos;

  if (! frame || ! payload) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_yield_from_deliver_inner_yield: missing frame "
                    "or payload");
    return -1;
  }

  if (KosEvalState_StackPushRef(es, payload) < 0) {
    return -1;
  }
  Py_DECREF(payload);
  KosEvent_PAYLOAD(out_ev) = NULL;
  KosEvent_KIND(out_ev) = KOS_EV_NONE;

  {
    KosPyAsyncGen *agen =
        (KosPyAsyncGen *) KosDynYieldFrom_DELEGATE(yf);
    agen->in_progress = 0;
  }

  KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_EXPECT_SEND);
  KosEvalState_SET_PC(es, KosEvalState_ACTING_PC(es));

  tos = KosEvalState_StackTailPtr(es, 1);
  if (! tos) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_yield_from_deliver_inner_yield: empty stack");
    return -1;
  }

  KosEvent_KIND(out_ev) = KOS_EV_YIELD;
  KosEvent_PAYLOAD(out_ev) = *tos;
  Py_INCREF(KosEvent_PAYLOAD(out_ev));

  KosEvalState_FlushLiveRegsToFrame(es);
  KosContext_FLAGS(ctx) &= ~KOS_CTX_RUNNING;
  KosContext_FLAGS(ctx) |= KOS_CTX_DEAD;
  return 0;
}


/**
   Inner Koskari async gen returned: pop yield-from (void completion).
   Advance delegating frame PC past the YIELD_FROM instruction.
*/
int kos_dyn_yield_from_deliver_inner_return(
    KosEvalState *es, KosDynYieldFrom *yf, KosEvent *out_ev) {

  PyObject *val = KosEvent_PAYLOAD(out_ev);
  KosDynFrame *df;
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
  if (kos_dyn_gen_unwind_exhausted(es) < 0) {
    return -1;
  }

  df = KosEvalState_FRAME(es);

  delegate_agen = (KosPyAsyncGen *) KosDynYieldFrom_DELEGATE(yf);
  if (delegate_agen)
    delegate_agen->in_progress = 0;

  code = df ? KosDynFrame_CODE(df) : NULL;
  if (df && code && KosEvalState_ACTING_PC(es) + 1u < KosCode_CODE_LEN(code))
    KosEvalState_SET_PC(es, KosEvalState_ACTING_PC(es) + 1u);

  if (kos_dyn_pop_yield_from(es, yf) < 0)
    return -1;
  return 0;
}


/**
   Remove YIELD_FROM from the chain whether or not it is dyn_top.
*/
int kos_dyn_unlink_yield_from(KosEvalState *es, KosDynYieldFrom *yf) {

  KosDynNode *node;
  KosDynNode *p;
  KosDynNode *c;

  if (! es || ! yf) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_unlink_yield_from: invalid arguments");
    return -1;
  }

  node = (KosDynNode *) yf;
  if (KosEvalState_DYN_TOP(es) == node)
    return kos_dyn_pop_yield_from(es, yf);

  p = KosDynNode_PARENT(node);
  KosEvalState_FlushLiveRegsToFrame(es);
  for (c = KosEvalState_DYN_TOP(es); c; c = KosDynNode_PARENT(c)) {
    if (KosDynNode_PARENT(c) == node) {
      KosDynNode_PARENT(c) = p;
      break;
    }
  }
  kos_dyn_rebuild_live_from_chain(es);
  KosDynNode_PARENT(node) = NULL;
  kos_dynyieldfrom_free(yf);
  return 0;
}


void kos_yieldfrom_close_delegate(KosDynYieldFrom *node) {

  if (! node)
    return;

  PyObject *delegate = KosDynYieldFrom_DELEGATE(node);
  if (! delegate)
    return;

  uint8_t kind = KosDynYieldFrom_KIND(node);
  PyObject *inflight = KosDynYieldFrom_INFLIGHT(node);

  switch (kind) {
  case KOS_YF_SYNC_ITER:
    break;

  case KOS_YF_SYNC_GEN: {
    PyObject *res = PyObject_CallMethodObjArgs(
        delegate, kos_dynyieldfrom_str_close, NULL);
    Py_XDECREF(res);
    PyErr_Clear();
    break;
  }

  case KOS_YF_PY_ASYNC_ITER:
  case KOS_YF_PY_ASYNC_GEN:
    Py_XDECREF(inflight);
    KosDynYieldFrom_INFLIGHT(node) = NULL;
    {
      PyObject *res = PyObject_CallMethodObjArgs(
          delegate, kos_dynyieldfrom_str_aclose, NULL);
      Py_XDECREF(res);
      PyErr_Clear();
    }
    break;

  case KOS_YF_KOS_ASYNC_GEN:
    if (KosPyAsyncGen_Check(delegate))
      ((KosPyAsyncGen *) delegate)->closed = 1;
    break;

  default:
    break;
  }
}


/**
   Close delegate on pop (uninstall path). Callers: kos_dyn_uninstall_handler.
*/
void kos_dyn_yieldfrom_unwind(KosEvalState *es, KosDynYieldFrom *node) {
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

