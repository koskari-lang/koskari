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
   Allocate DELIM marker for delimited return bookkeeping.
*/
KosDynDelim *kos_dyndelim_new(
    KosDynNode *parent,
    uint8_t mode,
    KosDynNode *chain,
    uint32_t pc,
    KosDynFrame *frame,
    KosDynNode *node) {

  KosDynDelim *delim = kos_dynnode_alloc();
  if (! delim)
    return NULL;

  kos_dynnode_init(delim, KOS_DYN_DELIM, parent,
                   mode == KOS_DYN_DELIM_MODE_INPLACE
                       ? KOS_DYN_CAP_INTERCEPTS_RAISE : 0);
  KosDynDelim_MODE(delim) = mode;
  KosDynDelim_RETURN_CHAIN(delim) = chain;
  KosDynDelim_RETURN_PC(delim) = pc;
  KosDynDelim_RETURN_FRAME_NODE(delim) = (KosDynNode *) frame;
  KosDynDelim_RETURN_NODE(delim) = node;
  KosDynDelim_SAVED_PC(delim) = 0;
  KosDynDelim_SAVED_ACTING_PC(delim) = 0;
  KosDynDelim_SAVED_RESUME_KIND(delim) = NULL;
  KosDynDelim_SAVED_RESUME_STATE(delim) = NULL;
  return delim;
}


/**
   Clone DELIM payload.
*/
static KosDynDelim *kos_dyndelim_clone(KosDynDelim *delim) {
  if (! delim)
    return NULL;

  KosDynDelim *clone = kos_dynnode_clone_base(delim);
  if (! clone)
    return NULL;

  /* An INPLACE delim inside a captured segment carries the shift
     handler's remainder: the stashed span is cloned with it, so a
     landing of the template can restitch and run that remainder, and a
     handler snapshot the delim owns (KOS_DYNF_HOLDS_RETURN_FRAME) is
     cloned and owned by the clone in turn. A borrowed return frame is
     copied as is; kos_dyn_remap_delim_frames points it at the frame's
     clone when the segment holds one. */
  KosDynDelim_MODE(clone) = KosDynDelim_MODE(delim);
  KosDynDelim_RETURN_CHAIN(clone) = NULL;
  KosDynDelim_RETURN_PC(clone) = KosDynDelim_RETURN_PC(delim);
  KosDynDelim_RETURN_FRAME_NODE(clone) = KosDynDelim_RETURN_FRAME_NODE(delim);
  if (KosDynDelim_MODE(delim) == KOS_DYN_DELIM_MODE_INPLACE) {
    if (KosDynDelim_RETURN_CHAIN(delim)) {
      KosDynDelim_RETURN_CHAIN(clone) =
          kos_dyn_clone_segment(KosDynDelim_RETURN_CHAIN(delim), NULL);
      if (! KosDynDelim_RETURN_CHAIN(clone)) {
        KosDynNode_FLAGS(clone) &= ~KOS_DYNF_HOLDS_RETURN_FRAME;
        KosDynDelim_RETURN_FRAME_NODE(clone) = NULL;
        kos_dyndelim_free(clone);
        return NULL;
      }
    }
    if (KosDynNode_FLAGS(clone) & KOS_DYNF_HOLDS_RETURN_FRAME) {
      KosDynFrame *snapshot = kos_dynframe_clone(KosDynDelim_RETURN_FRAME(delim));
      if (! snapshot) {
        KosDynNode_FLAGS(clone) &= ~KOS_DYNF_HOLDS_RETURN_FRAME;
        KosDynDelim_RETURN_FRAME_NODE(clone) = NULL;
        kos_dyndelim_free(clone);
        return NULL;
      }
      KosDynDelim_RETURN_FRAME_NODE(clone) = (KosDynNode *) snapshot;
    }
  } else {
    KosDynNode_FLAGS(clone) &= ~KOS_DYNF_HOLDS_RETURN_FRAME;
  }
  /* RETURN_NODE is a self-reference meaning "unwind to me", so copying it
     verbatim leaves the clone pointing at the original -- a pointer out of
     a cloned template and into the live chain, the same hazard
     kos_chain_detach_template guards against for parent. An unwind then
     targets the original, passes through the clone, and frees it while
     POP_PROMPT is still holding it. Re-point self-references at the
     clone; anything else is a genuine external target and is copied. */
  KosDynDelim_RETURN_NODE(clone) =
      (KosDynDelim_RETURN_NODE(delim) == (KosDynNode *) delim)
          ? (KosDynNode *) clone
          : KosDynDelim_RETURN_NODE(delim);
  KosDynDelim_SAVED_PC(clone) = KosDynDelim_SAVED_PC(delim);
  KosDynDelim_SAVED_ACTING_PC(clone) = KosDynDelim_SAVED_ACTING_PC(delim);

  /* A displaced resume follows the rule a frame clone applies to its
     own: the kind's clone carries the state, and a kind without one
     refuses the capture by name. */
  KosDynDelim_SAVED_RESUME_KIND(clone) = NULL;
  KosDynDelim_SAVED_RESUME_STATE(clone) = NULL;
  if (KosDynDelim_SAVED_RESUME_KIND(delim)) {
    const KosResumeKind *kind = KosDynDelim_SAVED_RESUME_KIND(delim);

    if (! kind->clone) {
      PyErr_Format(PyExc_RuntimeError,
                   "cannot capture a frame whose resume is %s",
                   kind->name ? kind->name : "?");
      kos_dyndelim_free(clone);
      return NULL;
    }
    KosDynDelim_SAVED_RESUME_KIND(clone) = kind;
    KosDynDelim_SAVED_RESUME_STATE(clone) =
        kind->clone(KosDynDelim_SAVED_RESUME_STATE(delim));
  }
  return clone;
}


/**
   Free DELIM payload and node. A held yoyo return-address snapshot
   (KOS_DYNF_HOLDS_RETURN_FRAME) dies with the delim: teardown without a
   restitch means no chain ever took ownership of it.
*/
void kos_dyndelim_free(KosDynDelim *delim) {
  if (! delim)
    return;

  if (KosDynDelim_RETURN_CHAIN(delim)) {
    kos_dynchain_free(KosDynDelim_RETURN_CHAIN(delim), NULL);
    KosDynDelim_RETURN_CHAIN(delim) = NULL;
  }

  if (KosDynNode_FLAGS((KosDynNode *) delim)
      & KOS_DYNF_HOLDS_RETURN_FRAME) {
    kos_dynframe_free(KosDynDelim_RETURN_FRAME(delim));
    KosDynDelim_RETURN_FRAME_NODE(delim) = NULL;
  }

  /* a displaced resume freed with the delimiter gets no delivery */
  if (KosDynDelim_SAVED_RESUME_KIND(delim)) {
    const KosResumeKind *kind = KosDynDelim_SAVED_RESUME_KIND(delim);

    if (kind->clear)
      kind->clear(KosDynDelim_SAVED_RESUME_STATE(delim));
    KosDynDelim_SAVED_RESUME_KIND(delim) = NULL;
    KosDynDelim_SAVED_RESUME_STATE(delim) = NULL;
  }

  kos_dynnode_return_to_freelist(delim);
}


static int dynnode_to_dict_delim(KosDynNode *node, PyObject *data) {

  KosDynDelim *delim = (KosDynDelim *) node;
  if (snapshot_set_int(data, "mode", (int) KosDynDelim_MODE(delim)) < 0)
    return -1;
  if (snapshot_set_uint(data, "pc", KosDynDelim_RETURN_PC(delim)) < 0)
    return -1;
  if (snapshot_set_uint(data, "frame_node_id",
      KosDynDelim_RETURN_FRAME_NODE(delim)
          ? kos_dynnode_id(KosDynDelim_RETURN_FRAME_NODE(delim))
          : 0) < 0)
    return -1;
  if (snapshot_set_uint(data, "return_node_id",
      KosDynDelim_RETURN_NODE(delim)
          ? kos_dynnode_id(KosDynDelim_RETURN_NODE(delim))
          : 0) < 0)
    return -1;
  if (snapshot_set_uint(data, "saved_pc", KosDynDelim_SAVED_PC(delim)) < 0)
    return -1;
  return 0;
}


/**
   Push DELIM marker via kos_dyn_push.
*/
KosDynDelim *kos_dyn_push_delim(
    KosContext *ctx,
    uint8_t mode,
    KosDynNode *chain,
    uint32_t pc,
    KosDynFrame *frame,
    KosDynNode *node) {

  // CONTEXT: dirty, touches context directly
  KosDynDelim *delim = kos_dyndelim_new(KosEvalState_DYN_TOP(ctx),
                                        mode,
                                        chain,
                                        pc,
                                        frame,
                                        node);
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
  if (! KosEvalState_DYN_TOP(ctx) || KosEvalState_DYN_TOP(ctx) != (KosDynNode *) delim) {
    PyErr_SetString(PyExc_RuntimeError, "DELIM node is not at top of chain");
    return -1;
  }

  kos_dyn_uninstall_handler(ctx, (KosDynNode *) delim, KOS_XFER_EVENT_NONE);
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
   GC traverse a KOS_DYN_DELIM: visit the owned return chain and a held
   yoyo return-address snapshot -- the snapshot's code, namespace, and
   operand references have no other owner between invoke and restitch.
*/
static int dynnode_traverse_delim(KosDynNode *node, visitproc visit, void *arg) {
  KosDynNode *chain = KosDynDelim_RETURN_CHAIN((KosDynDelim *) node);

  if (KosDynNode_FLAGS(node) & KOS_DYNF_HOLDS_RETURN_FRAME) {
    int tr = kos_dynnode_traverse(
        KosDynDelim_RETURN_FRAME_NODE((KosDynDelim *) node), visit, arg);
    if (tr != 0)
      return tr;
  }
  if (! chain)
    return 0;
  return kos_dynchain_traverse(chain, NULL, visit, arg);
}


// The end.
