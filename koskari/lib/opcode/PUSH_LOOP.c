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
   PUSH_LOOP: no arg. Break target is the pc after the POP_LOOP closing
   the LOOP extent, resolved once per site into the cache slot. Push
   LOOP region (with its own segment); continue target = next
   instruction. Initializes region result to None.
*/

KOS_OP_HANDLER(PUSH_LOOP) {
  KOS_SYNC_PC(ctx, insn);
  uint32_t continue_pc = KosEvalState_PC(ctx);  /* next insn; fetch advanced */
  uint32_t pop_pc;

  /* break lands after the POP_LOOP that closes the extent */
  if (kos_site_extent_close(ctx, insn, KOS_EXT_LOOP, "PUSH_LOOP", &pop_pc) < 0)
    RESULT_RAISE();

  KosDynNode *region = (KosDynNode *) kos_dyn_push_loop(
      ctx, pop_pc + 1u, continue_pc);
  if (! region) {
    PyErr_NoMemory();
    RESULT_RAISE();
  }

  /* Initialize loop result to None */
  Py_INCREF(Py_None);
  KosDynLoop_RESULT((KosDynLoop *) region) = Py_None;
  RESULT_NEXT(ctx);
}


// The end.
