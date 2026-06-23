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
   PAIR_SPLICE: arg = count. Stack (bottom to top) = seg_0 ... seg_{count-1}.
   Each segment is a pair (proper list), a Python tuple (of elements), nil,
   or any other iterable. Flatten all segments in order into a single proper
   cons list and push the result.

   nil segments are skipped. The result is always a proper list (ends in nil).
   PAIR_SPLICE 0 pushes nil.

   Used by compile/quasiquote to concatenate literal segments and
   unquote-splicing results into the final list.
*/

/**
   Extend a PyList with the contents of a single segment item.
   Handles pair (via KosPair_Unpack), tuple (fast path), nil (skip),
   and generic iterable. Returns 0 on success, -1 on error.
*/
static int
pair_splice_extend(PyObject *coll, PyObject *item) {
  if (KosNil_Check(item)) {
    /* nil contributes nothing */
    return 0;
  }

  if (KosPair_CheckExact(item)) {
    /* pair: unpack into an iterator, then extend */
    PyObject *it = KosPair_Unpack(item);
    if (! it)
      return -1;
    PyObject *tmp = _PyList_Extend((PyListObject *) coll, it);
    Py_DECREF(it);
    if (! tmp)
      return -1;
    Py_DECREF(tmp);
    return 0;
  }

  if (PyTuple_CheckExact(item)) {
    /* tuple: fast path — avoid iterator overhead */
    Py_ssize_t n = PyTuple_GET_SIZE(item);
    for (Py_ssize_t i = 0; i < n; i++) {
      if (PyList_Append(coll, PyTuple_GET_ITEM(item, i)) < 0)
        return -1;
    }
    return 0;
  }

  /* generic iterable */
  PyObject *it = PyObject_GetIter(item);
  if (! it)
    return -1;
  PyObject *tmp = _PyList_Extend((PyListObject *) coll, it);
  Py_DECREF(it);
  if (! tmp)
    return -1;
  Py_DECREF(tmp);
  return 0;
}


KOS_OP_HANDLER(PAIR_SPLICE) {
  size_t count = (size_t) KosOp_ARG(insn);

  if (count == 0) {
    Py_INCREF(KosNil);
    return kos_op_result_PUSH(es, KosNil);
  }

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count);
  if (! ptr)
    return EVAL_RAISE();

  PyObject *coll = PyList_New(0);
  if (! coll)
    return EVAL_RAISE();

  for (size_t i = 0; i < count; i++) {
    if (pair_splice_extend(coll, ptr[i]) < 0) {
      Py_DECREF(coll);
      return EVAL_RAISE();
    }
  }

  PyObject *result;
  if (PyList_GET_SIZE(coll) == 0) {
    /* All segments were empty or nil — result is nil. */
    Py_DECREF(coll);
    Py_INCREF(KosNil);
    result = KosNil;
  } else {
    /* Append nil to make the result a proper list. */
    if (PyList_Append(coll, KosNil) < 0) {
      Py_DECREF(coll);
      return EVAL_RAISE();
    }
    result = KosPair_Cons(coll);
    Py_DECREF(coll);
    if (! result)
      return EVAL_RAISE();
  }

  if (KosEvalState_StackDrop(es, count) < 0) {
    Py_DECREF(result);
    return EVAL_RAISE();
  }

  return kos_op_result_PUSH(es, result);
}


// The end.
