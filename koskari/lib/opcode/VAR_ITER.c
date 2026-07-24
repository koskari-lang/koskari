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
   VAR_ITER: fused VAR + OBJECT_ITER. Arg = constant index of a name.
   Resolve the name in the current frame's namespace and push its
   iterator, acquired uniformly over both protocols (kos_iter_acquire).
*/

KOS_OP_HANDLER(VAR_ITER) {
  PyObject *name = get_name_from_insn(es, insn);
  if (! name)
    return EVAL_RAISE();

  PyObject *val = frame_ns_deref(es, name);
  if (! val)
    return EVAL_RAISE();

  PyObject *res = kos_iter_acquire(val);
  Py_DECREF(val);
  if (! res)
    return EVAL_RAISE();
  return kos_op_result_PUSH(es, res);
}


// The end.
