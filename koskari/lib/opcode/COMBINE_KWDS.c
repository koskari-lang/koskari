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
  unsigned opa = KosOp_ARG(insn);

  PyObject *built = PyDict_New();
  if (! built)
    return KOS_OP_POST_ERR();

  if (opa == 0) {
    if (KosEvalState_StackPushRef(es, built) < 0) {
      Py_DECREF(built);
      return KOS_OP_POST_ERR();
    }
    Py_DECREF(built);
    (void) out_ev;
    return KOS_OP_RUN();
  }

  PyObject *const *vals = KosEvalState_StackTailPtr(es,
                                                 (size_t) opa);
  if (! vals) {
    Py_DECREF(built);
    return KOS_OP_POST_ERR();
  }

  for (unsigned i = 0; i < opa; i++) {
    if (PyDict_Merge(built, vals[i], 1) != 0) {
      Py_DECREF(built);
      KosEvalState_StackDropNoErr(es, (size_t) opa);
      return KOS_OP_POST_ERR();
    }
  }

  KosEvalState_StackDropNoErr(es, (size_t) opa);

  if (KosEvalState_StackPushRef(es, built) < 0) {
    Py_DECREF(built);
    return KOS_OP_POST_ERR();
  }
  Py_DECREF(built);

  (void) out_ev;
  return KOS_OP_RUN();
}
