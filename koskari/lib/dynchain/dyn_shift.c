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
   Abort to a prompt and run a handler there. See dynchain.h for the
   contract. Callers: opcodes ABORT, SHIFT_VAR, SHIFT_FAST.
*/
KosOpResult kos_abort_to_prompt(
    KosContext *ctx,
    KosDynPrompt *prompt,
    KosOp *entry) {

  PyObject *ns = KosContext_NAMESPACE(ctx);
  KosDynNode *base;
  KosDynFrame *aside;
  KosXferGoal goal;

  /* The aside outlives the frame it is taken from, which the abort
     leaves: it holds the code and the namespace itself, and the
     namespace is revealed, so the frame's release does not reclaim
     it. It stands above the frame under the prompt. */
  KosNamespace_TrackChain(ns);
  base = KosDynNode_PARENT((KosDynNode *) prompt);
  while (base && ! KosDynNode_IS_FRAME(base))
    base = KosDynNode_PARENT(base);
  aside = kos_dynframe_new_aside(
      NULL, (KosDynFrame *) base, KosDynFrame_CODE(KosContext_FRAME(ctx)),
      ns, entry);
  if (! aside)
    return EVAL_RAISE();

  /* The target is the one frame, which ATTACHING links on the prompt,
     the seam. No payload: the handler takes nothing from its stack.
     No resume pc: the aside stands at the handler's first instruction,
     and the frame below the prompt already stands after the reset,
     where the handler's return, which ends the prompt, is delivered.
     The transfer runs here, from a goal in this frame, and is kept
     only if a leave on the way pauses or fails. */
  kos_xfer_goal_init(
      &goal, (KosDynNode *) aside, (KosDynNode *) prompt, NULL, NULL, NULL,
      0);

  /* what a clone for an attach records, for a copy of one frame: its
     back link and its segment's are open for the seam's registers */
  goal.shape.bottom = (KosDynNode *) aside;
  goal.shape.frames = 1;
  goal.shape.top.top = (KosDynNode *) aside;
  goal.shape.top.frame = (KosDynNode *) aside;
  goal.shape.top.block = &KosDynNode_STACK((KosDynNode *) aside);
  goal.shape.top.ns = KosDynNode_NS((KosDynNode *) aside);
  goal.shape.open_frame = (KosDynNode *) aside;
  goal.shape.open_segment = KosDynNode_STACK((KosDynNode *) aside);
  return kos_xfer_goal_run(ctx, &goal);
}


// The end.
