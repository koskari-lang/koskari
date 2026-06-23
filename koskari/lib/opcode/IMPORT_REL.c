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
   IMPORT_REL: arg = constant index.  Perform a relative module import
   and push the leaf module onto the stack.

   The constant at arg must be a 2-tuple (level:int, name:str|None).
   level is the number of leading dots in the original import expression
   (e.g. 1 for "(import .foo)", 2 for "(import ..foo)").  name is the
   module name after the dots, or None for a bare relative import
   (e.g. "(import .)" which imports the enclosing package itself).

   The outermost frame namespace supplies __name__; a temporary dict
   holding that key is passed to PyImport_ImportModuleLevelObject (which
   requires dict or NULL, not KosNamespace) so Python can resolve the anchor
   package for relative imports.

   Like IMPORT, the leaf module is pushed (not the root package) by
   passing _import_fromlist as the fromlist sentinel.

   _import_fromlist is initialised once in koskari_eval_init.
*/

KOS_OP_HANDLER(IMPORT_REL) {
  /* Constant is a (level:int, name:str|None) tuple */
  PyObject *tup = get_name_from_insn(KosEvalState_CurrentCode(es), insn);
  if (! tup)
    return EVAL_RAISE();

  if (! PyTuple_Check(tup) || PyTuple_GET_SIZE(tup) != 2) {
    PyErr_SetString(PyExc_TypeError,
                    "IMPORT_REL constant must be a (level, name) tuple");
    return EVAL_RAISE();
  }

  PyObject *level_obj = PyTuple_GET_ITEM(tup, 0);
  PyObject *name      = PyTuple_GET_ITEM(tup, 1);

  long level = PyLong_AsLong(level_obj);
  if (level < 0 && PyErr_Occurred())
    return EVAL_RAISE();

  /* A bare relative import "(import .)" has name == None */
  if (name == Py_None)
    name = NULL;

  /* Globals namespace supplies __name__ and __package__ for relative import
     anchor resolution (same as GLOBALS opcode). */
  PyObject *globals_ns = KosEvalState_GlobalsNamespace(es);


  PyObject *name_val = KosNamespace_Deref(globals_ns, str_name);
  if (! name_val) {
    return EVAL_RAISE();
  }
  PyObject *package_val = KosNamespace_Deref(globals_ns, str_package);
  if (! package_val) {
    Py_DECREF(name_val);
    return EVAL_RAISE();
  }

  PyObject *globals_dict = PyDict_New();
  if (! globals_dict) {
    Py_DECREF(name_val);
    Py_DECREF(package_val);
    return EVAL_RAISE();
  }

  if (PyDict_SetItem(globals_dict, str_name, name_val) ||
      PyDict_SetItem(globals_dict, str_package, package_val)) {
    Py_DECREF(name_val);
    Py_DECREF(package_val);
    Py_DECREF(globals_dict);
    return EVAL_RAISE();
  }
  Py_DECREF(name_val);
  Py_DECREF(package_val);

  PyObject *mod = PyImport_ImportModuleLevelObject(
    name, globals_dict, NULL, _import_fromlist, (int) level);
  Py_DECREF(globals_dict);
  if (! mod)
    return EVAL_RAISE();

  return kos_op_result_PUSH(es, mod);
}


// The end.
