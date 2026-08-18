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
  OBJECT_ITER: pop TOS, acquire an iterator uniformly over both
  protocols (kos_iter_acquire, PUSH_FOREACH's preference order: Koskari
  AsyncGenerator passes through, am_aiter, then the sync protocol),
  push result.
*/
KOS_OP_HANDLER(OBJECT_ITER) {
  (void) insn;

  PyObject *val = KosEvalState_StackPopRef(es);
  if (unlikely(! val))
    return EVAL_RAISE();

  PyObject *res = kos_iter_acquire(val);
  Py_DECREF(val);

  if (unlikely(! res))
    return EVAL_RAISE();

  return kos_op_result_PUSH(es, res);
}


// The end.
