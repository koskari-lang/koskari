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
  MATCH_STORE_NAMES: arg = cpool index of a names tuple. Stack [..., caps]
  with the matched arm's capture dict = TOS. Pop the dict and bind each
  named capture into the current namespace, then fall through to the arm
  body. Single-cell captures are revealed from their Datum box; repeat
  frames and rest slices arrive as plain lists/tuples and pass through. A
  name absent from the dict (an OPTIONAL that did not fire) is left unbound.
*/

KOS_OP_HANDLER_COLD(MATCH_STORE_NAMES) {
  PyObject *names = KosEvalState_CONST(es, KosOp_ARG(insn));
  if (unlikely(! names || ! PyTuple_Check(names))) {
    PyErr_SetString(PyExc_SystemError,
                    "MATCH_STORE_NAMES requires a names tuple const");
    return EVAL_RAISE();
  }

  PyObject *caps = KosEvalState_StackPopRef(es);
  if (unlikely(! caps))
    return EVAL_RAISE();

  Py_ssize_t n = PyTuple_GET_SIZE(names);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *name = PyTuple_GET_ITEM(names, i);
    PyObject *val = PyDict_GetItemWithError(caps, name);
    if (! val) {
      if (unlikely(PyErr_Occurred())) {
        Py_DECREF(caps);
        return EVAL_RAISE();
      }
      continue;
    }

    if (KosDatum_Check(val)) {
      PyObject *inner = ((KosDatum *) val)->datum;
      if (inner)
        val = inner;
    }

    Py_INCREF(val);
    int r = frame_ns_store(es, name, val);
    Py_DECREF(val);
    if (unlikely(r < 0)) {
      Py_DECREF(caps);
      return EVAL_RAISE();
    }
  }

  Py_DECREF(caps);
  return EVAL_NORMAL();
}


// The end.
