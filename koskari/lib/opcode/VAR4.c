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
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *name;

  name = KosEvalState_CONST(es, row.a0);
  PyObject *a = name ? frame_ns_deref(es, name) : NULL;
  if (unlikely(! a))
    RESULT_RAISE();

  name = KosEvalState_CONST(es, row.a1);
  PyObject *b = name ? frame_ns_deref(es, name) : NULL;
  if (unlikely(! b)) {
    Py_DECREF(a);
    RESULT_RAISE();
  }

  name = KosEvalState_CONST(es, row.a2);
  PyObject *c = name ? frame_ns_deref(es, name) : NULL;
  if (unlikely(! c)) {
    Py_DECREF(a);
    Py_DECREF(b);
    RESULT_RAISE();
  }

  name = KosEvalState_CONST(es, row.a3);
  PyObject *d = name ? frame_ns_deref(es, name) : NULL;
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
