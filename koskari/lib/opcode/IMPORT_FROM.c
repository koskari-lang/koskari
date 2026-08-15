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
   IMPORT_FROM: arg = extra index. Schema CONST (module name str, or
   None for a bare relative import), COUNT (level: leading dots, 0 =
   absolute), CONST (tuple of attribute names), CONST (tuple of binding
   names, parallel to the attribute names).

   Self-contained: imports the leaf module via kos_import_module, then
   for each attribute name gets the attribute and stores it into the
   current frame namespace under the corresponding binding name. No
   stack effect.

   Transformer probe: a member defined as a syntax transformer is bound
   in its module under the compile/ prefix, so an unprefixed member that
   misses is retried as "compile/{attr}" before the submodule fallback.
   A member written with a namespace prefix is taken literally. See
   design/language/prefix-aware-imports.md; full family import is the
   follow-up there.

   Submodule fallback: when getattr raises AttributeError and the
   member is actually a not-yet-imported submodule, import
   "{module.__name__}.{attr}" and retry the getattr — matching
   CPython's "from pkg import sub" behavior for submodules the package
   __init__ does not import. Other exceptions propagate.
*/


/**
   Fetch one from-import member from the module, retrying an unprefixed
   miss as "compile/{attr}", then importing "{mod.__name__}.{attr}" on a
   remaining AttributeError. New reference or NULL with error set.
*/
static PyObject *kos_import_from_member(PyObject *mod, PyObject *attr) {

  PyObject *val = PyObject_GetAttr(mod, attr);
  if (val || ! PyErr_ExceptionMatches(PyExc_AttributeError))
    return val;

  PyErr_Clear();

  const char *rawname = PyUnicode_Check(attr)? PyUnicode_AsUTF8(attr): NULL;
  if (! rawname)
    PyErr_Clear();

  if (rawname &&
      strncmp(rawname, "compile/", 8) != 0 &&
      strncmp(rawname, "layout/", 7) != 0) {

    PyObject *pattr = PyUnicode_FromFormat("compile/%U", attr);
    if (! pattr)
      return NULL;

    val = PyObject_GetAttr(mod, pattr);
    Py_DECREF(pattr);
    if (val || ! PyErr_ExceptionMatches(PyExc_AttributeError))
      return val;

    PyErr_Clear();
  }

  PyObject *mod_name = PyObject_GetAttrString(mod, "__name__");
  if (! mod_name)
    return NULL;
  if (! PyUnicode_Check(mod_name)) {
    Py_DECREF(mod_name);
    PyErr_Format(PyExc_ImportError, "cannot import %R", attr);
    return NULL;
  }

  PyObject *full = PyUnicode_FromFormat("%U.%U", mod_name, attr);
  Py_DECREF(mod_name);
  if (! full)
    return NULL;

  PyObject *sub = PyImport_ImportModuleLevelObject(
    full, NULL, NULL, _import_fromlist, 0);
  Py_DECREF(full);
  if (! sub)
    return NULL;

  /* the import wires the submodule up as an attribute of the parent,
     but the imported leaf is exactly the value we want */
  return sub;
}


KOS_OP_HANDLER_COLD(IMPORT_FROM) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *name = KosEvalState_CONST(es, row.a0);
  if (! name)
    return EVAL_RAISE();

  PyObject *attrs = KosEvalState_CONST(es, row.a2);
  if (! attrs)
    return EVAL_RAISE();

  PyObject *binds = KosEvalState_CONST(es, row.a3);
  if (! binds)
    return EVAL_RAISE();

  if (! PyTuple_CheckExact(attrs) || ! PyTuple_CheckExact(binds) ||
      PyTuple_GET_SIZE(attrs) != PyTuple_GET_SIZE(binds)) {
    PyErr_SetString(PyExc_TypeError, "IMPORT_FROM names must be parallel"
                    " tuples");
    return EVAL_RAISE();
  }

  PyObject *mod = kos_import_module(es, name, (long) row.a1, 1);
  if (! mod)
    return EVAL_RAISE();

  Py_ssize_t n = PyTuple_GET_SIZE(attrs);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *val = kos_import_from_member(mod, PyTuple_GET_ITEM(attrs, i));
    if (! val) {
      Py_DECREF(mod);
      return EVAL_RAISE();
    }

    if (frame_ns_store(es, PyTuple_GET_ITEM(binds, i), val) < 0) {
      Py_DECREF(val);
      Py_DECREF(mod);
      return EVAL_RAISE();
    }
    Py_DECREF(val);
  }
  Py_DECREF(mod);

  return EVAL_NORMAL();
}


// The end.
