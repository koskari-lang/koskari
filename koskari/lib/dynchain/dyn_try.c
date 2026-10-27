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
  Included by dynchain.c. The KOS_DYN_TRY region node.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynTry (try region) === */


/**
   Allocate TRY with handler_pc; INTERCEPTS_RAISE cap for kos_dyn_handle.
*/
KosDynTry *kos_dyntry_new(KosDynNode *parent, KosOp *handler_pc) {
  KosDynTry *node = kos_dynnode_alloc();

  if (! node)
    return NULL;

  kos_dynnode_init(node, KOS_DYN_TRY, parent,
                   KOS_DYN_CAP_INTERCEPTS_RAISE);
  KosDynTry_HANDLER_PC(node) = handler_pc;
  KosDynTry_ENABLED(node) = 1;
  KosDynTry_EXC_SAVED(node) = 0;
  KosDynTry_EXC_FROZEN(node) = 0;
  KosExc_Init(KosDynTry_SAVED(node));
  kos_dynnode_use_inline_segment((KosDynNode *) node);
  return node;
}


/**
   Clone enabled flag and handler_pc for continuation capture.
*/
KosDynTry *kos_dyntry_clone(KosDynTry *node) {
  KosDynTry *clone = kos_dynnode_clone_base(node);
  if (! clone)
    return NULL;
  KosDynTry_HANDLER_PC(clone) = KosDynTry_HANDLER_PC(node);
  KosDynTry_ENABLED(clone) = KosDynTry_ENABLED(node);
  KosDynTry_EXC_SAVED(clone) = KosDynTry_EXC_SAVED(node);
  KosDynTry_EXC_FROZEN(clone) = KosDynTry_EXC_FROZEN(node);
  KosExc_Init(KosDynTry_SAVED(clone));
  KosExc_Copy(KosDynTry_SAVED(clone), KosDynTry_SAVED(node));
  return clone;
}


/**
   Release the saved exception state; return TRY node to freelist.
*/
void kos_dyntry_free(KosDynTry *node) {
  KosExc_Clear(KosDynTry_SAVED(node));
  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int dynnode_to_dict_try(KosDynNode *node, PyObject *data) {

  if (snapshot_set_uint(data, "handler_pc",
                        kos_insn_pc(KosDynTry_HANDLER_PC(
                            (KosDynTry *) node))) < 0)
    return -1;
  if (snapshot_set_int(data, "enabled",
                       KosDynTry_ENABLED((KosDynTry *) node)) < 0)
    return -1;
  return 0;
}


/**
   Push TRY as DYN_TOP. Callers: opcode PUSH_TRY.
*/
KosDynTry *kos_dyn_push_try(KosContext *ctx, KosOp *handler_pc) {
  KosDynTry *node = kos_dyntry_new(KosContext_DYN_TOP(ctx), handler_pc);
  if (! node)
    return NULL;
  if (kos_dyn_push(ctx, (KosDynNode *) node) < 0) {
    kos_dyntry_free(node);
    return NULL;
  }
  return node;
}


/**
   Pop TRY when it is top (fast path). Callers: opcode POP_TRY.
*/
int kos_dyn_pop_try(KosContext *ctx, KosDynTry *node) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "TRY node is not at top of chain");
    return -1;
  }

  KOS_DYN_POP_REQUIRE_KIND(node, KOS_DYN_TRY, "POP_TRY", "TRY");
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) node);

    kos_dyn_region_leave_exc((KosDynNode *) node);
    kos_dyn_commit_dyn_top(ctx, parent);
    KosDynNode_PARENT((KosDynNode *) node) = NULL;
    kos_dyntry_free(node);
  }
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_TRY: the saved exception state.
*/
static int dynnode_traverse_try(KosDynNode *node, visitproc visit, void *arg) {
  return KosExc_Traverse(KosDynTry_SAVED((KosDynTry *) node), visit, arg);
}


// The end.
