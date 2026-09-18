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
  THEEND: arg unused. The end-of-code barrier appended to every code object
  during assembly (KosCode_New). Reaching it means the program counter walked
  off the end -- code that failed to hit a terminal RETURN -- so it faults
  exactly as a real off-the-end fetch does: a SystemError and a hard
  KOS_RES_FATAL. Its presence is what lets the dispatch loop omit a per-opcode
  PC bounds check. Users cannot supply THEEND (rejected in code_collect); we
  are its only source.
*/
KOS_OP_HANDLER(THEEND) {
  (void) insn;
  kos_eval_report_pc_bounds_error(es);
  RESULT_FAIL();
}


// The end.
