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
   IMPORT_STAR: arg = extra index. Schema CONST (module name str, or
   None for a bare relative import), COUNT (level: leading dots, 0 =
   absolute).

   Self-contained: imports the leaf module via kos_import_module and
   binds its public names into the current frame namespace, matching
   Python's "from MODULE import *". No stack effect.

   Name selection follows CPython's import_all_from: when the module
   defines __all__ (a sequence of strings; a non-string entry is a
   TypeError) exactly those names are bound; otherwise every name from
   dir(module) that does not start with an underscore. Existing
   bindings are overwritten.
*/


/**
   Compute the export name list for a wildcard import: __all__ when
   present, else underscore-filtered dir(). New reference to a list of
   names, or NULL with error set.
*/
static PyObject *kos_import_star_names(PyObject *mod) {

  PyObject *all = PyObject_GetAttrString(mod, "__all__");
  if (all)
    return all;
  if (! PyErr_ExceptionMatches(PyExc_AttributeError))
    return NULL;
  PyErr_Clear();

  PyObject *dir_names = PyObject_Dir(mod);
  if (! dir_names)
    return NULL;

  PyObject *out = PyList_New(0);
  if (! out) {
    Py_DECREF(dir_names);
    return NULL;
  }

  Py_ssize_t n = PyList_GET_SIZE(dir_names);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *name = PyList_GET_ITEM(dir_names, i);
    if (PyUnicode_Check(name) &&
        PyUnicode_GET_LENGTH(name) > 0 &&
        PyUnicode_READ_CHAR(name, 0) == '_')
      continue;
    if (PyList_Append(out, name) < 0) {
      Py_DECREF(dir_names);
      Py_DECREF(out);
      return NULL;
    }
  }
  Py_DECREF(dir_names);

  return out;
}


KOS_OP_HANDLER(IMPORT_STAR) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *name = KosExtra_OBJ(row, 0);

  PyObject *mod = kos_import_module(ctx, name, (long) KosExtra_IMM(row, 1), 1);
  if (! mod)
    RESULT_RAISE();

  PyObject *names = kos_import_star_names(mod);
  if (! names) {
    Py_DECREF(mod);
    RESULT_RAISE();
  }

  PyObject *fast = PySequence_Fast(names,
                                   "__all__ must be a sequence of names");
  Py_DECREF(names);
  if (! fast) {
    Py_DECREF(mod);
    RESULT_RAISE();
  }

  Py_ssize_t n = PySequence_Fast_GET_SIZE(fast);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *bind = PySequence_Fast_GET_ITEM(fast, i);

    if (! PyUnicode_Check(bind)) {
      PyErr_Format(PyExc_TypeError, "wildcard import name must be str,"
                   " not %R", bind);
      goto fail;
    }

    PyObject *val = PyObject_GetAttr(mod, bind);
    if (! val)
      goto fail;

    if (frame_ns_store(ctx, bind, val) < 0) {
      Py_DECREF(val);
      goto fail;
    }
    Py_DECREF(val);
  }

  Py_DECREF(fast);
  Py_DECREF(mod);
  RESULT_NEXT(ctx);

 fail:
  Py_DECREF(fast);
  Py_DECREF(mod);
  RESULT_RAISE();
}


// The end.
