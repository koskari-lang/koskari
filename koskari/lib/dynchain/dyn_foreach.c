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
  Included by dynchain.c. The KOS_DYN_FOREACH region node.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynForeach === */


/**
   FOREACH region: loop PCs, iterator, exhausted flag, for/else PC.
*/
KosDynForeach *kos_dynforeach_new(KosDynNode *parent, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  kos_dynnode_init(node, KOS_DYN_FOREACH, parent, 0);
  KosDynForeach_BREAK_PC(node) = break_pc;
  KosDynForeach_CONTINUE_PC(node) = continue_pc;
  KosDynForeach_ENABLED(node) = 1;
  KosDynForeach_RESULT(node) = NULL;
  KosDynForeach_EXHAUSTED(node) = 0;
  KosDynForeach_ITER_NEXT_ELSE_PC(node) = 0;
  KosDynForeach_REQ(node)->iter = NULL;
  KosDynForeach_REQ(node)->islots = NULL;
  KosDynForeach_REQ(node)->mode = KOS_IREQ_IDLE;
  KosDynForeach_REQ(node)->kind = KOS_IREQ_KIND_NONE;
  kos_dynnode_use_inline_segment((KosDynNode *) node);
  return node;
}


/**
   Clone iterator and result refs for continuation capture.
*/
KosDynForeach *kos_dynforeach_clone(KosDynForeach *node) {
  KosDynForeach *clone = kos_dynnode_clone_base(node);
  if (! clone)
    return NULL;
  KosDynForeach_BREAK_PC(clone) = KosDynForeach_BREAK_PC(node);
  KosDynForeach_CONTINUE_PC(clone) = KosDynForeach_CONTINUE_PC(node);
  KosDynForeach_ENABLED(clone) = KosDynForeach_ENABLED(node);
  KosDynForeach_RESULT(clone) = KosDynForeach_RESULT(node);
  Py_XINCREF(KosDynForeach_RESULT(clone));
  KosDynForeach_EXHAUSTED(clone) = KosDynForeach_EXHAUSTED(node);
  KosDynForeach_ITER_NEXT_ELSE_PC(clone) = KosDynForeach_ITER_NEXT_ELSE_PC(node);
  *KosDynForeach_REQ(clone) = *KosDynForeach_REQ(node);
  Py_XINCREF(KosDynForeach_REQ(clone)->iter);
  return clone;
}


/**
   DECREF result and iterator; return node to freelist.
*/
void kos_dynforeach_free(KosDynForeach *node) {
  Py_XDECREF(KosDynForeach_RESULT(node));
  Py_XDECREF(KosDynForeach_REQ(node)->iter);
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int dynnode_to_dict_foreach(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "break_pc",
                        KosDynForeach_BREAK_PC((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "continue_pc",
                        KosDynForeach_CONTINUE_PC((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "enabled",
                       KosDynForeach_ENABLED((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "result",
      KosDynForeach_RESULT((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "iter",
      KosDynForeach_ITER((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "exhausted",
      KosDynForeach_EXHAUSTED((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "iter_next_else_pc",
      KosDynForeach_ITER_NEXT_ELSE_PC((KosDynForeach *) node)) < 0)
    return -1;
  if (snapshot_set_uint(data, "req_mode",
      KosDynForeach_REQ(node)->mode) < 0)
    return -1;
  return 0;
}


/**
   Push FOREACH as DYN_TOP. Callers: opcode PUSH_FOREACH.
*/
KosDynForeach *kos_dyn_push_foreach(KosEvalState *es, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynforeach_new(KosEvalState_DYN_TOP(es), break_pc, continue_pc);
  if (! node)
    return NULL;
  if (kos_dyn_push(es, (KosDynNode *) node) < 0) {
    kos_dynforeach_free(node);
    return NULL;
  }
  return node;
}


/**
   Pop FOREACH when top. Callers: opcode POP_FOREACH, kos_dyn_unwind_to_loop.
*/
int kos_dyn_pop_foreach(KosEvalState *es, KosDynForeach *node) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "FOREACH node is not at top of chain");
    return -1;
  }
  kos_dyn_foreach_clear_store_resume(es);
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynforeach_free(node);
  }
  return 0;
}


void kos_dyn_foreach_clear_store_resume(KosEvalState *es) {
  KosDynFrame *frame = KosEvalState_FRAME(es);

  if (frame
      && KosDynFrame_RESUME_KIND(frame) == &kos_foreach_store_resume_kind) {
    KosDynFrame_RESUME_KIND(frame) = NULL;
    KosDynFrame_RESUME_STATE(frame) = NULL;
  }
}


void kos_dyn_foreach_unwind(KosEvalState *es, KosDynForeach *node) {
  (void) node;
  kos_dyn_foreach_clear_store_resume(es);
}


/**
   A deferred element lands as the resumed frame expects, the frame
   being live again by the time it arrives: staged for the resume a
   STORE step set, else pushed. Consumes val, failure included.
*/
static int foreach_deliver_deferred(KosEvalState *es, PyObject *val) {
  KosDynFrame *frame = KosEvalState_FRAME(es);

  if (frame && KosDynFrame_RESUME_KIND(frame)) {
    KosEvalState_StageDelivery(es, val);
    return 0;
  }
  return KosEvalState_StackPUSH(es, val);
}


int kos_dyn_foreach_deliver_yield_kos_agen(
    KosEvalState *es,
    KosDynForeach *fe,
    PyObject *val) {

  if (foreach_deliver_deferred(es, val) < 0)
    return -1;

  kos_dyn_iter_request_finish_req(KosDynForeach_REQ(fe), 0);
  kos_dyn_iter_request_set_mode((KosDynNode *) fe, KOS_IREQ_IDLE);
  return 0;
}


/**
   Exhaustion tail shared by the return delivery (GEN just unwound) and
   the raise-walk StopIteration conversion (GEN already finished): mark
   the iterator closed, flag the loop exhausted, and resume at the
   else pc. val consumed.
*/
int kos_dyn_foreach_deliver_exhausted(
    KosEvalState *es,
    KosDynForeach *fe,
    PyObject *val) {

  kos_dyn_iter_request_finish_req(KosDynForeach_REQ(fe), 1);

  /* no element is coming: a store resume the step set goes with it */
  kos_dyn_foreach_clear_store_resume(es);
  KosDynForeach_EXHAUSTED(fe) = 1;
  KosEvalState_SET_PC(es, KosDynForeach_ITER_NEXT_ELSE_PC(fe));
  kos_dyn_iter_request_set_mode((KosDynNode *) fe, KOS_IREQ_IDLE);

  Py_XDECREF(val);
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FOREACH: visit result and iterator refs.
*/
static int dynnode_traverse_foreach(KosDynNode *node, visitproc visit, void *arg) {
  Py_VISIT(KosDynForeach_RESULT((KosDynForeach *) node));
  Py_VISIT(KosDynForeach_REQ(node)->iter);
  return 0;
}


// The end.
