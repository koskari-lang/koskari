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
  Included by dynchain.c. The KOS_DYN_NEXT node: the iteration node of
  a standalone NEXT over a Koskari generator.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KOS_DYN_NEXT: standalone NEXT opcode iteration node for Koskari async gen === */


void kos_dyn_next_free_payload(KosDynNode *node) {

  Py_CLEAR(KosDynNext_REQ(node)->iter);
  Py_CLEAR(KosDynNext_DEFAULT_VAL(node));
  kos_dynnode_return_to_freelist(node);
}


int kos_dyn_push_next_node(
    KosContext *ctx, PyObject *iter_obj, PyObject *default_val, int n) {

  // CONTEXT: dirty, touches context directly
  KosDynNode *node = kos_dynnode_alloc();
  if (! node) {
    return -1;
  }

  kos_dynnode_init(
      node, KOS_DYN_NEXT, KosEvalState_DYN_TOP(ctx),
      KOS_DYN_CAP_ITER_RECEIVER);
  Py_INCREF(iter_obj);
  kos_dyn_iter_request_bind_iter(KosDynNext_REQ(node), iter_obj);
  KosDynNext_REQ(node)->mode = KOS_IREQ_IDLE;
  if (default_val) {
    Py_INCREF(default_val);
    KosDynNext_DEFAULT_VAL(node) = default_val;
  } else {
    KosDynNext_DEFAULT_VAL(node) = NULL;
  }
  KosDynNext_N(node) = n;

  if (kos_dyn_push(ctx, node) < 0) {
    kos_dyn_next_free_payload(node);
    return -1;
  }
  return 0;
}


void kos_dyn_next_abort_after_failed_attach(KosContext *ctx) {

  KosDynNode *top = KosEvalState_DYN_TOP(ctx);
  if (top && KosDynNode_IS_NEXT(top)) {
    KosDynNode *parent = KosDynNode_PARENT(top);

    kos_dyn_commit_dyn_top(ctx, parent);
    KosDynNode_PARENT(top) = NULL;
    kos_dyn_next_free_payload(top);
  } else {
    PyErr_SetString(PyExc_RuntimeError,
                    "kos_dyn_next_abort_after_failed_attach: top is not NEXT");
  }
}


void kos_dyn_unlink_next_node(KosContext *ctx, KosDynNext *next) {

  KosDynNode *node = (KosDynNode *) next;
  KosDynNode *p = KosDynNode_PARENT(node);

  if (KosEvalState_DYN_TOP(ctx) == node) {
    kos_dyn_commit_dyn_top(ctx, p);
  } else {
    KosDynNode *c;
    for (c = KosEvalState_DYN_TOP(ctx); c; c = KosDynNode_PARENT(c)) {
      if (KosDynNode_PARENT(c) == node) {
        KosDynNode_PARENT(c) = p;
        break;
      }
    }
    kos_dyn_resync_chain(ctx);
  }

  KosDynNode_PARENT(node) = NULL;
  kos_dyn_next_free_payload(node);
}


int kos_dyn_next_deliver_yield(
    KosContext *ctx, KosDynNext *next, PyObject *val) {

  if (KosEvalState_StackPUSH(ctx, val) < 0)
    return -1;  /* the push consumed val, failure included */

  kos_dyn_iter_request_finish_req(KosDynNext_REQ(next), 0);

  kos_dyn_unlink_next_node(ctx, next);
  return 0;
}


/**
   Exhaustion tail shared by the return delivery (GEN just unwound) and
   the raise-walk StopIteration conversion (GEN already finished): mark
   the iterator closed, then honor the NEXT arity -- 2-arg pushes the
   default, 1-arg re-raises StopIteration to the caller carrying the
   generator's return value. val consumed.
*/
int kos_dyn_next_deliver_exhausted(
    KosContext *ctx,
    KosDynNext *next,
    PyObject *val) {

  int nn = KosDynNext_N((KosDynNode *) next);

  kos_dyn_iter_request_finish_req(KosDynNext_REQ(next), 1);

  if (nn == 2 && KosDynNext_DEFAULT_VAL((KosDynNode *) next)) {
    if (KosEvalState_StackPushRef(
            ctx,
            KosDynNext_DEFAULT_VAL((KosDynNode *) next)) < 0) {
      Py_XDECREF(val);
      return -1;
    }
    Py_XDECREF(val);
  } else if (nn == 1) {
    /* the StopIteration carries the generator's return value, as
       Python's next does; a bare return raises it empty */
    if (val && val != Py_None) {
      PyObject *err = PyObject_CallOneArg(PyExc_StopIteration, val);
      Py_DECREF(val);
      if (err) {
        PyErr_SetObject(PyExc_StopIteration, err);
        Py_DECREF(err);
      }
    } else {
      Py_XDECREF(val);
      PyErr_SetNone(PyExc_StopIteration);
    }
    kos_dyn_unlink_next_node(ctx, next);
    return -1;
  } else {
    Py_XDECREF(val);
  }

  kos_dyn_unlink_next_node(ctx, next);
  return 0;
}


KosDynNext *kos_dyn_next_clone(KosDynNext *node) {

  KosDynNext *clone = (KosDynNext *) kos_dynnode_clone_base(
      (KosDynNode *) node);
  if (! clone)
    return NULL;
  Py_XINCREF(KosDynNext_REQ(node)->iter);
  *KosDynNext_REQ(clone) = *KosDynNext_REQ(node);
  Py_XINCREF(KosDynNext_DEFAULT_VAL((KosDynNode *) node));
  KosDynNext_DEFAULT_VAL((KosDynNode *) clone) = KosDynNext_DEFAULT_VAL((KosDynNode *) node);
  KosDynNext_N((KosDynNode *) clone) = KosDynNext_N((KosDynNode *) node);
  return clone;
}


static int dynnode_to_dict_next(KosDynNode *node, PyObject *data) {

  if (snapshot_set_obj(data, "iter", KosDynNext_REQ(node)->iter) < 0)
    return -1;
  if (snapshot_set_uint(data, "req_mode", KosDynNext_REQ(node)->mode) < 0)
    return -1;
  if (snapshot_set_obj(data, "default_val", KosDynNext_DEFAULT_VAL(node)) < 0)
    return -1;
  if (snapshot_set_int(data, "n", KosDynNext_N(node)) < 0)
    return -1;
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_NEXT: visit async gen object and optional default.
*/
static int dynnode_traverse_next(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynNext_REQ(node)->iter);
  Py_VISIT(KosDynNext_DEFAULT_VAL(node));
  return 0;
}


// The end.
