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
   Move the running aside onto a prompt. See dynchain.h for the
   contract. Callers: opcodes ABORT, SHIFT_VAR, SHIFT_FAST.
*/
KosOpResult kos_abort_to_prompt(KosContext *ctx, KosDynPrompt *prompt) {

  KosDynNode *aside = (KosDynNode *) KosContext_FRAME(ctx);
  KosDynNode *base;
  KosXferGoal goal;

  if (unlikely(! aside || KosContext_DYN_TOP(ctx) != aside)) {
    PyErr_SetString(PyExc_SystemError,
        "abort: the running frame is not at the top of the chain");
    return EVAL_RAISE();
  }

  /* Take the aside off the chain as it stands: its pc goes into the
     frame, and the registers go back to what it stood on. From here it
     belongs to the goal, linked to nothing. */
  KosContext_FlushLiveRegsToFrame(ctx);
  kos_dyn_commit_dyn_top(ctx, KosDynNode_PARENT(aside));
  KosDynNode_PARENT(aside) = NULL;
  kos_dynnode_note_parent(aside);
  KosSegment_BACK(KosDynNode_STACK(aside)) = NULL;

  /* it will stand above the frame under the prompt */
  base = KosDynNode_PARENT((KosDynNode *) prompt);
  while (base && ! KosDynNode_IS_FRAME(base))
    base = KosDynNode_PARENT(base);
  KosDynFrame_DEPTH(aside) = base ? KosDynFrame_DEPTH(base) + 1 : 1;
  KosDynFrame_FLAGS(aside) =
      (KosDynFrame_FLAGS(aside) & ~KOS_FRAME_DEMAND)
      | (base ? KosDynFrame_FLAGS(base) & KOS_FRAME_DEMAND : 0);
  KosDynFrame_BACK_FRAME(aside) = NULL;

  /* The target is the one frame, which ATTACHING links on the prompt,
     the seam. No payload: the aside takes nothing from its stack. No
     resume pc: the aside carries its own, and the frame below the
     prompt already stands after the reset, where the aside's return,
     which ends the prompt, is delivered. The transfer runs here, from
     a goal in this frame, and is kept only if a leave on the way
     pauses or fails. */
  kos_xfer_goal_init(&goal, aside, (KosDynNode *) prompt, NULL, NULL, NULL, 0);

  /* what a clone for an attach records, for a copy of one frame: its
     back link and its segment's are open for the seam's registers */
  goal.shape.bottom = aside;
  goal.shape.frames = 1;
  goal.shape.top.top = aside;
  goal.shape.top.frame = aside;
  goal.shape.top.block = &KosDynNode_STACK(aside);
  goal.shape.top.ns = KosDynNode_NS(aside);
  goal.shape.open_frame = aside;
  goal.shape.open_segment = KosDynNode_STACK(aside);
  return kos_xfer_goal_run(ctx, &goal);
}


// The end.
