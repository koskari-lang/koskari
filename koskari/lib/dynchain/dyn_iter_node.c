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


/* The for-each bind: as kos_iter_request_bind_next, with async tried
   before sync, as the for-each over a python object answering both
   protocols has always taken its async one. */
void kos_dyn_iter_request_bind_iter(KosIterRequest *req, PyObject *iter) {

  req->iter = iter;
  req->send = NULL;
  if (KosIter_CheckSlots(iter, &req->islots)) {
    req->kind = KOS_IREQ_KIND_PROVIDER;
    return;
  }
  req->islots = NULL;
  if (! iter)
    req->kind = KOS_IREQ_KIND_NONE;
  else if (Py_TYPE(iter)->tp_as_async && Py_TYPE(iter)->tp_as_async->am_anext)
    req->kind = KOS_IREQ_KIND_PY_ASYNC;
  else if (PyIter_Check(iter))
    req->kind = KOS_IREQ_KIND_PY_SYNC;
  else
    req->kind = KOS_IREQ_KIND_NONE;
}


void kos_dyn_iter_request_finish_req(KosIterRequest *req, int exhausted) {
  if (req->islots)
    kos_iter_finish(req->iter, req->islots, exhausted);
}


// The end.
