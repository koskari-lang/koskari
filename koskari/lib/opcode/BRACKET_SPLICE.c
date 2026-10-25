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
  Included after PAIR_SPLICE.c, whose pair_splice_collect it shares.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/


/**
  BRACKET_SPLICE: row = (count, tail flag). Stack (bottom to top) =
  seg_0 ... seg_{count-1}. Flatten the segments in order as PAIR_SPLICE
  does and push the result with a bracket pair as its leading cell over
  plain pair cells. Repr is governed by the leading cell, so the rest is
  not bracketed.

  Tail flag 0: the last cell's cdr is nil, and an empty result,
  including BRACKET_SPLICE (0, 0), is bracket-nil.

  Tail flag 1: seg_{count-1} is the tail, not a segment. The last
  cell's cdr is the tail, and an empty result is the tail itself.

  Used by compile/quasiquote for a [ ] list holding an unquote-splicing.
*/
KOS_OP_HANDLER(BRACKET_SPLICE) {
  KOS_SYNC_PC(ctx, insn);

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *tail;
  PyObject *coll = pair_splice_collect(ctx, row, "BRACKET_SPLICE", &tail);
  if (unlikely(! coll))
    RESULT_RAISE();

  Py_ssize_t n = PyList_GET_SIZE(coll);

  if (n == 0) {
    /* every segment was empty or nil: bracket-nil, or the stacked tail */
    Py_DECREF(coll);
    if (KosExtra_IMM(row, 1))
      RESULT_STEAL(ctx, tail);

    Py_DECREF(tail);
    RESULT_PUSH(ctx, KosBracketNil);
  }

  /* the tail terminates the plain cells after the bracket head */
  int rc = PyList_Append(coll, tail);
  Py_DECREF(tail);
  if (unlikely(rc < 0)) {
    Py_DECREF(coll);
    RESULT_RAISE();
  }

  /* cells over elements 1 .. n-1 ending in the tail, or the tail
     alone for a single element */
  PyObject **items = PySequence_Fast_ITEMS(coll);
  PyObject *rest;

  if (n == 1) {
    rest = items[1];
    Py_INCREF(rest);
  } else {
    rest = KosPair_ConsFromPtr(items + 1, n);
  }

  if (unlikely(! rest)) {
    Py_DECREF(coll);
    RESULT_RAISE();
  }

  PyObject *result = KosBracketPair_New(items[0], rest);
  Py_DECREF(rest);
  Py_DECREF(coll);
  RESULT_STEAL(ctx, result);
}


// The end.
