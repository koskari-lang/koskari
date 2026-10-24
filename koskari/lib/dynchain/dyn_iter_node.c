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
  finishing and the exception that ends its step.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === The FOREACH region's request === */


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
    kos_iter_finish(req->iter, req->islots, exhausted);
}


PyObject *kos_dyn_iter_node_exhaustion_exc(KosDynNode *node) {

  if (KosDynForeach_REQ(node)->kind == KOS_IREQ_KIND_PY_ASYNC)
    return PyExc_StopAsyncIteration;
  return PyExc_StopIteration;
}


// The end.
