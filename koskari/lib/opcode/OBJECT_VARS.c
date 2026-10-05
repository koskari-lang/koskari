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
  OBJECT_VARS: arg unused. Pop TOS, push getattr(TOS, "__dict__").
*/
KOS_OP_HANDLER(OBJECT_VARS) {
  (void) insn;

  PyObject *val = KosEvalState_StackPopRef(es);
  if (unlikely(! val))
    RESULT_RAISE();

  PyObject *d = PyObject_GetAttrString(val, "__dict__");
  Py_DECREF(val);

  /* only a missing __dict__ is the caller's type error; any other
     error from the lookup propagates as raised */
  if (unlikely(! d)) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
      PyErr_SetString(PyExc_TypeError,
                      "vars() argument must have __dict__ attribute");
    }
    RESULT_RAISE();
  }

  RESULT_STEAL(es, d);
}


// The end.
