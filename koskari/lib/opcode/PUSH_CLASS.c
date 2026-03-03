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
   PUSH_CLASS: arg = bitflags (1 = has pos, 2 = has kwd), same as CALL_STAR.
   Stack: [pos, kwd] with pos at TOS-1, kwd at TOS. pos = (name, bases) or
   (name, bases, metaclass). kwd = dict of extra keyword args. Creates
   KOS_DYN_CLASS, installs CollectorNamespace for class body.
*/

static int push_class_ensure_tuple(PyObject **val) {
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


KOS_OP_HANDLER(PUSH_CLASS) {
  unsigned opa = KosOp_ARG(insn);

  PyObject *pos = NULL;
  PyObject *kwd = NULL;
  PyObject *name = NULL;
  PyObject *bases = NULL;
  PyObject *metaclass = NULL;
  PyObject *kwargs = NULL;
  PyObject *class_binding_ns = NULL;
  PyObject *saved_ns = NULL;
  KosDynClass *cls_node = NULL;

  KosStackBlock **stack = &CURRENT_STACK(ctx);

  if ((opa & 3) == 3) {
    kwd = KosStackBlock_PopRef(stack);
    if (! kwd)
      return -1;
    pos = KosStackBlock_PopRef(stack);
    if (! pos) {
      Py_DECREF(kwd);
      return -1;
    }
  }
  else if (opa & 2) {
    kwd = KosStackBlock_PopRef(stack);
    if (! kwd)
      return -1;
    pos = PyTuple_New(0);
    if (! pos) {
      Py_DECREF(kwd);
      return -1;
    }
  }
  else if (opa & 1) {
    pos = KosStackBlock_PopRef(stack);
    if (! pos)
      return -1;
    kwd = NULL;
  }
  else {
    PyErr_SetString(PyExc_RuntimeError,
                    "PUSH_CLASS requires at least positional args (name, bases)");
    return -1;
  }

  if (push_class_ensure_tuple(&pos) < 0) {
    Py_DECREF(pos);
    Py_XDECREF(kwd);
    return -1;
  }

  if (! PyArg_ParseTuple(pos, "OO|O:PUSH_CLASS", &name, &bases, &metaclass)) {
    Py_DECREF(pos);
    Py_XDECREF(kwd);
    return -1;
  }
  Py_INCREF(name);
  Py_INCREF(bases);
  Py_XINCREF(metaclass);
  Py_DECREF(pos);

  if (push_class_ensure_tuple(&bases) < 0) {
    Py_DECREF(name);
    Py_DECREF(bases);
    Py_XDECREF(metaclass);
    Py_XDECREF(kwd);
    return -1;
  }

  if (! metaclass && kwd) {
    metaclass = PyDict_GetItemString(kwd, "metaclass");
    Py_XINCREF(metaclass);
  }
  if (! metaclass)
    metaclass = kos_get_default_metaclass();
  if (! metaclass) {
    Py_DECREF(name);
    Py_DECREF(bases);
    Py_XDECREF(kwd);
    return -1;
  }

  if (kwd) {
    kwargs = PyDict_Copy(kwd);
    if (! kwargs) {
      Py_DECREF(metaclass);
      Py_DECREF(name);
      Py_DECREF(bases);
      Py_DECREF(kwd);
      return -1;
    }
    if (PyDict_DelItemString(kwargs, "metaclass") < 0)
      PyErr_Clear();
  }
  Py_XDECREF(kwd);

  saved_ns = CURRENT_NS(ctx);
  Py_INCREF(saved_ns);

  {
    PyObject *class_slot = PyTuple_Pack(1, str_class);
    if (! class_slot)
      goto push_class_fail;
    class_binding_ns = KosNamespace_NewFull(
        name,
        KosNamespace_Check(saved_ns) ? saved_ns : NULL,
        class_slot,
        NULL);
    Py_DECREF(class_slot);
    if (! class_binding_ns)
      goto push_class_fail;
  }

  cls_node = kos_dynclass_new(
      KosContext_DYN_TOP(ctx),
      metaclass,
      name,
      bases,
      kwargs,
      class_binding_ns,
      saved_ns);
  if (! cls_node) {
    Py_DECREF(class_binding_ns);
    return -1;
  }

  /* dynclass_new steals references, and if there's an error
     it will decref on its own. */
  // Py_DECREF(metaclass);
  // Py_DECREF(name);
  // Py_DECREF(bases);
  // Py_XDECREF(kwargs);
  // Py_DECREF(class_binding_ns);
  // Py_DECREF(saved_ns);

  if (kos_dyn_push(ctx, (KosDynNode *) cls_node) < 0) {
    kos_dynnode_free((KosDynNode *) cls_node);
    return -1;
  }

  (void) out_ev;
  return 0;

 push_class_fail:
  Py_DECREF(metaclass);
  Py_DECREF(name);
  Py_DECREF(bases);
  Py_XDECREF(kwargs);
  Py_DECREF(saved_ns);
  return -1;
}
