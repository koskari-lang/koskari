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
   TAIL_CALL_FAST_STAR: arg = extra index. Schema FASTVAR (slot), STAR
   (bitflags). Call callable from fast local slot. Stack [pos, kwd] or
   subsets per flags (1 = *args, 2 = **kwargs). Pop per flags, ensure pos
   is tuple, call via KosEvalState_CallArgs. Tail call: do not push a new
   frame; remove or recycle the current one.
*/


static int ensure_tuple_tail_fast_star(PyObject **val) {
  if (! (val && *val))
    return -1;

  if (! PyTuple_CheckExact(*val)) {
    PyObject *fixed = PySequence_Tuple(*val);
    Py_DECREF(*val);
    *val = fixed;

    if (! fixed)
      return -1;
  }

  return 0;
}


KOS_OP_HANDLER_COLD(TAIL_CALL_FAST_STAR) {
  const KosExtra *row = KosOp_ROW(insn);
  uint32_t slot_idx = KosExtra_IMM(row, 0);
  unsigned opa = KosExtra_IMM(row, 1);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *callable = kos_ns_fast_deref(ns, (size_t) slot_idx);
  if (! callable)
    RESULT_RAISE();

  PyObject *pos = NULL;
  PyObject *kwd = NULL;

  if ((opa & 3) == 3) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(callable);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
    if (ensure_tuple_tail_fast_star(&pos)) {
      Py_DECREF(callable);
      Py_DECREF(pos);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
  } else if (opa & 2) {
    kwd = KosEvalState_StackPopRef(es);
    if (! kwd) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(callable);
      Py_DECREF(kwd);
      RESULT_RAISE();
    }
  } else if (opa & 1) {
    pos = KosEvalState_StackPopRef(es);
    if (! pos) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
    if (ensure_tuple_tail_fast_star(&pos)) {
      Py_DECREF(callable);
      Py_DECREF(pos);
      RESULT_RAISE();
    }
    kwd = NULL;
  } else {
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(callable);
      RESULT_RAISE();
    }
    kwd = NULL;
  }

  KosOpResult res = KosEvalState_CallArgs(es, callable, pos, kwd, 1);

  Py_DECREF(callable);
  Py_DECREF(pos);
  Py_XDECREF(kwd);
  RESULT_RELAY(es, res);
}


// The end.
