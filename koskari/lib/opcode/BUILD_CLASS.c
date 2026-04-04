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
   Pops ns, pos; materializes ns to dict; calls metaclass(name, bases,
   cls_dict); pushes resulting class.
*/

KOS_OP_HANDLER(BUILD_CLASS) {
  (void) out_ev;
  (void) insn;

  PyObject *ns = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "BUILD_CLASS stack underrun (ns)");
    return KOS_OP_POST_ERR();
  }

  SAYF("BUILD_CLASS: ns=%R", ns);

  PyObject *pos = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
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

  SAYF("BUILD_CLASS: name=%R, bases=%R, metaclass=%R", name, bases, metaclass);

  if (! metaclass) {
    metaclass = kos_get_default_metaclass();
    if (! metaclass) {
      Py_DECREF(ns);
      return KOS_OP_POST_ERR();
    }
  } else {
    Py_INCREF(metaclass);
  }

  PyObject *cls_dict = PyDict_New();
  if (! cls_dict) {
    Py_DECREF(metaclass);
    Py_DECREF(ns);
    return KOS_OP_POST_ERR();
  }

  if (PyDict_Merge(cls_dict, ns, 1) < 0) {
    Py_DECREF(cls_dict);
    Py_DECREF(metaclass);
    Py_DECREF(ns);
    return KOS_OP_POST_ERR();
  }
  Py_DECREF(ns);

  SAYF("BUILD_CLASS: cls_dict=%R", cls_dict);

  PyObject *args = PyTuple_Pack(3, name, bases, cls_dict);
  Py_DECREF(cls_dict);
  if (! args) {
    Py_DECREF(metaclass);
    return KOS_OP_POST_ERR();
  }

  SAYF("BUILD_CLASS: metaclass args=%R", args);

  PyObject *cls = PyObject_Call(metaclass, args, NULL);
  Py_DECREF(metaclass);
  Py_DECREF(args);
  if (! cls)
    return KOS_OP_POST_ERR();

  SAYF("BUILD_CLASS: cls=%R", cls);

  int res = KosStackBlock_PushRef(&CURRENT_STACK(ctx), cls);
  Py_DECREF(cls);

  return kos_op_outcome_from_int_step(res);
}


// The end.
