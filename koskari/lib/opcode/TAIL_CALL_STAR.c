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
   TAIL_CALL_STAR: arg = bitflags (1 = has *args, 2 = has **kwargs). Stack
   [callable, pos, kwd] with kwd at TOS, pos and kwd present per flag.
   Tail call: the callee replaces the current frame.
*/
KOS_OP_HANDLER_COLD(TAIL_CALL_STAR) {
  KosOpResult res = kos_op_call_star(
      es, NULL, NULL, NULL, KosOp_ARG(insn), 1);
  RESULT_RELAY(es, res);
}


// The end.
