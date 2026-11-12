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
  Included by dynchain.c. A shift's abort to its prompt.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === Shift abort === */


/**
   Abort a shift to its prompt and call its handler there. See
   dynchain.h for the contract. Callers: opcode SHIFT.
*/
int kos_shift_abort(
    KosContext *ctx,
    KosDynPrompt *prompt,
    PyObject *handler,
    PyObject *k) {

  /* No resume pc: the frame below the prompt already stands after the
     reset, and the handler's return, which ends the prompt, lands
     there as the reset's value. */
  KosXferGoal *goal = kos_xfer_goal_new(
      NULL, (KosDynNode *) prompt, handler, NULL, k, 0);

  if (! goal)
    return -1;

  /* DELIVERING calls the handler with k above the prompt */
  goal->shift_abort = 1;
  kos_xfer_goal_push(ctx, goal);
  return 0;
}


// The end.
