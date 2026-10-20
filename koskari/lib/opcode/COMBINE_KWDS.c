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
  COMBINE_KWDS: arg = count of dicts to merge.
  Stack: [..., d_0, ..., d_{m-1}] -> [..., dict].
  Merge the m dicts (later overwrite earlier on key clash) and push result.
*/
KOS_OP_HANDLER(COMBINE_KWDS) {
  KOS_SYNC_PC(ctx, insn);
  unsigned opa = KosOp_ARG(insn);

  PyObject *built = PyDict_New();
  if (unlikely(! built))
    RESULT_RAISE();

  if (opa == 0) {
    RESULT_STEAL(ctx, built);
  }

  PyObject *const *vals = KosEvalState_StackTailPtr(ctx, (size_t) opa);
  if (unlikely(! vals)) {
    Py_DECREF(built);
    RESULT_RAISE();
  }

  for (unsigned i = 0; i < opa; i++) {
    if (PyDict_Merge(built, vals[i], 1) != 0) {
      if (PyErr_ExceptionMatches(PyExc_TypeError)
          || PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Format(PyExc_TypeError,
                     "argument after **: must be a mapping, not %.100s",
                     Py_TYPE(vals[i])->tp_name);
      }
      Py_DECREF(built);
      KosEvalState_StackDropNoErr(ctx, (size_t) opa);
      RESULT_RAISE();
    }
  }

  KosEvalState_StackDropNoErr(ctx, (size_t) opa);

  RESULT_STEAL(ctx, built);
}


// The end.
