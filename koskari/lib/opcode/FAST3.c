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
  FAST3: arg = extra index. Schema FASTVAR x3. Load the three slots from the
  current fast namespace and push the values in row order. Every slot
  resolves before any push, so an empty slot raises with nothing
  pushed; a push failure releases what the handler still holds, the
  failed value included by the push itself.
*/
KOS_OP_HANDLER(FAST3) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *a = kos_ns_fast_deref(ns, (size_t) row.a0);
  if (unlikely(! a))
    RESULT_RAISE();

  PyObject *b = kos_ns_fast_deref(ns, (size_t) row.a1);
  if (unlikely(! b)) {
    Py_DECREF(a);
    RESULT_RAISE();
  }

  PyObject *c = kos_ns_fast_deref(ns, (size_t) row.a2);
  if (unlikely(! c)) {
    Py_DECREF(a);
    Py_DECREF(b);
    RESULT_RAISE();
  }

  if (unlikely(KosEvalState_StackPUSH(es, a) < 0)) {
    Py_DECREF(b);
    Py_DECREF(c);
    RESULT_RAISE();
  }

  if (unlikely(KosEvalState_StackPUSH(es, b) < 0)) {
    Py_DECREF(c);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, c);
}


// The end.
