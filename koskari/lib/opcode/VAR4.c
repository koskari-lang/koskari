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
  VAR4: arg = extra index. Schema VAR x4 (name constants). Look up the
  four names in the current frame namespace and push the values in row
  order. Every name resolves before any push, so a missing name raises
  with nothing pushed; a push failure releases what the handler still
  holds, the failed value included by the push itself.
*/
KOS_OP_HANDLER_COLD(VAR4) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *name;

  name = KosExtra_OBJ(row, 0);
  PyObject *a = frame_ns_deref(es, name);
  if (unlikely(! a))
    RESULT_RAISE();

  name = KosExtra_OBJ(row, 1);
  PyObject *b = frame_ns_deref(es, name);
  if (unlikely(! b)) {
    Py_DECREF(a);
    RESULT_RAISE();
  }

  name = KosExtra_OBJ(row, 2);
  PyObject *c = frame_ns_deref(es, name);
  if (unlikely(! c)) {
    Py_DECREF(a);
    Py_DECREF(b);
    RESULT_RAISE();
  }

  name = KosExtra_OBJ(row, 3);
  PyObject *d = frame_ns_deref(es, name);
  if (unlikely(! d)) {
    Py_DECREF(a);
    Py_DECREF(b);
    Py_DECREF(c);
    RESULT_RAISE();
  }

  if (unlikely(KosEvalState_StackPUSH(es, a) < 0)) {
    Py_DECREF(b);
    Py_DECREF(c);
    Py_DECREF(d);
    RESULT_RAISE();
  }

  if (unlikely(KosEvalState_StackPUSH(es, b) < 0)) {
    Py_DECREF(c);
    Py_DECREF(d);
    RESULT_RAISE();
  }

  if (unlikely(KosEvalState_StackPUSH(es, c) < 0)) {
    Py_DECREF(d);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, d);
}


// The end.
