/* Included by dynchain.c — dyn_boundary */
/* === Generic iterator-boundary dispatch === */

/*
  One capability-driven dispatch surface for the consumers of a flattened
  Koskari generator's boundary events: NEXT, an awaiting FOREACH, and a
  flattened-delegate YIELD_FROM.  Routing keys on
  KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY only; the per-kind delivery
  semantics stay in dyn_next.c / dyn_foreach.c / dyn_yield_from.c.

  Future receiver kinds join by carrying the capability and adding a
  delivery case here — in particular a GEN node carrying a yield-from
  passthru field would slot in without touching the finder or the eval
  loop (see design/archive/boundary-control-reemit.md §5.2).
*/


KosDynNode *kos_dyn_find_iter_boundary(KosEvalState *es) {

  /* Prefer dyn_top.  After the yield freeze the yielding generator's GEN
     node has detached, so its parent — the boundary that was directly
     driving it — sits at dyn_top and is the structurally nearest
     receiver.  It must win: a YIELD_FROM delegating to this generator
     beats an outer FOREACH awaiting an *enclosing* generator, otherwise
     the delegate's yields are misrouted and the delegate is torn down. */
  KosDynNode *const top = KosEvalState_DYN_TOP(es);
  if (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY))
    return top;

  /* Otherwise (the gen frame is still installed) walk outward from the
     running frame and take the nearest receiver of any kind. */
  {
    KosDynFrame *cf = KosEvalState_FRAME(es);
    KosDynNode *n;
    if (! cf)
      return NULL;
    for (n = KosDynNode_PARENT((KosDynNode *) cf); n; n = KosDynNode_PARENT(n)) {
      if (KosDynNode_HAS_CAP(n, KOS_DYN_CAP_RECEIVES_ITER_BOUNDARY))
        return n;
    }
  }
  return NULL;
}


int kos_dyn_iter_boundary_deliver_yield(
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
               "iterator boundary yield to unexpected receiver kind %u",
               (unsigned) KosDynNode_KIND(node));
  Py_DECREF(val);
  return -1;
}


int kos_dyn_iter_boundary_deliver_return(
    KosEvalState *es, KosDynNode *node, PyObject *val) {

  if (KosDynNode_IS_NEXT(node)) {
    if (kos_dyn_next_deliver_return_node(es, (KosDynNext *) node, val) < 0)
      /* StopIteration (1-arg NEXT) and internal failures both surface as
         a set PyErr; a bare -1 is a completed delivery. */
      return PyErr_Occurred()
          ? KOS_ITER_BOUNDARY_ERROR : KOS_ITER_BOUNDARY_DELIVERED;
    return KOS_ITER_BOUNDARY_DELIVERED;
  }

  if (KosDynNode_IS_FOREACH(node)) {
    if (kos_dyn_foreach_deliver_return_kos_agen(
            es, (KosDynForeach *) node, val) < 0)
      return KOS_ITER_BOUNDARY_ERROR;
    return KOS_ITER_BOUNDARY_DELIVERED;
  }

  if (KosDynNode_IS_YIELD_FROM(node)) {
    if (kos_dyn_yield_from_deliver_inner_return(
            es, (KosDynYieldFrom *) node, val) < 0)
      return KOS_ITER_BOUNDARY_RAISE;
    return KOS_ITER_BOUNDARY_DELIVERED;
  }

  PyErr_Format(PyExc_RuntimeError,
               "iterator boundary return to unexpected receiver kind %u",
               (unsigned) KosDynNode_KIND(node));
  Py_DECREF(val);
  return KOS_ITER_BOUNDARY_ERROR;
}


/**
   Exhaustion delivery for a receiving iterator boundary whose flattened
   generator has already been finished (raise-walk StopIteration
   conversion): dispatch to the node's exhaustion tail without the GEN
   unwind the return deliveries perform. val consumed.

   :return: 0 delivered; -1 with PyErr set when the boundary re-emits an
       exception (e.g. 1-arg NEXT re-raising StopIteration); -1 without
       PyErr on internal failure
*/
int kos_dyn_iter_boundary_deliver_exhausted(
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
               "iterator boundary exhaustion to unexpected receiver kind %u",
               (unsigned) KosDynNode_KIND(node));
  Py_XDECREF(val);
  return -1;
}


/**
   The exception type that ends this boundary's in-flight step: a
   flattened Koskari agen step finishes as StopIteration (the flattened
   generator's own semantics), a suspended python async iterator step
   finishes as StopAsyncIteration (the __anext__ protocol).
*/
PyObject *kos_dyn_iter_boundary_exhaustion_exc(KosDynNode *node) {

  if (KosDynNode_IS_NEXT(node)) {
    if (KosDynNext_RESUME(node))
      return PyExc_StopAsyncIteration;
    return KosIter_Check(KosDynNext_AGEN_OBJ(node))
        ? PyExc_StopIteration : PyExc_StopAsyncIteration;
  }
  if (KosDynNode_IS_FOREACH(node)) {
    uint8_t phase = KosDynForeach_ITER_STEP_PHASE((KosDynForeach *) node);
    return (phase == KOS_FOREACH_ITER_STEP_AWAITING_PY
            || phase == KOS_FOREACH_ITER_STEP_AWAITING_KOS_ITER)
        ? PyExc_StopAsyncIteration : PyExc_StopIteration;
  }
  return PyExc_StopIteration;
}


/**
   True when the boundary's in-flight step is a suspended python async
   iterator being driven through the pending context await -- the flavor
   whose step value arrives at revive-from-await rather than as a
   boundary yield event.
*/
int kos_dyn_iter_boundary_awaiting_py(KosDynNode *node) {

  if (KosDynNode_IS_NEXT(node))
    return KosDynNext_RESUME(node)
        || ! KosIter_Check(KosDynNext_AGEN_OBJ(node));
  if (KosDynNode_IS_FOREACH(node)) {
    uint8_t phase = KosDynForeach_ITER_STEP_PHASE((KosDynForeach *) node);
    return phase == KOS_FOREACH_ITER_STEP_AWAITING_PY
        || phase == KOS_FOREACH_ITER_STEP_AWAITING_KOS_ITER;
  }
  return 0;
}


int kos_dyn_iter_boundary_resume_park(KosDynNode *node) {

  if (KosDynNode_IS_NEXT(node))
    return KosDynNext_RESUME(node);
  if (KosDynNode_IS_FOREACH(node))
    return KosDynForeach_ITER_STEP_PHASE((KosDynForeach *) node)
        == KOS_FOREACH_ITER_STEP_AWAITING_KOS_ITER;
  return 0;
}


PyObject *kos_dyn_iter_boundary_iter_obj(KosDynNode *node) {

  if (KosDynNode_IS_NEXT(node))
    return KosDynNext_AGEN_OBJ(node);
  if (KosDynNode_IS_FOREACH(node))
    return KosDynForeach_ITER((KosDynForeach *) node);
  return NULL;
}


PyObject *kos_dyn_iter_boundary_agen(KosDynNode *node) {

  if (KosDynNode_IS_NEXT(node))
    return KosDynNext_AGEN_OBJ(node);
  if (KosDynNode_IS_FOREACH(node))
    return KosDynForeach_ITER(node);
  if (KosDynNode_IS_YIELD_FROM(node))
    return KosDynYieldFrom_DELEGATE(node);
  return NULL;
}


void kos_dyn_iter_boundary_abort(KosEvalState *es, KosDynNode *node) {

  if (KosDynNode_IS_NEXT(node)) {
    kos_dyn_unlink_next_node(es, (KosDynNext *) node);
  } else if (KosDynNode_IS_FOREACH(node)) {
    kos_dyn_foreach_set_iter_step_phase(
        (KosDynForeach *) node, KOS_FOREACH_ITER_STEP_IDLE);
  } else if (KosDynNode_IS_YIELD_FROM(node)) {
    (void) kos_dyn_unlink_yield_from(es, (KosDynYieldFrom *) node);
  }
}
