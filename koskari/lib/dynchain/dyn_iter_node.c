/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
  Included by dynchain.c. The FOREACH node's step request: binding,
  finishing and the receiver lookup.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === Generic iteration-node dispatch === */

/*
  Routing keys on KOS_DYN_CAP_ITER_RECEIVER only; the per-kind
  delivery semantics stay in dyn_foreach.c.
*/


KosDynNode *kos_dyn_find_iter_node(KosContext *ctx) {

  /* Prefer dyn_top.  After the yield freeze the yielding generator's GEN
     node has detached, so its parent, the receiver that was directly
     driving it, sits at dyn_top and is the structurally nearest
     receiver.  It must win over an outer FOREACH awaiting an enclosing
     generator, otherwise the yields are misrouted. */
  KosDynNode *const top = KosContext_DYN_TOP(ctx);
  if (top && KosDynNode_HAS_CAP(top, KOS_DYN_CAP_ITER_RECEIVER))
    return top;

  /* Otherwise (the gen frame is still installed) walk outward from the
     running frame and take the nearest receiver of any kind. */
  {
    KosDynFrame *cf = KosContext_FRAME(ctx);
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

  if (req->islots) {
    req->kind = KOS_IREQ_KIND_PROVIDER;
  } else if (! iter) {
    req->kind = KOS_IREQ_KIND_NONE;
  } else {
    PyTypeObject *tp = Py_TYPE(iter);
    if (tp->tp_as_async && tp->tp_as_async->am_anext)
      req->kind = KOS_IREQ_KIND_PY_ASYNC;
    else if (PyIter_Check(iter))
      req->kind = KOS_IREQ_KIND_PY_SYNC;
    else
      req->kind = KOS_IREQ_KIND_NONE;
  }
}


void kos_dyn_iter_request_finish_req(KosIterRequest *req, int exhausted) {
  if (req->islots)
    req->islots->ki_finish(req->iter,
                           exhausted && ! req->islots->ki_pyctx_recyclable);
}


void kos_dyn_iter_request_finish(KosDynNode *node, int exhausted) {
  kos_dyn_iter_request_finish_req(kos_dyn_iter_request(node), exhausted);
}


PyObject *kos_dyn_iter_node_exhaustion_exc(KosDynNode *node) {

  KosIterRequest *req = kos_dyn_iter_request(node);

  if (! req)
    return PyExc_StopIteration;

  /* A suspended python async step ends its iteration with
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


void kos_dyn_iter_node_abort(KosContext *ctx, KosDynNode *node) {

  (void) ctx;
  if (KosDynNode_IS_FOREACH(node))
    kos_dyn_iter_request_set_mode(node, KOS_IREQ_IDLE);
}


// The end.
