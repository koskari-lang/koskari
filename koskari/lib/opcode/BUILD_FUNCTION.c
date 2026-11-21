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
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *defnames = KosExtra_OBJ(row, 0);

  PyObject *annnames = KosExtra_OBJ(row, 1);

  if (! PyTuple_CheckExact(defnames) || ! PyTuple_CheckExact(annnames)) {
    PyErr_Format(PyExc_TypeError, "BUILD_FUNCTION names must be tuples,"
                 " not %R and %R", defnames, annnames);
    RESULT_RAISE();
  }

  PyObject *fspec = KosContext_StackPopRef(ctx);
  if (unlikely(! fspec))
    RESULT_RAISE();

  PyObject *defaults = NULL;
  if (PyTuple_GET_SIZE(defnames) > 0) {
    defaults = KosContext_StackPopTailDictFromNames(ctx, defnames);
    if (unlikely(! defaults)) {
      Py_DECREF(fspec);
      RESULT_RAISE();
    }
  }

  PyObject *annotations = NULL;
  if (PyTuple_GET_SIZE(annnames) > 0) {
    annotations = KosContext_StackPopTailDictFromNames(ctx, annnames);
    if (unlikely(! annotations)) {
      Py_DECREF(fspec);
      Py_XDECREF(defaults);
      RESULT_RAISE();
    }
  }

  PyObject *fn = kos_eval_build_function(fspec, defaults, annotations,
                                         CURRENT_NS());
  Py_DECREF(fspec);
  Py_XDECREF(defaults);
  Py_XDECREF(annotations);
  if (unlikely(! fn))
    RESULT_RAISE();

  RESULT_STEAL(ctx, fn);
}


// The end.
