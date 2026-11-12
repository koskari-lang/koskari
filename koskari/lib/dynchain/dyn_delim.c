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
  Included by dynchain.c. The KOS_DYN_DELIM node: the return bookmark
  of a delimited continuation invoke.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynDelim implementation === */


/**
   Allocate DELIM marker for delimited return bookkeeping. It records
   the return pc alone; the frame it returns into is the first FRAME
   below it wherever it sits (kos_dyndelim_return_frame).
*/
KosDynDelim *kos_dyndelim_new(KosDynNode *parent, KosOp *pc) {

  KosDynDelim *delim = kos_dynnode_alloc();
  if (! delim)
    return NULL;

  kos_dynnode_init(delim, KOS_DYN_DELIM, parent, 0);
  KosDynDelim_RETURN_PC(delim) = pc;
  return delim;
}


/**
   Clone DELIM payload: the return pc. A clone attached with a captured
   segment returns into the frame it then sits on.
*/
static KosDynDelim *kos_dyndelim_clone(KosDynDelim *delim) {
  if (! delim)
    return NULL;

  KosDynDelim *clone = kos_dynnode_clone_base(delim);
  if (! clone)
    return NULL;

  KosDynDelim_RETURN_PC(clone) = KosDynDelim_RETURN_PC(delim);
  return clone;
}


/**
   Free DELIM node.
*/
void kos_dyndelim_free(KosDynDelim *delim) {
  if (! delim)
    return;

  kos_dynnode_return_to_freelist(delim);
}


static int dynnode_to_dict_delim(KosDynNode *node, PyObject *data) {

  KosDynDelim *delim = (KosDynDelim *) node;

  return snapshot_set_uint(data, "pc",
                           kos_insn_pc(KosDynDelim_RETURN_PC(delim)));
}


/**
   Push DELIM marker via kos_dyn_push.
*/
KosDynDelim *kos_dyn_push_delim(KosContext *ctx, KosOp *pc) {

  // CONTEXT: dirty, touches context directly
  KosDynDelim *delim = kos_dyndelim_new(KosContext_DYN_TOP(ctx), pc);
  if (! delim)
    return NULL;

  if (kos_dyn_push(ctx, (KosDynNode *) delim) < 0) {
    kos_dyndelim_free(delim);
    return NULL;
  }

  return delim;
}


/**
   Pop DELIM marker from top of chain.
*/
int kos_dyn_pop_delim(KosContext *ctx, KosDynDelim *delim) {
  if (! KosContext_DYN_TOP(ctx) || KosContext_DYN_TOP(ctx) != (KosDynNode *) delim) {
    PyErr_SetString(PyExc_RuntimeError, "DELIM node is not at top of chain");
    return -1;
  }

  kos_dyn_uninstall_handler(ctx, (KosDynNode *) delim, KOS_XFER_EVENT_NONE,
                            NULL);
  {
    KosDynNode *parent = KosDynNode_PARENT((KosDynNode *) delim);

    kos_dyn_commit_dyn_top(ctx, parent);
    KosDynNode_PARENT((KosDynNode *) delim) = NULL;
    kos_dyndelim_free(delim);
  }
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_DELIM: no PyObject refs on the node.
*/
static int dynnode_traverse_delim(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}


// The end.
