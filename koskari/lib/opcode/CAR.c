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
  CAR: arg = repeat count. Pop TOS once; walk car that many times in locals
  (refcount-safe); push the final value. arg < 1 is an error. Fails if any
  intermediate is not a pair or is nil.
*/
KOS_OP_HANDLER(CAR) {
  KOS_SYNC_PC(ctx, insn);
  int opa = (int) KosOp_ARG(insn);
  if (unlikely(opa <= 0)) {
    PyErr_Format(PyExc_RuntimeError, "CAR invoked with invalid"
                 " argument count: %i", opa);
    RESULT_RAISE();
  }

  PyObject *cur = KosEvalState_StackPopRef(ctx);
  if (unlikely(! cur))
    RESULT_RAISE();

  for (int i = 0; i < opa; i++) {
    if (! KosPair_Check(cur) || KosNilP(cur)) {
      const char *got = KosNilP(cur)
          ? "nil"
          : Py_TYPE(cur)->tp_name;
      Py_DECREF(cur);

      PyErr_Format(PyExc_TypeError,
                   "CAR at index %d of %d requires a non-nil pair, got %s",
                   i, opa, got);
      RESULT_RAISE();
    }

    PyObject *next = KosPair_CAR(cur);
    Py_INCREF(next);
    Py_DECREF(cur);
    cur = next;
  }

  RESULT_STEAL(ctx, cur);
}


// The end.
