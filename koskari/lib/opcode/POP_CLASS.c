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
   POP_CLASS: arg unused. Verify dyn_top is KOS_DYN_CLASS, materialize
   collector to dict, call metaclass(name, bases, cls_dict, **kwargs),
   store cls in class_binding_ns.__class__, restore frame ns, pop node,
   push cls.
*/


KOS_OP_HANDLER(POP_CLASS) {
  KosDynNode *top = KosContext_DYN_TOP(ctx);
  if (! top || ! KosDynNode_IS_CLASS(top)) {
    PyErr_SetString(PyExc_RuntimeError, "POP_CLASS: top is not a class scope");
    return -1;
  }

  KosDynClass *cls_node = (KosDynClass *) top;
  PyObject *collector = CURRENT_NS(ctx);
  PyObject *cls_dict = NULL;
  PyObject *cls = NULL;
  PyObject *args = NULL;
  int r;

  cls_dict = PyDict_New();
  if (! cls_dict)
    return -1;
  if (PyDict_Update(cls_dict, collector) < 0) {
    Py_DECREF(cls_dict);
    return -1;
  }

  args = PyTuple_Pack(3,
      KosDynClass_NAME(cls_node),
      KosDynClass_BASES(cls_node),
      cls_dict);
  Py_DECREF(cls_dict);
  if (! args)
    return -1;

  cls = PyObject_Call(
      KosDynClass_METACLASS(cls_node),
      args,
      KosDynClass_KWARGS(cls_node));
  Py_DECREF(args);
  if (! cls)
    return -1;

  r = KosNamespace_Store(KosDynClass_BINDING_NS(cls_node), str_class, cls);
  if (r < 0) {
    Py_DECREF(cls);
    return -1;
  }

  Py_DECREF(CURRENT_NS(ctx));
  CURRENT_NS(ctx) = KosDynClass_SAVED_NS(cls_node);
  KosDynClass_SAVED_NS(cls_node) = NULL;

  r = kos_dyn_uninstall(ctx, top, out_ev);
  if (r < 0) {
    Py_DECREF(cls);
    return -1;
  }

  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), cls) < 0) {
    Py_DECREF(cls);
    return -1;
  }
  Py_DECREF(cls);

  (void) insn;
  return 0;
}
