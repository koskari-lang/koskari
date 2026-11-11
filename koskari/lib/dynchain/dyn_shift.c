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
  Included by dynchain.c. The KOS_DYN_SHIFT node: the shift handler
  bookmark.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynShift implementation === */


/**
   Marker for a shift handler's extent (no frame), with no parent yet.
   It owns the handler body's operand segment and carries ns, the
   namespace live at the shift: the abort leaves the scopes between the
   prompt and the shift, and the handler body still reads their names.
   Callers: kos_shift_abort.
*/
static KosDynShift *kos_dynshift_new(PyObject *ns) {
  KosDynShift *shift = (KosDynShift *) kos_dynnode_alloc();
  if (! shift)
    return NULL;

  kos_dynnode_init((KosDynNode *) shift, KOS_DYN_SHIFT, NULL, 0);
  KosDynNode_NS(shift) = KosNamespace_Reveal(ns);
  kos_dynnode_use_inline_segment((KosDynNode *) shift);
  return shift;
}


/**
   Clone for captured chain segments.
*/
static KosDynShift *kos_dynshift_clone(KosDynShift *shift) {
  if (! shift)
    return NULL;

  return (KosDynShift *) kos_dynnode_clone_base((KosDynNode *) shift);
}


/**
   Return shift node to freelist.
*/
static void kos_dynshift_free(KosDynShift *shift) {
  if (! shift)
    return;

  kos_dynnode_return_to_freelist(shift);
}


/**
   Abort a shift to its prompt. See dynchain.h for the contract.
   Callers: opcode PUSH_SHIFT.
*/
int kos_shift_abort(
    KosContext *ctx,
    KosDynPrompt *prompt,
    PyObject *k,
    KosOp *body_pc) {

  PyObject *ns = KosContext_NAMESPACE(ctx);
  KosDynFrame *running = NULL;
  KosDynNode *target;
  KosDynNode *node;
  KosXferGoal *goal;

  /* A frame between dyn_top and the prompt is the frame the shift ran
     in: the first one met is the live frame. */
  for (node = KosContext_DYN_TOP(ctx);
       node && node != (KosDynNode *) prompt;
       node = KosDynNode_PARENT(node)) {
    if (KosDynNode_IS_FRAME(node)) {
      running = (KosDynFrame *) node;
      break;
    }
  }

  target = (KosDynNode *) kos_dynshift_new(ns);
  if (! target)
    return -1;

  if (running) {
    KosDynFrame *handler;

    /* the copy takes the frame's pc fields, which live on the context
       while the frame runs */
    KosContext_FlushLiveRegsToFrame(ctx);
    handler = kos_dynframe_clone(running);
    if (! handler) {
      kos_dynchain_free(target, NULL);
      return -1;
    }

    /* The handler pushes its own operands; the frame's pending ones
       belong to the region and are in the continuation. Its namespace
       is the one live at the shift, as the SHIFT's is. */
    if (KosDynNode_STACK(handler))
      KosStack_Clear(&KosDynNode_STACK(handler));
    if (ns != KosDynFrame_NS(handler)) {
      PyObject *own = KosDynFrame_NS(handler);

      KosDynFrame_NS(handler) = KosNamespace_Reveal(ns);
      Py_XDECREF(own);
    }
    KosDynNode_PARENT(handler) = target;
    kos_dynnode_note_parent((KosDynNode *) handler);
    KosDynFrame_FLAGS(handler) |= KOS_FRAME_SHIFT_HANDLER;
    target = (KosDynNode *) handler;
  }

  /* a failure frees target */
  goal = kos_xfer_goal_new(target, (KosDynNode *) prompt, k, body_pc,
                           NULL, 0);
  if (! goal)
    return -1;

  /* The region's operands are in the continuation, but they stay on
     the prompt until the abort can no longer be cancelled: a finally
     body that raises, breaks or returns on the way out leaves the
     region running on them. ATTACHING drops them. */
  goal->drop_seam_operands = 1;
  kos_xfer_goal_push(ctx, goal);
  return 0;
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_SHIFT: no PyObject refs on the node.
*/
static int dynnode_traverse_shift(KosDynNode *node, visitproc visit, void *arg) {
  (void) node;
  (void) visit;
  (void) arg;
  return 0;
}


static int dynnode_to_dict_shift(KosDynNode *node, PyObject *data) {

  (void) node;
  (void) data;
  return 0;
}


// The end.
