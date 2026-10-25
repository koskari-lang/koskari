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
   PUSH_CC: no arg. Push a CC block onto the dynamic chain. The resume pc
   (the instruction after the matching POP_CC) comes from the CC extent,
   resolved once per site into the cache slot.
*/

KOS_OP_HANDLER(PUSH_CC) {
  KOS_SYNC_PC(ctx, insn);
  /* the continuation resumes after the POP_CC closing the extent */
  uint32_t pop_pc;
  if (kos_site_extent_close(ctx, insn, KOS_EXT_CC, "PUSH_CC", &pop_pc) < 0)
    RESULT_RAISE();
  if (! kos_dyn_push_cc(ctx, pop_pc + 1u)) {
    RESULT_RAISE();
  }
  RESULT_NEXT(ctx);
}


// The end.
