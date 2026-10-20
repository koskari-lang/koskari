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
  Extend a PyList with the contents of a single segment item.
  Handles pair (via KosPair_Unpack), tuple (fast path), nil (skip),
  and generic iterable. Returns 0 on success, -1 on error.
*/
static int pair_splice_extend(PyObject *coll, PyObject *item) {
  if (KosNilP(item)) {
    /* nil contributes nothing */
    return 0;
  }

  if (KosPair_Check(item)) {
    /* pair (or bracket-pair): unpack into an iterator, then extend */
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


/**
  The operand half shared by PAIR_SPLICE and BRACKET_SPLICE. Row field 0
  is the count of stacked values; field 1 is 0, or 1 when the top value
  of that count is a tail rather than a segment. Pop the tail when field
  1 is set, flatten the segments below it in order into a new list, and
  drop them. On success *tail is a new reference to the popped tail, or
  to nil when field 1 is 0. Returns NULL with an exception set and
  nothing held on error.
*/
static PyObject *pair_splice_collect(
    KosContext *ctx,
    const KosExtra *row,
    const char *opname,
    PyObject **tail) {

  size_t count = (size_t) KosExtra_IMM(row, 0);
  size_t has_tail = (size_t) KosExtra_IMM(row, 1);

  if (unlikely(has_tail > 1 || has_tail > count)) {
    PyErr_Format(PyExc_ValueError,
                 "%s tail field must be 0 or 1 and within count %zu,"
                 " got %zu", opname, count, has_tail);
    return NULL;
  }

  PyObject *end;
  if (has_tail) {
    /* the tail is the top stacked value; the segments sit below it */
    end = KosEvalState_StackPopRef(ctx);
    if (unlikely(! end))
      return NULL;
    count--;

  } else {
    Py_INCREF(KosNil);
    end = KosNil;
  }

  PyObject *coll = PyList_New(0);
  if (unlikely(! coll))
    goto failure;

  if (count) {
    PyObject *const *ptr = KosEvalState_StackTailPtr(ctx, count);
    if (unlikely(! ptr))
      goto failure;

    for (size_t i = 0; i < count; i++) {
      if (unlikely(pair_splice_extend(coll, ptr[i]) < 0))
        goto failure;
    }

    /* coll holds its own references to every flattened element */
    if (unlikely(KosEvalState_StackDrop(ctx, count) < 0))
      goto failure;
  }

  *tail = end;
  return coll;

 failure:
  Py_XDECREF(coll);
  Py_DECREF(end);
  return NULL;
}


/**
  PAIR_SPLICE: row = (count, tail flag). Stack (bottom to top) = seg_0
  ... seg_{count-1}. Each segment is a pair (proper list), a Python
  tuple (of elements), nil, or any other iterable. Flatten the segments
  in order into one cons list and push the result; nil segments are
  skipped.

  Tail flag 0: the list ends in nil, and an empty result is nil.
  PAIR_SPLICE (0, 0) pushes nil.

  Tail flag 1: seg_{count-1} is the tail, not a segment. The last
  cell's cdr is the tail, and an empty result is the tail itself, so
  `(a ,@xs . b) builds what (cons a (append xs b)) does.

  Used by compile/quasiquote to concatenate literal segments and
  unquote-splicing results into the final list.
*/
KOS_OP_HANDLER(PAIR_SPLICE) {
  KOS_SYNC_PC(ctx, insn);

  PyObject *tail;
  PyObject *coll = pair_splice_collect(
      ctx, KosOp_ROW(insn), "PAIR_SPLICE", &tail);
  if (unlikely(! coll))
    RESULT_RAISE();

  if (PyList_GET_SIZE(coll) == 0) {
    /* every segment was empty or nil: the result is the tail */
    Py_DECREF(coll);
    RESULT_STEAL(ctx, tail);
  }

  /* the tail is the last cell's cdr */
  int rc = PyList_Append(coll, tail);
  Py_DECREF(tail);
  if (unlikely(rc < 0)) {
    Py_DECREF(coll);
    RESULT_RAISE();
  }

  PyObject *result = KosPair_Cons(coll);
  Py_DECREF(coll);
  RESULT_STEAL(ctx, result);
}


// The end.
