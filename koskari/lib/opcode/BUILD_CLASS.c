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
   BUILD_CLASS: arg unused. Stack: [..., pos, ns] with TOS = ns,
   TOS-1 = pos. pos = (name, bases) or (name, bases, metaclass).
   Pops ns, pos; builds class via kos_build_class (prepare, composite meta,
   namespace merge); pushes resulting class.
*/

KOS_OP_HANDLER(BUILD_CLASS) {
  (void) out_ev;
  (void) insn;

  PyObject *ns = KosEvalState_StackPopRef(es);
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "BUILD_CLASS stack underrun (ns)");
    return KOS_OP_POST_ERR();
  }

  SAYF("BUILD_CLASS: ns=%R", ns);

  PyObject *pos = KosEvalState_StackPopRef(es);
  if (! pos) {
    Py_DECREF(ns);
    PyErr_SetString(PyExc_RuntimeError, "BUILD_CLASS stack underrun (pos)");
    return KOS_OP_POST_ERR();
  }

  SAYF("BUILD_CLASS: pos=%R", pos);

  if (! PyTuple_CheckExact(pos)) {
    PyObject *fixed = PySequence_Tuple(pos);
    Py_DECREF(pos);
    pos = fixed;
    if (! pos) {
      Py_DECREF(ns);
      return KOS_OP_POST_ERR();
    }
  }

  PyObject *name = NULL;
  PyObject *bases = NULL;
  PyObject *metaclass = NULL;

  if (! PyArg_ParseTuple(pos, "OO|O:BUILD_CLASS", &name, &bases, &metaclass)) {
    Py_DECREF(pos);
    Py_DECREF(ns);
    return KOS_OP_POST_ERR();
  }
  Py_INCREF(name);
  Py_INCREF(bases);
  Py_XINCREF(metaclass);
  Py_DECREF(pos);

  if (metaclass == Py_None) {
    Py_CLEAR(metaclass);
  }

  SAYF("BUILD_CLASS: name=%R, bases=%R, metaclass=%R", name, bases, metaclass);

  PyObject *cls = kos_build_class(name, bases, ns, metaclass);

  Py_DECREF(name);
  Py_DECREF(bases);
  Py_XDECREF(metaclass);
  Py_DECREF(ns);

  if (! cls)
    return KOS_OP_POST_ERR();

  SAYF("BUILD_CLASS: cls=%R", cls);

  int res = KosEvalState_StackPushRef(es, cls);
  Py_DECREF(cls);

  return kos_op_outcome_from_int_step(res);
}


// The end.
