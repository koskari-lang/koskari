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
  MATCH_CASE: arg = extra index. Schema (LABEL, CONST, CONST, COUNT):
  the one-instruction form of a match arm over the native engine the
  matcher used to be. The compiler lowers every pattern to the MATCH_*
  instructions and never emits this one; the opcode keeps its number
  in the table and the handler raises.
*/
KOS_OP_HANDLER(MATCH_CASE) {
  KOS_SYNC_PC(es, insn);
  PyErr_SetString(PyExc_RuntimeError,
                  "MATCH_CASE is not executable: patterns compile to"
                  " the MATCH_* instructions");
  RESULT_RAISE();
}


// The end.
