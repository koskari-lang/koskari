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
  Included by dynchain.c. An abort to a prompt.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === Abort === */


/**
   Leave everything up to a prompt and run an aside above it. See
   dynchain.h for the contract. Callers: opcodes ABORT, SHIFT.
*/
KosOpResult kos_abort_to_prompt(
    KosContext *ctx,
    KosDynPrompt *prompt,
    const KosExtentDesc *aside,
    PyObject *payload) {

  KosDynNode *base;
  KosDynNode *frame;
  KosXferGoal goal;
  PyObject *ns;
  int owns_ns;

  ns = kos_aside_namespace(ctx, aside, &owns_ns);
  if (! ns)
    return EVAL_RAISE();

  /* An aside that introduces no namespace runs over the live one,
     which belongs to a frame the abort leaves: the namespace is
     revealed, so that frame's release does not reclaim it. */
  if (! owns_ns)
    KosNamespace_TrackChain(ns);

  /* it will stand above the frame under the prompt, linked by the
     transfer: its back link and its segment's are open for the seam's
     registers */
  base = KosDynNode_PARENT((KosDynNode *) prompt);
  while (base && ! KosDynNode_IS_FRAME(base))
    base = KosDynNode_PARENT(base);

  frame = (KosDynNode *) kos_dynframe_new_aside(
      NULL, (KosDynFrame *) base, KosDynFrame_CODE(KosContext_FRAME(ctx)),
      ns, owns_ns, aside->entry);
  if (owns_ns)
    Py_DECREF(ns);
  if (! frame)
    return EVAL_RAISE();
  KosDynFrame_BACK_FRAME(frame) = NULL;
  KosSegment_BACK(KosDynNode_STACK(frame)) = NULL;

  /* The target is the one frame, which ATTACHING links on the prompt,
     the seam, and which belongs to the goal until then. No resume pc:
     the frame starts at the aside's entry, and the frame below the
     prompt already stands after the reset, where the aside's return,
     which ends the prompt, is delivered. The transfer runs here, from
     a goal in this frame, and is kept only if a leave on the way
     pauses or fails. */
  kos_xfer_goal_init(&goal, frame, (KosDynNode *) prompt, payload, NULL, NULL,
                     0);

  /* what a clone for an attach records, for a copy of one frame */
  goal.shape.bottom = frame;
  goal.shape.frames = 1;
  goal.shape.top.top = frame;
  goal.shape.top.frame = frame;
  goal.shape.top.block = &KosDynNode_STACK(frame);
  goal.shape.top.ns = KosDynNode_NS(frame);
  goal.shape.open_frame = frame;
  goal.shape.open_segment = KosDynNode_STACK(frame);
  return kos_xfer_goal_run(ctx, &goal);
}


// The end.
