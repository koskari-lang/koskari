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

  kos_dynnode_init(node, KOS_DYN_FOREACH, parent, KOS_DYN_CAP_LOOP_REGION);
  KosDynForeach_BREAK_PC(node) = break_pc;
  KosDynForeach_CONTINUE_PC(node) = continue_pc;
  KosDynForeach_RESULT(node) = NULL;
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
  KosDynForeach_RESULT(clone) = KosDynForeach_RESULT(node);
  Py_XINCREF(KosDynForeach_RESULT(clone));
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
  if (snapshot_set_uint(data, "req_mode",
      KosDynForeach_REQ(node)->mode) < 0)
    return -1;
  return 0;
}


/**
   Push FOREACH as DYN_TOP. Callers: opcode PUSH_FOREACH.
*/
KosDynForeach *kos_dyn_push_foreach(KosContext *ctx, uint32_t break_pc, uint32_t continue_pc) {
  KosDynForeach *node = kos_dynforeach_new(KosContext_DYN_TOP(ctx), break_pc, continue_pc);
  if (! node)
    return NULL;
  if (kos_dyn_push(ctx, (KosDynNode *) node) < 0) {
    kos_dynforeach_free(node);
    return NULL;
  }
  return node;
}


/* A step resume on the live frame when its region leaves is a step
   whose delivery or failure never reached it: a resume is never
   skipped, so under the safety net this is an internal fault. */
static int foreach_step_resume_left_behind(KosContext *ctx) {
#if KOS_EVAL_SAFETYNET
  KosDynFrame *frame = KosContext_FRAME(ctx);

  if (frame
      && KosDynFrame_RESUME_KIND(frame)
      && KosDynFrame_RESUME_KIND(frame)->direct) {
    PyErr_SetString(PyExc_SystemError,
                    "for-each region left with its step resume on the"
                    " frame");
    return 1;
  }
#else
  (void) ctx;
#endif
  return 0;
}


/**
   Pop FOREACH when top. Callers: opcode POP_FOREACH, kos_dyn_unwind_to_loop.
*/
int kos_dyn_pop_foreach(KosContext *ctx, KosDynForeach *node) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "FOREACH node is not at top of chain");
    return -1;
  }
  if (foreach_step_resume_left_behind(ctx))
    return -1;
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_commit_dyn_top(ctx, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynforeach_free(node);
  }
  return 0;
}


void kos_dyn_foreach_unwind(KosContext *ctx, KosDynForeach *node) {
  (void) node;
  (void) foreach_step_resume_left_behind(ctx);
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
