/* Included by dynchain.c — dyn_iter_node */
/* === Generic iteration-node dispatch === */

/*
  One capability-driven dispatch surface for the consumers of a flattened
  Koskari generator's iteration events: NEXT, an awaiting FOREACH, and a
  flattened-delegate YIELD_FROM.  Routing keys on
  KOS_DYN_CAP_ITER_RECEIVER only; the per-kind delivery
  semantics stay in dyn_next.c / dyn_foreach.c / dyn_yield_from.c.

  Future receiver kinds join by carrying the capability and adding a
  delivery case here — in particular a GEN node carrying a yield-from
  passthru field would slot in without touching the finder or the eval
  loop (see design/archive/boundary-control-reemit.md §5.2).
*/


KosDynNode *kos_dyn_find_iter_node(KosEvalState *es) {

  /* Prefer dyn_top.  After the yield freeze the yielding generator's GEN
     node has detached, so its parent — the receiver that was directly
     driving it — sits at dyn_top and is the structurally nearest
     receiver.  It must win: a YIELD_FROM delegating to this generator
     beats an outer FOREACH awaiting an *enclosing* generator, otherwise
     the delegate's yields are misrouted and the delegate is torn down. */
  KosDynNode *const top = KosEvalState_DYN_TOP(es);
  if (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_ITER_RECEIVER))
    return top;

  /* Otherwise (the gen frame is still installed) walk outward from the
     running frame and take the nearest receiver of any kind. */
  {
    KosDynFrame *cf = KosEvalState_FRAME(es);
    KosDynNode *n;
    if (! cf)
      return NULL;
    for (n = KosDynNode_PARENT((KosDynNode *) cf); n; n = KosDynNode_PARENT(n)) {
      if (KosDynNode_HAS_CAP(n, KOS_DYN_CAP_ITER_RECEIVER))
        return n;
    }
  }
  return NULL;
}


void kos_dyn_iter_request_bind_iter(KosIterRequest *req, PyObject *iter) {
  KosIterSlots *islots = NULL;

  req->iter = iter;
  req->islots = (iter && KosIter_CheckSlots(iter, &islots)) ? islots : NULL;
}


void kos_dyn_iter_request_finish(KosDynNode *node, int exhausted) {
  KosIterRequest *req = kos_dyn_iter_request(node);

  if (req->islots)
    req->islots->ki_finish(req->iter,
                           exhausted && ! req->islots->ki_pyctx_recyclable);
}


int kos_dyn_iter_node_deliver_yield(
    KosEvalState *es, KosDynNode *node, PyObject *val) {

  if (KosDynNode_IS_NEXT(node))
    return kos_dyn_next_deliver_yield(es, (KosDynNext *) node, val);

  if (KosDynNode_IS_FOREACH(node))
    return kos_dyn_foreach_deliver_yield_kos_agen(
        es, (KosDynForeach *) node, val);

  if (KosDynNode_IS_YIELD_FROM(node))
    /* Hand the value to the delegating generator; it re-emits as its own
       yield on the next handler dispatch. */
    return kos_dyn_yield_from_deliver_inner_yield(
        es, (KosDynYieldFrom *) node, val);

  PyErr_Format(PyExc_RuntimeError,
               "iteration-node yield to unexpected receiver kind %u",
               (unsigned) KosDynNode_KIND(node));
  Py_DECREF(val);
  return -1;
}


int kos_dyn_iter_node_deliver_exhausted(
    KosEvalState *es, KosDynNode *node, PyObject *val) {

  if (KosDynNode_IS_NEXT(node))
    return kos_dyn_next_deliver_exhausted(es, (KosDynNext *) node, val);

  if (KosDynNode_IS_FOREACH(node))
    return kos_dyn_foreach_deliver_exhausted(
        es, (KosDynForeach *) node, val);

  if (KosDynNode_IS_YIELD_FROM(node))
    return kos_dyn_yield_from_deliver_exhausted(
        es, (KosDynYieldFrom *) node, val);

  PyErr_Format(PyExc_RuntimeError,
               "iteration-node exhaustion to unexpected receiver kind %u",
               (unsigned) KosDynNode_KIND(node));
  Py_XDECREF(val);
  return -1;
}


PyObject *kos_dyn_iter_node_exhaustion_exc(KosDynNode *node) {

  KosIterRequest *req = kos_dyn_iter_request(node);

  if (! req)
    return PyExc_StopIteration;

  /* A parked python async step ends its iteration with
     StopAsyncIteration; installed provider frames and value grafts
     end with StopIteration. */
  if (req->mode == KOS_IREQ_AWAITING_PY)
    return PyExc_StopAsyncIteration;
  return PyExc_StopIteration;
}


/**
   True when the node's in-flight step is a suspended python async
   iterator being driven through the pending context await.
*/
int kos_dyn_iter_node_awaiting_py(KosDynNode *node) {

  KosIterRequest *req = kos_dyn_iter_request(node);
  return req && req->mode == KOS_IREQ_AWAITING_PY;
}


int kos_dyn_iter_node_value_mode(KosDynNode *node) {

  KosIterRequest *req = kos_dyn_iter_request(node);
  return req && req->mode == KOS_IREQ_GRAFT_VALUE;
}


PyObject *kos_dyn_iter_node_iter_obj(KosDynNode *node) {

  KosIterRequest *req = kos_dyn_iter_request(node);
  return req ? req->iter : NULL;
}


void kos_dyn_iter_node_abort(KosEvalState *es, KosDynNode *node) {

  if (KosDynNode_IS_NEXT(node)) {
    kos_dyn_unlink_next_node(es, (KosDynNext *) node);
  } else if (KosDynNode_IS_FOREACH(node)) {
    kos_dyn_iter_request_set_mode(node, KOS_IREQ_IDLE);
  } else if (KosDynNode_IS_YIELD_FROM(node)) {
    (void) kos_dyn_unlink_yield_from(es, (KosDynYieldFrom *) node);
  }
}
