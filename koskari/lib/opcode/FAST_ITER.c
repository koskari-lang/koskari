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
  FAST_ITER: fused FAST + OBJECT_ITER. Arg = slot index. Push the fast
  local's iterator, acquired uniformly over both protocols
  (kos_iter_acquire).
*/
KOS_OP_HANDLER(FAST_ITER) {
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  size_t idx = (size_t) KosOp_ARG(insn);
  PyObject *val = kos_ns_fast_deref(ns, idx);
  if (unlikely(! val))
    return EVAL_RAISE();

  PyObject *res = kos_iter_acquire(val);
  Py_DECREF(val);
  if (unlikely(! res))
    return EVAL_RAISE();

  return kos_op_result_PUSH(es, res);
}


// The end.
