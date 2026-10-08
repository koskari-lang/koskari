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
  YIELD_FAST: arg = fast slot index; yield the value in the slot,
  raising NameError when it is empty, as FAST would.  The convert_fast
  lowering of YIELD_VAR.  Marks the code as yielding.  Stack unchanged.
*/
KOS_OP_HANDLER(YIELD_FAST) {
  KOS_SYNC_PC(es, insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *val = kos_ns_fast_deref(ns, (size_t) KosOp_ARG(insn));
  if (unlikely(! val))
    RESULT_RAISE();
  TAILCALL(kos_yield_deliver, es, insn, val);
}


// The end.
