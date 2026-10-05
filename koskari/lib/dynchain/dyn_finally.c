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
  Included by dynchain.c. The KOS_DYN_FINALLY region node.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynFinally (finally region) === */


/**
   Allocate FINALLY with handler_pc; handles RAISE and RETURN when enabled.
*/
KosDynFinally *kos_dynfinally_new(KosDynNode *parent, uint32_t handler_pc) {
  KosDynFinally *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  kos_dynnode_init(node, KOS_DYN_FINALLY, parent,
                   KOS_DYN_CAP_HANDLES_EXCEPTIONS
                   | KOS_DYN_CAP_INTERCEPTS_RAISE);
  KosDynFinally_HANDLER_PC(node) = handler_pc;
  KosDynFinally_ENABLED(node) = 1;
  KosDynFinally_HANDLING_RAISE(node) = 0;
  KosDynFinally_HANDLING_CONTROL(node) = 0;
  KosDynFinally_EXC_SAVED(node) = 0;
  KosExc_Init(KosDynFinally_SAVED(node));
  KosExc_Init(KosDynFinally_HANDLING(node));
  kos_dynnode_use_inline_segment((KosDynNode *) node);
  return node;
}


/**
   Clone finally state for continuation templates.
*/
KosDynFinally *kos_dynfinally_clone(KosDynFinally *node) {
  KosDynFinally *clone = kos_dynnode_alloc();
  if (! clone)
    return NULL;
  kos_dynnode_init(clone, KOS_DYN_FINALLY, NULL, KosDynNode_CAPS(node));
  KosDynNode_FLAGS(clone) = KosDynNode_FLAGS(node) | KOS_DYNF_CLONED;
  KosDynNode_NODE_ID(clone) = kos_dynnode_id(node);
  KosDynFinally_HANDLER_PC(clone) = KosDynFinally_HANDLER_PC(node);
  KosDynFinally_ENABLED(clone) = KosDynFinally_ENABLED(node);
  KosDynFinally_HANDLING_RAISE(clone) = KosDynFinally_HANDLING_RAISE(node);
  KosDynFinally_HANDLING_CONTROL(clone) = KosDynFinally_HANDLING_CONTROL(node);
  KosDynFinally_EXC_SAVED(clone) = KosDynFinally_EXC_SAVED(node);
  KosExc_Init(KosDynFinally_SAVED(clone));
  KosExc_Copy(KosDynFinally_SAVED(clone), KosDynFinally_SAVED(node));
  KosExc_Init(KosDynFinally_HANDLING(clone));
  KosExc_Copy(KosDynFinally_HANDLING(clone), KosDynFinally_HANDLING(node));
  return clone;
}


/**
   Release both exception states; return node to freelist.
*/
void kos_dynfinally_free(KosDynFinally *node) {
  KosExc_Clear(KosDynFinally_SAVED(node));
  KosExc_Clear(KosDynFinally_HANDLING(node));
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int dynnode_to_dict_finally(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "handler_pc",
                        KosDynFinally_HANDLER_PC((KosDynFinally *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "enabled",
                       KosDynFinally_ENABLED((KosDynFinally *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "handling_raise",
      KosDynFinally_HANDLING_RAISE((KosDynFinally *) node)) < 0)
    return -1;
  if (snapshot_set_int(data, "handling_control",
      KosDynFinally_HANDLING_CONTROL((KosDynFinally *) node)) < 0)
    return -1;
  return 0;
}


/**
   Push FINALLY as DYN_TOP. Callers: opcode PUSH_FINALLY.
*/
KosDynFinally *kos_dyn_push_finally(KosEvalState *es, uint32_t handler_pc) {
  KosDynFinally *node = kos_dynfinally_new(KosEvalState_DYN_TOP(es), handler_pc);
  if (! node)
    return NULL;
  if (kos_dyn_push(es, (KosDynNode *) node) < 0) {
    kos_dynfinally_free(node);
    return NULL;
  }
  return node;
}


/**
   Pop FINALLY when top. Callers: abort_push_region, POP_FINALLY fast path.
*/
int kos_dyn_pop_finally(KosEvalState *es, KosDynFinally *node) {
  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "FINALLY node is not at top of chain");
    return -1;
  }
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_region_leave_exc((KosDynNode *) node);
    kos_dyn_commit_dyn_top(es, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dynfinally_free(node);
  }
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_FINALLY: both exception states.
*/
static int dynnode_traverse_finally(KosDynNode *node, visitproc visit, void *arg) {
  if (KosExc_Traverse(KosDynFinally_SAVED((KosDynFinally *) node), visit, arg))
    return -1;
  return KosExc_Traverse(KosDynFinally_HANDLING((KosDynFinally *) node),
                         visit, arg);
}


// The end.
