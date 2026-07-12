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
   BUILD_FUNCTION: arg = extra index. Schema CONST (keyword-defaults
   names tuple), CONST (annotation names tuple); either may be the
   empty tuple.

   Stack: ... [ ann_1 ... ann_M, ] [ def_1 ... def_N, ] fspec. Pop
   fspec, then if the defaults names tuple is non-empty pop N values
   into a defaults dict keyed by it, then if the annotation names tuple
   is non-empty pop M more values into an annotations dict keyed by it.
   Build the function via KosFunction_New, or
   KosFunction_NewWithAnnotations when annotations are present, and
   push the result.
*/

KOS_OP_HANDLER(BUILD_FUNCTION) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *defnames = KosEvalState_CONST(es, row.a0);
  if (! defnames)
    return EVAL_RAISE();

  PyObject *annnames = KosEvalState_CONST(es, row.a1);
  if (! annnames)
    return EVAL_RAISE();

  if (! PyTuple_CheckExact(defnames) || ! PyTuple_CheckExact(annnames)) {
    PyErr_Format(PyExc_TypeError, "BUILD_FUNCTION names must be tuples,"
                 " not %R and %R", defnames, annnames);
    return EVAL_RAISE();
  }

  PyObject *fspec = KosEvalState_StackPopRef(es);
  if (! fspec)
    return EVAL_RAISE();

  PyObject *defaults = NULL;
  if (PyTuple_GET_SIZE(defnames) > 0) {
    defaults = KosEvalState_StackPopTailDictFromNames(es, defnames);
    if (! defaults) {
      Py_DECREF(fspec);
      return EVAL_RAISE();
    }
  }

  PyObject *annotations = NULL;
  if (PyTuple_GET_SIZE(annnames) > 0) {
    annotations = KosEvalState_StackPopTailDictFromNames(es, annnames);
    if (! annotations) {
      Py_DECREF(fspec);
      Py_XDECREF(defaults);
      return EVAL_RAISE();
    }
  }

  PyObject *parent_ns = CURRENT_NS();
  PyObject *fn;
  if (annotations) {
    fn = KosFunction_NewWithAnnotations(fspec, defaults, parent_ns,
                                        annotations);
  } else {
    fn = KosFunction_New(fspec, defaults, parent_ns);
  }
  Py_DECREF(fspec);
  Py_XDECREF(defaults);
  Py_XDECREF(annotations);
  if (! fn)
    return EVAL_RAISE();

  if (KosEvalState_StackPushRef(es, fn) < 0) {
    Py_DECREF(fn);
    return EVAL_RAISE();
  }
  Py_DECREF(fn);

  return EVAL_NORMAL();
}


// The end.
