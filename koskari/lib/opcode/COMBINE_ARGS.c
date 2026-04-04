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
   COMBINE_ARGS: arg = count of sequences to combine.
   Stack: [..., t_0, ..., t_{m-1}] -> [..., tuple].
   Concatenate the m sequences (each via iteration) into a single tuple.
   Non-tuple values are converted with PySequence_Tuple when m == 1.
*/

KOS_OP_HANDLER(COMBINE_ARGS) {
  unsigned opa = KosOp_ARG(insn);

  if (opa == 0) {
    PyObject *empty = PyTuple_New(0);
    if (! empty)
      return KOS_OP_POST_ERR();
    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), empty) < 0) {
      Py_DECREF(empty);
      return KOS_OP_POST_ERR();
    }
    Py_DECREF(empty);
    (void) out_ev;
    return KOS_OP_RUN();
  }

  if (opa == 1) {
    PyObject *v = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! v)
      return KOS_OP_POST_ERR();

    if (! PyTuple_CheckExact(v)) {
      PyObject *s = PySequence_Tuple(v);
      Py_DECREF(v);
      v = s;
    }

    if (! v)
      return KOS_OP_POST_ERR();

    if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), v) < 0) {
      Py_DECREF(v);
      return KOS_OP_POST_ERR();
    }
    Py_DECREF(v);
    (void) out_ev;
    return KOS_OP_RUN();
  }

  PyObject *const *vals = KosStackBlock_TailPtr(CURRENT_STACK(ctx),
                                                 (size_t) opa);
  if (! vals)
    return KOS_OP_POST_ERR();

  PyObject *built = PyList_New(0);
  if (! built)
    return KOS_OP_POST_ERR();

  for (unsigned i = 0; i < opa; i++) {
    PyObject *nb = PySequence_InPlaceConcat(built, vals[i]);
    Py_DECREF(built);
    if (! nb) {
      KosStackBlock_DropNoErr(&CURRENT_STACK(ctx), (size_t) opa);
      return KOS_OP_POST_ERR();
    }
    built = nb;
  }

  KosStackBlock_DropNoErr(&CURRENT_STACK(ctx), (size_t) opa);

  PyObject *result = PySequence_Tuple(built);
  Py_DECREF(built);
  if (! result)
    return KOS_OP_POST_ERR();

  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), result) < 0) {
    Py_DECREF(result);
    return KOS_OP_POST_ERR();
  }
  Py_DECREF(result);

  (void) out_ev;
  return KOS_OP_RUN();
}
