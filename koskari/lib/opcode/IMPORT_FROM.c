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
  True when the current exception is a ModuleNotFoundError whose name
  attribute equals full — i.e. the module we synthesized for the
  submodule fallback is itself what was not found, rather than some
  deeper import failing during its execution. Preserves the pending
  exception.
*/
static int kos_pending_missing_module_is(PyObject *full) {

  if (! PyErr_ExceptionMatches(PyExc_ModuleNotFoundError))
    return 0;

  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_Fetch(&typ, &val, &tb);
  PyErr_NormalizeException(&typ, &val, &tb);

  int match = 0;
  PyObject *name = val ? PyObject_GetAttrString(val, "name") : NULL;
  if (name) {
    match = PyObject_RichCompareBool(name, full, Py_EQ) == 1;
    Py_DECREF(name);
  }
  PyErr_Clear();

  PyErr_Restore(typ, val, tb);
  return match;
}


/**
  Fetch one from-import member from the module, importing
  "{mod.__name__}.{attr}" and retrying on AttributeError. New
  reference or NULL with error set.
*/
static PyObject *kos_import_from_member(PyObject *mod, PyObject *attr) {

  PyObject *val = PyObject_GetAttr(mod, attr);
  if (val || ! PyErr_ExceptionMatches(PyExc_AttributeError))
    return val;

  PyErr_Clear();

  PyObject *mod_name = PyObject_GetAttrString(mod, "__name__");
  if (! mod_name)
    return NULL;
  if (! PyUnicode_Check(mod_name)) {
    Py_DECREF(mod_name);
    PyErr_Format(PyExc_ImportError, "cannot import %R", attr);
    return NULL;
  }

  PyObject *full = PyUnicode_FromFormat("%U.%U", mod_name, attr);
  if (! full) {
    Py_DECREF(mod_name);
    return NULL;
  }

  PyObject *sub = PyImport_ImportModuleLevelObject(
    full, NULL, NULL, _import_fromlist, 0);

  if (! sub && kos_pending_missing_module_is(full)) {
    /* No such attribute and no such submodule: the real story is the
       missing member, not a missing module. */
    PyErr_Clear();
    PyObject *path = PyModule_GetFilenameObject(mod);
    if (path) {
      PyErr_Format(PyExc_ImportError, "cannot import name %R from %R (%S)",
                   attr, mod_name, path);
      Py_DECREF(path);

    } else {
      PyErr_Clear();
      PyErr_Format(PyExc_ImportError,
                   "cannot import name %R from %R (unknown location)",
                   attr, mod_name);
    }
  }

  Py_DECREF(full);
  Py_DECREF(mod_name);

  /* the import wires the submodule up as an attribute of the parent,
     but the imported leaf is exactly the value we want */
  return sub;
}


/**
  IMPORT_FROM: arg = extra index. Schema CONST (module name str, or
  None for a bare relative import), COUNT (level: leading dots, 0 =
  absolute), CONST (tuple of attribute names), CONST (tuple of binding
  names, parallel to the attribute names).

  Self-contained: imports the leaf module via kos_import_module, then
  for each attribute name gets the attribute and stores it into the
  current frame namespace under the corresponding binding name. No
  stack effect.

  Submodule fallback: when getattr raises AttributeError and the
  member is actually a not-yet-imported submodule, import
  "{module.__name__}.{attr}" and retry the getattr — matching
  CPython's "from pkg import sub" behavior for submodules the package
  __init__ does not import. When the fallback fails because the
  synthesized submodule itself does not exist, the raw
  ModuleNotFoundError would misreport the situation (the user asked
  for a member, not a module), so it is replaced with CPython's
  from-import report: cannot import name X from Y (path). Other
  exceptions propagate.
*/
KOS_OP_HANDLER_COLD(IMPORT_FROM) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *name = KosExtra_OBJ(row, 0);
  if (unlikely(! name))
    RESULT_RAISE();

  PyObject *attrs = KosExtra_OBJ(row, 2);
  if (unlikely(! attrs))
    RESULT_RAISE();

  PyObject *binds = KosExtra_OBJ(row, 3);
  if (unlikely(! binds))
    RESULT_RAISE();

  if (unlikely(! PyTuple_CheckExact(attrs) || ! PyTuple_CheckExact(binds) ||
      PyTuple_GET_SIZE(attrs) != PyTuple_GET_SIZE(binds))) {
    PyErr_SetString(PyExc_TypeError, "IMPORT_FROM names must be parallel"
                    " tuples");
    RESULT_RAISE();
  }

  PyObject *mod = kos_import_module(es, name, (long) KosExtra_IMM(row, 1), 1);
  if (unlikely(! mod))
    RESULT_RAISE();

  Py_ssize_t n = PyTuple_GET_SIZE(attrs);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *val = kos_import_from_member(mod, PyTuple_GET_ITEM(attrs, i));
    if (unlikely(! val)) {
      Py_DECREF(mod);
      RESULT_RAISE();
    }

    if (unlikely(frame_ns_store(es, PyTuple_GET_ITEM(binds, i), val) < 0)) {
      Py_DECREF(val);
      Py_DECREF(mod);
      RESULT_RAISE();
    }
    Py_DECREF(val);
  }
  Py_DECREF(mod);

  RESULT_NEXT(es);
}


// The end.
