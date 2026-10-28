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
  req->iter = iter;
  req->kind = kos_iter_kind(iter, &req->islots);
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
