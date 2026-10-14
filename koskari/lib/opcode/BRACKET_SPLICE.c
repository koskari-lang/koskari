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
  koskari/lib/opcode/BRACKET_SPLICE.c

  The BRACKET_SPLICE opcode handler, the bracket twin of PAIR_SPLICE.
  Included after PAIR_SPLICE.c, whose pair_splice_extend it shares.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/


/**
  BRACKET_SPLICE: arg = count. Stack (bottom to top) = seg_0 ...
  seg_{count-1}. Flatten the segments in order as PAIR_SPLICE does and
  push the result with a bracket pair as its leading cell over a plain
  pair tail ending in nil. Repr is governed by the leading cell, so the
  tail is not bracketed.

  An empty result, including BRACKET_SPLICE 0, pushes bracket-nil.

  Used by compile/quasiquote for a [ ] list holding an unquote-splicing.
*/
KOS_OP_HANDLER(BRACKET_SPLICE) {
  KOS_SYNC_PC(es, insn);
  size_t count = (size_t) KosOp_ARG(insn);

  if (count == 0)
    RESULT_PUSH(es, KosBracketNil);

  PyObject *const *ptr = KosEvalState_StackTailPtr(es, count);
  if (unlikely(! ptr))
    RESULT_RAISE();

  PyObject *coll = PyList_New(0);
  if (unlikely(! coll))
    RESULT_RAISE();

  for (size_t i = 0; i < count; i++) {
    if (unlikely(pair_splice_extend(coll, ptr[i]) < 0)) {
      Py_DECREF(coll);
      RESULT_RAISE();
    }
  }

  Py_ssize_t n = PyList_GET_SIZE(coll);
  PyObject *result;

  if (n == 0) {
    /* All segments were empty or nil -- result is bracket-nil. */
    Py_DECREF(coll);
    Py_INCREF(KosBracketNil);
    result = KosBracketNil;

  } else {
    /* Append nil as the plain tail's terminator, build the tail from
       elements 1 .. n-1, then the bracket head over it. */
    if (PyList_Append(coll, KosNil) < 0) {
      Py_DECREF(coll);
      RESULT_RAISE();
    }

    PyObject **items = PySequence_Fast_ITEMS(coll);
    PyObject *tail;

    if (n == 1) {
      Py_INCREF(KosNil);
      tail = KosNil;
    } else {
      tail = KosPair_ConsFromPtr(items + 1, n);
    }

    if (! tail) {
      Py_DECREF(coll);
      RESULT_RAISE();
    }

    result = KosBracketPair_New(items[0], tail);
    Py_DECREF(tail);
    Py_DECREF(coll);
    if (! result)
      RESULT_RAISE();
  }

  if (unlikely(KosEvalState_StackDrop(es, count) < 0)) {
    Py_DECREF(result);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, result);
}


// The end.
