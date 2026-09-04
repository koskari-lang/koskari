/* Included by dynchain.c — dyn_yield_from */
/* === KosDynYieldFrom implementation === */


static PyObject *kos_dynyieldfrom_str_close = NULL;


static int kos_dynyieldfrom_strings_init(void) {

  STR_CONST(kos_dynyieldfrom_str_close, "close");
  if (! kos_dynyieldfrom_str_close)
    return -1;
  return 0;
}


static void kos_dynyieldfrom_strings_clear(void) {

  Py_CLEAR(kos_dynyieldfrom_str_close);
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
  /* KOS_YF_KOS_ITER (4): flattened iteration node with main eval loop */
  if (kind == (uint8_t) 4)
    caps |= KOS_DYN_CAP_ITER_RECEIVER;

  kos_dynnode_init((KosDynNode *) node, KOS_DYN_YIELD_FROM, parent, caps);
  Py_INCREF(delegate);
  KosDynYieldFrom_DELEGATE(node) = delegate;
  KosDynYieldFrom_KIND(node) = kind;
  KosDynYieldFrom_PENDING_ACTION(node) = 0;
  KosDynYieldFrom_PHASE_SET(node, KOS_YF_PHASE_DRIVING);
  KosDynYieldFrom_PENDING_ARG(node) = NULL;
  KosDynYieldFrom_PENDING_ARG_TYPE(node) = NULL;
  KosDynYieldFrom_INFLIGHT(node) = NULL;
  KosDynYieldFrom_REQ(node)->iter = NULL;
  KosDynYieldFrom_REQ(node)->mode = KOS_IREQ_IDLE;
  KosDynYieldFrom_DRIVE(node) = NULL;
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
  Py_XDECREF(KosDynYieldFrom_REQ(node)->iter);
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

  *KosDynYieldFrom_REQ(clone) = *KosDynYieldFrom_REQ(node);
  Py_XINCREF(KosDynYieldFrom_REQ(clone)->iter);
  KosDynYieldFrom_DRIVE(clone) = KosDynYieldFrom_DRIVE(node);

  return clone;
}


/**
   Flattened koskari async gen delegate yielded a value at the iterator
   iteration node.  Hand the value to the delegating generator's frame and stop:
   control returns to the eval loop, which re-dispatches the YIELD_FROM opcode
   in EMIT phase.  The handler then re-emits the value as this generator's own
   yield (yield_from_emit_yield), and the normal yield walk freezes this
   generator's GEN node and delivers onward to the next receiver.  Each hop is
   one walk plus one delivery, so nested for-each / yield-from / generator
   stacks compose to arbitrary depth.
*/
int kos_dyn_yield_from_deliver_inner_yield(
    KosEvalState *es, KosDynYieldFrom *yf,
    PyObject *inner_val) {

  KosDynFrame *const frame = KosEvalState_FRAME(es);

  if (! frame || ! inner_val) {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_yield_from_deliver_inner_yield: missing frame "
                    "or payload");
    return -1;
  }

  if (KosEvalState_StackPushRef(es, inner_val) < 0) {
    return -1;
  }
  Py_DECREF(inner_val);

  {
    PyObject *delegate = KosDynYieldFrom_DELEGATE(yf);
    KosIterSlots *islots;
    if (delegate && KosIter_CheckSlots(delegate, &islots))
      islots->ki_finish(delegate, 0);
  }
  kos_dyn_iter_request_set_mode((KosDynNode *) yf, KOS_IREQ_IDLE);

  KosDynYieldFrom_PHASE_SET(yf, KOS_YF_PHASE_EMIT);
  KosEvalState_SET_PC(es, KosEvalState_ACTING_PC(es));
  return 0;
}


/**
   Inner Koskari async gen returned: pop yield-from (void completion).
   Advance delegating frame PC past the YIELD_FROM instruction.
*/
/**
   Exhaustion tail shared by the inner-return delivery (GEN just
   unwound) and the raise-walk StopIteration conversion (GEN already
   finished): clear the delegate's in-progress mark, resume after the
   YIELD_FROM, and pop the node. val consumed.
*/
int kos_dyn_yield_from_deliver_exhausted(
    KosEvalState *es, KosDynYieldFrom *yf, PyObject *val) {

  KosDynFrame *df;
  KosCode *code;

  Py_XDECREF(val);

  df = KosEvalState_FRAME(es);

  {
    PyObject *delegate = KosDynYieldFrom_DELEGATE(yf);
    KosIterSlots *islots;
    if (delegate && KosIter_CheckSlots(delegate, &islots))
      islots->ki_finish(delegate, 1);
  }

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
  if (KosEvalState_DYN_TOP(es) == node) {
    /* Forced teardown: the delegate may still be active, so close it
       before popping (kos_dyn_pop_yield_from no longer closes). */
    kos_yieldfrom_close_delegate(yf);
    return kos_dyn_pop_yield_from(es, yf);
  }

  p = KosDynNode_PARENT(node);
  for (c = KosEvalState_DYN_TOP(es); c; c = KosDynNode_PARENT(c)) {
    if (KosDynNode_PARENT(c) == node) {
      KosDynNode_PARENT(c) = p;
      break;
    }
  }
  kos_dyn_resync_chain(es);
  KosDynNode_PARENT(node) = NULL;
  kos_yieldfrom_close_delegate(yf);
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
    /* Structural teardown of the dyn chain: we are not inside a drivable
       eval step here and cannot await. Do NOT call aclose() -- its
       coroutine would never be awaited, which runs no cleanup at all and
       only raises a RuntimeWarning. Release any in-flight awaitable and
       drop our reference; the delegate finalizes itself (for a Python
       async generator, the interpreter's asyncgen finalizer hook runs
       aclose on the event loop when it is collected). An explicit close
       of the delegating generator reaches the inner via the
       GeneratorExit throw -> athrow path in yield_from_drive_py_async,
       which does await and can suspend. */
    Py_XDECREF(inflight);
    KosDynYieldFrom_INFLIGHT(node) = NULL;
    break;

  case KOS_YF_KOS_ITER: {
    KosIterSlots *islots;
    if (KosIter_CheckSlots(delegate, &islots))
      islots->ki_finish(delegate, 1);
    break;
  }

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
  Py_VISIT(KosDynYieldFrom_REQ(yf)->iter);
  return 0;
}

