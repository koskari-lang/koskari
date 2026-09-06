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
   CALL_FAST_STAR: arg = extra index. Schema FASTVAR (slot), STAR
   (bitflags). Callable from the fast local slot; stack [pos, kwd] per
   flags.
*/
KOS_OP_HANDLER_COLD(CALL_FAST_STAR) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *callable = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 0));
  if (! callable)
    RESULT_RAISE();

  KosOpResult res = kos_op_call_star(
      es, callable, NULL, NULL, KosExtra_IMM(row, 1), 0);
  RESULT_RELAY(es, res);
}


// The end.
