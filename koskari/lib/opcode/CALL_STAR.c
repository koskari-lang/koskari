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
   CALL_STAR and TAIL_CALL_STAR: arg = bitflags (1 = has *args, 2 = has
   **kwargs). Stack [callable, pos, kwd] with kwd at TOS, pos and kwd present
   per flag. A TAIL_CALL instruction reaches this same handler and adds the
   tail bit, which the core turns into a frame reset instead of a push.
*/
KOS_OP_HANDLER_COLD(CALL_STAR) {
  int tail = KOS_OP_IS_TAIL_CALL(KosOp_OP(insn));
  KosOpResult res = kos_op_call_star(
      es, NULL, NULL, NULL, KosOp_ARG(insn), tail);
  RESULT_RELAY(es, res);
}


// The end.
