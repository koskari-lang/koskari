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
  AWAIT: arg = stack element count. 1: pop TOS (awaitable), start/resume
  awaiting it; may return EVAL_AWAIT(val) or push the result. 0: bare
  cooperative suspend — no stack effect; EVAL_AWAIT(Py_None) payload
  and no py_await_iter.
*/
KOS_OP_HANDLER(AWAIT) {
  uint32_t arg = KosOp_ARG(insn);

  if (arg == 0u)
    return EVAL_AWAIT(Py_NewRef(Py_None));

  if (unlikely(arg != 1u)) {
    PyErr_Format(PyExc_RuntimeError,
                 "AWAIT: arg must be 0 or 1 (stack count), got %u",
                 (unsigned) arg);
    RESULT_RAISE();
  }

  PyObject *val = KosEvalState_StackPopRef(es);
  if (unlikely(! val))
    RESULT_RAISE();

  RESULT_RELAY(es, kos_await_dispatch(es, val));
}


// The end.
