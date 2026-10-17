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
   tests/native/test_matcher.c

   Native tests for the matcher's step helpers: the cursor over a
   sequence or a pair spine, the pull, rest and collect walks, the
   captures, and the element tests read through a Datum box. Each
   walk is checked for the references it takes and releases, and the
   subjects that read past a sequence or loop a spine run here under
   ASan and Valgrind. The matcher core's cursor steps and marks are
   tested the same way: each step's hit and miss, the references it
   takes and releases, the copy of a shared mark, and the captures
   over a sequence and a spine.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


#define ANY_WIDTH KOS_MATCH_PACK(0, KOS_MATCH_UNBOUNDED8)


/* === helpers === */


/**
   The proper list (1 2 ... n). New reference.
*/
static PyObject *make_spine(long n) {
  PyObject *tail = KosNil;

  Py_INCREF(tail);
  for (long i = n; i >= 1; i--) {
    PyObject *head = PyLong_FromLong(i);
    PyObject *cell = KosPair_New(head, tail);
    Py_DECREF(head);
    Py_DECREF(tail);
    if (! cell)
      return NULL;
    tail = cell;
  }
  return tail;
}


/**
   A Datum over val, as a pull from a spine boxes one. New reference.
*/
static PyObject *make_datum(PyObject *val) {
  return PyObject_CallFunctionObjArgs((PyObject *) &KosDatumType, val, NULL);
}


static int clear_expected(PyObject *exc_type) {
  if (! PyErr_ExceptionMatches(exc_type))
    return 0;
  PyErr_Clear();
  return 1;
}


/* === the walks === */


static int test_sequence_walk(void) {
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;

  TEST_ASSERT_NOT_NULL(subj, "subject");
  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  TEST_ASSERT(KosMatchCursor_Check(cursor), "a sequence cursor");
  TEST_ASSERT_REFCNT(subj, 2, "the cursor holds the subject");

  for (long i = 1; i <= 3; i++) {
    KosMatchStep step;
    TEST_ASSERT(! KosMatch_End(cursor), "not at the end");
    TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "pull");
    TEST_ASSERT_EQ(PyLong_AsLong(step.elem), i, "the element");
    TEST_ASSERT_SAME(step.cursor, cursor, "the step records the cursor");
    TEST_ASSERT_REFCNT(cursor, 2, "and holds a reference to it");
    TEST_ASSERT_NULL(step.cell, "no cell from a sequence");
    TEST_ASSERT_NULL(step.next, "no next from a sequence");

    PyObject *boxed = KosMatch_Box(&step);
    TEST_ASSERT_SAME(boxed, step.elem, "a sequence element is raw");
    Py_DECREF(boxed);

    PyObject *advanced = KosMatch_StepAdvance(&step);
    TEST_ASSERT_SAME(advanced, cursor, "the only holder advances in place");
    Py_DECREF(advanced);
    KosMatch_StepClear(&step);
    TEST_ASSERT_NULL(step.elem, "clear releases the element");
    TEST_ASSERT_NULL(step.cursor, "and the cursor");
    TEST_ASSERT_REFCNT(cursor, 1, "the step's reference is gone");
  }

  KosMatchStep past;
  TEST_ASSERT(KosMatch_End(cursor), "at the end");
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &past), 0, "a pull at the end misses");
  TEST_ASSERT_EQ(KosMatch_Available(cursor, 1), 0, "none available");

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "the cursor released the subject");
  Py_DECREF(subj);
  return 0;
}


static int test_descend_gate_and_widths(void) {
  PyObject *subj = Py_BuildValue("[ii]", 1, 2);
  PyObject *num = PyLong_FromLong(7);
  PyObject *cursor = NULL;

  TEST_ASSERT_EQ(KosMatch_Descend(num, ANY_WIDTH, &cursor), 0,
                 "an int is not a sequence");
  TEST_ASSERT_NULL(cursor, "no cursor on a miss");
  TEST_ASSERT_EQ(KosMatch_Descend(subj, KOS_MATCH_PACK(3, 3), &cursor), 0,
                 "too short for the widths");
  TEST_ASSERT_EQ(KosMatch_Descend(subj, KOS_MATCH_PACK(0, 1), &cursor), 0,
                 "too long for the widths");
  TEST_ASSERT_EQ(KosMatch_Descend(subj, KOS_MATCH_PACK(2, 2), &cursor), 1,
                 "within the widths");
  TEST_ASSERT_REFCNT(subj, 2, "the cursor holds the list");

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(num);
  Py_DECREF(subj);
  return 0;
}


static int test_spine_walk(void) {
  PyObject *subj = make_spine(3);
  PyObject *cursor = NULL;

  TEST_ASSERT_NOT_NULL(subj, "subject");
  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  TEST_ASSERT_SAME(cursor, subj, "the spine is its own cursor");
  TEST_ASSERT_REFCNT(subj, 2, "the cursor reference");

  for (long i = 1; i <= 3; i++) {
    KosMatchStep step;
    TEST_ASSERT(! KosMatch_End(cursor), "not at the end");
    TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "pull");
    TEST_ASSERT_EQ(PyLong_AsLong(step.elem), i, "the element");
    TEST_ASSERT_SAME(step.cell, cursor, "the step records the cell");
    TEST_ASSERT_REFCNT(cursor, 3, "and holds a reference beside the walk's");
    TEST_ASSERT_NULL(step.cursor, "no sequence cursor from a spine");
    TEST_ASSERT_SAME(step.next, KosPair_CDR(cursor), "next is the cdr");

    PyObject *boxed = KosMatch_Box(&step);
    TEST_ASSERT_NOT_NULL(boxed, "box");
    TEST_ASSERT(KosDatum_Check(boxed), "a spine element boxes");
    TEST_ASSERT_SAME(((KosDatum *) boxed)->datum, step.elem,
                     "the box holds the element");
    Py_DECREF(boxed);

    /* the handler stores next into the cursor's stack entry and
       clears the step; the step's reference to next moves to the
       entry */
    PyObject *next = step.next;
    step.next = NULL;
    KosMatch_StepClear(&step);
    TEST_ASSERT_NULL(step.cell, "clear releases the cell");
    TEST_ASSERT_REFCNT(cursor, 2, "the step's reference is gone");
    Py_DECREF(cursor);
    cursor = next;
  }

  KosMatchStep past;
  TEST_ASSERT(KosNilP(cursor), "the cursor reached nil");
  TEST_ASSERT(KosMatch_End(cursor), "at the end");
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &past), 0, "a pull at nil misses");

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "every cell reference released");
  Py_DECREF(subj);
  return 0;
}


static int test_box_reads_a_boxed_car_once(void) {
  PyObject *inner = PyLong_FromLong(9);
  PyObject *datum = make_datum(inner);
  PyObject *subj = KosPair_New(datum, KosNil);
  KosMatchStep step;

  TEST_ASSERT_NOT_NULL(subj, "subject");
  TEST_ASSERT_EQ(KosMatch_Pull(subj, &step), 1, "pull");

  PyObject *boxed = KosMatch_Box(&step);
  TEST_ASSERT_NOT_NULL(boxed, "box");
  TEST_ASSERT(KosDatum_Check(boxed), "boxed");
  TEST_ASSERT_SAME(((KosDatum *) boxed)->datum, inner,
                   "the box holds the peeled value, not a Datum");

  Py_DECREF(boxed);
  KosMatch_StepClear(&step);
  Py_DECREF(subj);
  Py_DECREF(datum);
  Py_DECREF(inner);
  return 0;
}


static int test_rest_over_a_spine(void) {
  PyObject *subj = make_spine(4);
  PyObject *value = NULL;
  PyObject *next = NULL;

  TEST_ASSERT_NOT_NULL(subj, "subject");

  /* a suffix of two: the first two cells boxed, next the third */
  TEST_ASSERT_EQ(KosMatch_Rest(subj, KOS_MATCH_PACK(0, 2), &value, &next),
                 1, "rest with a suffix");
  TEST_ASSERT(PyTuple_CheckExact(value) && PyTuple_GET_SIZE(value) == 2,
              "two boxed elements");
  TEST_ASSERT(KosDatum_Check(PyTuple_GET_ITEM(value, 0)), "boxed");
  TEST_ASSERT_SAME(next, KosPair_CDR(KosPair_CDR(subj)),
                   "next is the suffix's first cell");
  Py_DECREF(value);
  Py_DECREF(next);

  /* no suffix: the cell itself boxed, next the terminator */
  TEST_ASSERT_EQ(KosMatch_Rest(subj, KOS_MATCH_PACK(0, 0), &value, &next),
                 1, "rest of the whole spine");
  TEST_ASSERT(KosDatum_Check(value), "boxed");
  TEST_ASSERT_SAME(((KosDatum *) value)->datum, subj, "the spine itself");
  TEST_ASSERT(KosNilP(next), "next is nil");
  Py_DECREF(value);
  Py_DECREF(next);

  /* the empty remainder: a Datum over nil, the cursor unchanged */
  TEST_ASSERT_EQ(KosMatch_Rest(KosNil, KOS_MATCH_PACK(0, 0), &value, &next),
                 1, "rest of nothing");
  TEST_ASSERT(KosDatum_Check(value), "boxed");
  TEST_ASSERT(KosNilP(((KosDatum *) value)->datum), "over nil");
  TEST_ASSERT_SAME(next, KosNil, "next is the cursor");
  Py_DECREF(value);
  Py_DECREF(next);

  /* misses: not enough for the minimum tail, or for the suffix */
  TEST_ASSERT_EQ(KosMatch_Rest(subj, KOS_MATCH_PACK(5, 0), &value, &next),
                 0, "short of the minimum");
  TEST_ASSERT_EQ(KosMatch_Rest(subj, KOS_MATCH_PACK(0, 5), &value, &next),
                 0, "short of the suffix");
  TEST_ASSERT_NULL(value, "no value on a miss");

  TEST_ASSERT_REFCNT(subj, 1, "nothing held");
  Py_DECREF(subj);
  return 0;
}


static int test_rest_over_a_sequence(void) {
  PyObject *subj = Py_BuildValue("(iiii)", 1, 2, 3, 4);
  PyObject *cursor = NULL;
  PyObject *value = NULL;
  PyObject *next = NULL;
  KosMatchStep step;

  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatch_Rest(cursor, KOS_MATCH_PACK(0, 1), &value, &next),
                 1, "rest with a suffix of one");
  TEST_ASSERT(PyTuple_CheckExact(value) && PyTuple_GET_SIZE(value) == 3,
              "three consumed");
  TEST_ASSERT_SAME(next, cursor, "the only holder advances in place");
  TEST_ASSERT_EQ(((KosMatchCursor *) cursor)->index, 3, "past the three");
  Py_DECREF(next);
  TEST_ASSERT(! KosMatch_End(cursor), "the suffix remains");
  Py_DECREF(value);

  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "the suffix element");
  TEST_ASSERT_EQ(PyLong_AsLong(step.elem), 4, "the last one");
  next = KosMatch_StepAdvance(&step);
  TEST_ASSERT_SAME(next, cursor, "in place again");
  Py_DECREF(next);
  KosMatch_StepClear(&step);
  TEST_ASSERT(KosMatch_End(cursor), "at the end");

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


static int test_list_shrinks_under_the_walk(void) {
  PyObject *subj = Py_BuildValue("[iii]", 1, 2, 3);
  PyObject *cursor = NULL;
  PyObject *value = NULL;
  PyObject *next = NULL;
  KosMatchStep step;

  TEST_ASSERT_EQ(KosMatch_Descend(subj, KOS_MATCH_PACK(3, 3), &cursor), 1,
                 "descend records three");
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "first pull");
  next = KosMatch_StepAdvance(&step);
  TEST_ASSERT_SAME(next, cursor, "in place");
  Py_DECREF(next);
  KosMatch_StepClear(&step);

  /* user code drops the last element: two recorded ahead, one live */
  TEST_ASSERT_EQ(PyList_SetSlice(subj, 2, 3, NULL), 0, "shrink to two");
  TEST_ASSERT_EQ(KosMatch_Available(cursor, 2), 0, "two are not available");
  TEST_ASSERT_EQ(KosMatch_Available(cursor, 1), 1, "one is");
  TEST_ASSERT_EQ(KosMatch_Rest(cursor, KOS_MATCH_PACK(0, 0), &value, &next),
                 1, "rest reads the live size");
  TEST_ASSERT(PyTuple_CheckExact(value) && PyTuple_GET_SIZE(value) == 1,
              "one element");
  TEST_ASSERT_EQ(PyLong_AsLong(PyTuple_GET_ITEM(value, 0)), 2, "the second");
  Py_DECREF(value);
  Py_DECREF(next);
  TEST_ASSERT(KosMatch_End(cursor), "at the live end");

  /* and then everything */
  TEST_ASSERT_EQ(PyList_SetSlice(subj, 0, 2, NULL), 0, "shrink to none");
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 0,
                 "a pull past the live end misses");
  TEST_ASSERT(KosMatch_End(cursor), "still at the end");

  PyObject *out = PyList_New(0);
  TEST_ASSERT_EQ(KosMatch_Collect(cursor, out, &next), 1, "collect");
  TEST_ASSERT_EQ(PyList_GET_SIZE(out), 0, "collects nothing");
  TEST_ASSERT_SAME(next, cursor, "in place");
  Py_DECREF(next);
  Py_DECREF(out);

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


static int test_list_subclass_shrinks_under_the_walk(void) {
  PyObject *cls = PyObject_CallFunction(
      (PyObject *) &PyType_Type, "s(O){}", "L", (PyObject *) &PyList_Type);
  TEST_ASSERT_NOT_NULL(cls, "a list subclass");
  PyObject *subj = PyObject_CallNoArgs(cls);
  TEST_ASSERT_NOT_NULL(subj, "an instance");
  PyObject *items = Py_BuildValue("[iii]", 1, 2, 3);
  TEST_ASSERT_EQ(PyList_SetSlice(subj, 0, 0, items), 0, "filled");
  Py_DECREF(items);

  PyObject *cursor = NULL;
  PyObject *value = NULL;
  PyObject *next = NULL;
  KosMatchStep step;

  TEST_ASSERT_EQ(KosMatch_Descend(subj, KOS_MATCH_PACK(3, 3), &cursor), 1,
                 "descend records three");
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "first pull");
  next = KosMatch_StepAdvance(&step);
  Py_DECREF(next);
  KosMatch_StepClear(&step);

  /* user code drops the last two: the subclass answers its live size
     as an exact list does */
  TEST_ASSERT_EQ(PyList_SetSlice(subj, 1, 3, NULL), 0, "shrink to one");
  TEST_ASSERT_EQ(KosMatch_Available(cursor, 1), 0, "none available");
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 0, "a pull misses");
  TEST_ASSERT(! PyErr_Occurred(), "no IndexError");
  TEST_ASSERT_EQ(KosMatch_Rest(cursor, KOS_MATCH_PACK(0, 0), &value, &next),
                 1, "rest reads the live size");
  TEST_ASSERT(PyTuple_CheckExact(value) && PyTuple_GET_SIZE(value) == 0,
              "no elements");
  Py_DECREF(value);
  Py_DECREF(next);
  TEST_ASSERT(KosMatch_End(cursor), "at the live end");

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  Py_DECREF(cls);
  return 0;
}


static int test_looping_spine_is_refused(void) {
  PyObject *a = make_spine(2);
  PyObject *b = KosPair_CDR(a);
  PyObject *cursor = NULL;
  PyObject *next = NULL;
  PyObject *out = PyList_New(0);

  TEST_ASSERT_NOT_NULL(a, "subject");
  KosPair_SETCDR(b, a);

  TEST_ASSERT_EQ(KosMatch_Descend(a, ANY_WIDTH, &cursor), -1,
                 "descent refuses the loop");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  TEST_ASSERT_NULL(cursor, "no cursor");

  TEST_ASSERT_EQ(KosMatch_Collect(a, out, &next), -1,
                 "collect refuses the loop");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  TEST_ASSERT_EQ(PyList_GET_SIZE(out), 0, "nothing appended");

  PyObject *value = NULL;
  TEST_ASSERT_EQ(KosMatch_Rest(a, KOS_MATCH_PACK(0, 0), &value, &next), -1,
                 "rest refuses the loop");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");

  /* a bounded walk terminates on its own */
  TEST_ASSERT_EQ(KosMatch_Available(a, 5), 1,
                 "five are available around the loop");

  KosPair_SETCDR(b, KosNil);
  Py_DECREF(out);
  TEST_ASSERT_REFCNT(a, 1, "nothing held");
  Py_DECREF(a);
  return 0;
}


static int test_collect_over_a_spine(void) {
  PyObject *subj = make_spine(3);
  PyObject *out = PyList_New(0);
  PyObject *next = NULL;

  TEST_ASSERT_EQ(KosMatch_Collect(subj, out, &next), 1, "collect");
  TEST_ASSERT_EQ(PyList_GET_SIZE(out), 3, "three appended");
  TEST_ASSERT(KosDatum_Check(PyList_GET_ITEM(out, 0)), "boxed");
  TEST_ASSERT(KosNilP(next), "next is the terminator");
  Py_DECREF(next);
  Py_DECREF(out);

  /* an improper tail misses before anything is appended */
  PyObject *tail = PyLong_FromLong(5);
  PyObject *improper = KosPair_New(tail, tail);
  out = PyList_New(0);
  TEST_ASSERT_EQ(KosMatch_Collect(improper, out, &next), 0, "improper misses");
  TEST_ASSERT_EQ(PyList_GET_SIZE(out), 0, "nothing appended");
  Py_DECREF(out);
  Py_DECREF(improper);
  Py_DECREF(tail);

  TEST_ASSERT_REFCNT(subj, 1, "nothing held");
  Py_DECREF(subj);
  return 0;
}


static int test_advance_under_a_mark(void) {
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;
  KosMatchStep step;

  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  /* the cursor held a second time, as a cloned segment holding its
     stack entry would; MATCH_MARK pushes a separate mark object */
  PyObject *held = cursor;
  Py_INCREF(held);

  /* with a second holder the advance replaces: the held cursor keeps
     its position */
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "pull");
  PyObject *advanced = KosMatch_StepAdvance(&step);
  KosMatch_StepClear(&step);
  TEST_ASSERT_NOT_NULL(advanced, "advanced");
  TEST_ASSERT(advanced != cursor, "a held cursor advances by replacement");
  TEST_ASSERT(KosMatchCursor_Check(advanced), "a sequence cursor");
  TEST_ASSERT_EQ(((KosMatchCursor *) advanced)->index, 1, "past one");
  TEST_ASSERT_EQ(((KosMatchCursor *) cursor)->index, 0,
                 "the held cursor untouched");
  TEST_ASSERT_SAME(((KosMatchCursor *) advanced)->seq, subj, "same subject");

  /* the second holder released, the cursor is again the only
     holder: the next advance moves it in place */
  Py_DECREF(held);
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "pull again");
  PyObject *moved = KosMatch_StepAdvance(&step);
  KosMatch_StepClear(&step);
  TEST_ASSERT_SAME(moved, cursor, "in place");
  TEST_ASSERT_EQ(((KosMatchCursor *) cursor)->index, 1, "past one");
  Py_DECREF(moved);

  Py_DECREF(advanced);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


static int test_mark_remark_and_reset(void) {
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;
  KosMatchStep step;

  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  PyObject *mark = KosMatch_Mark(cursor);
  TEST_ASSERT_NOT_NULL(mark, "a mark");
  TEST_ASSERT(mark != cursor, "its own object");
  TEST_ASSERT(KosMatchCursor_Check(mark), "of the cursor's type");
  TEST_ASSERT_EQ(KosMatchCursor_KIND(mark), KOS_MATCH_MARK, "kind mark");
  TEST_ASSERT_EQ(KosMatchCursor_KIND(cursor), KOS_MATCH_CURSOR, "kind cursor");
  TEST_ASSERT_EQ(((KosMatchCursor *) mark)->index, 0, "at the cursor's index");
  TEST_ASSERT_SAME(((KosMatchCursor *) mark)->seq, subj, "over the subject");

  /* the cursor advances in place beside a separate mark */
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "pull");
  PyObject *advanced = KosMatch_StepAdvance(&step);
  KosMatch_StepClear(&step);
  TEST_ASSERT_SAME(advanced, cursor, "in place");
  Py_DECREF(advanced);
  TEST_ASSERT_EQ(((KosMatchCursor *) mark)->index, 0, "the mark untouched");

  /* a re-mark moves an unheld mark in place */
  PyObject *moved = KosMatch_Remark(cursor, mark);
  TEST_ASSERT_SAME(moved, mark, "the mark moved in place");
  TEST_ASSERT_EQ(((KosMatchCursor *) mark)->index, 1, "to the cursor's index");
  TEST_ASSERT_EQ(KosMatchCursor_KIND(mark), KOS_MATCH_MARK, "still a mark");
  Py_DECREF(moved);

  /* a held mark is replaced */
  Py_INCREF(mark);
  moved = KosMatch_Remark(cursor, mark);
  TEST_ASSERT(moved != mark, "a held mark is replaced");
  TEST_ASSERT_EQ(KosMatchCursor_KIND(moved), KOS_MATCH_MARK, "by a mark");
  TEST_ASSERT_EQ(((KosMatchCursor *) moved)->index, 1, "at the index");
  Py_DECREF(moved);
  Py_DECREF(mark);

  /* a reset moves an unheld cursor back in place; a held one is
     replaced by a cursor */
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "pull");
  advanced = KosMatch_StepAdvance(&step);
  KosMatch_StepClear(&step);
  Py_DECREF(advanced);
  TEST_ASSERT_EQ(((KosMatchCursor *) cursor)->index, 2, "past two");
  PyObject *back = KosMatch_Reset(cursor, mark);
  TEST_ASSERT_SAME(back, cursor, "the cursor moved in place");
  TEST_ASSERT_EQ(((KosMatchCursor *) cursor)->index, 1, "back at the mark");
  Py_DECREF(back);
  Py_INCREF(cursor);
  back = KosMatch_Reset(cursor, mark);
  TEST_ASSERT(back != cursor, "a held cursor is replaced");
  TEST_ASSERT_EQ(KosMatchCursor_KIND(back), KOS_MATCH_CURSOR, "by a cursor");
  TEST_ASSERT_EQ(((KosMatchCursor *) back)->index, 1, "at the mark");
  Py_DECREF(back);
  Py_DECREF(cursor);

  Py_DECREF(mark);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);

  /* a spine: the cell is the mark, the re-mark and the reset */
  PyObject *spine = make_spine(2);
  PyObject *second = KosPair_CDR(spine);
  mark = KosMatch_Mark(spine);
  TEST_ASSERT_SAME(mark, spine, "the cell itself");
  moved = KosMatch_Remark(second, mark);
  TEST_ASSERT_SAME(moved, second, "the current cell");
  Py_DECREF(moved);
  back = KosMatch_Reset(second, mark);
  TEST_ASSERT_SAME(back, spine, "the marked cell");
  Py_DECREF(back);
  Py_DECREF(mark);
  TEST_ASSERT_REFCNT(spine, 1, "released");
  Py_DECREF(spine);
  return 0;
}


/**
   Remark and Reset over a sequence cursor with mark as the mark: both
   NULL with RuntimeError set, and mark left as it was.
*/
static int refuse_over_a_sequence(PyObject *cursor, PyObject *mark) {
  TEST_ASSERT_NULL(KosMatch_Remark(cursor, mark), "re-mark refused");
  TEST_ASSERT(clear_expected(PyExc_RuntimeError), "RuntimeError");
  TEST_ASSERT_NULL(KosMatch_Reset(cursor, mark), "reset refused");
  TEST_ASSERT(clear_expected(PyExc_RuntimeError), "RuntimeError");
  TEST_ASSERT_EQ(((KosMatchCursor *) cursor)->index, 1, "cursor unmoved");
  return 0;
}


static int test_remark_and_reset_refuse_a_foreign_mark(void) {
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;
  PyObject *other = NULL;
  KosMatchStep step;

  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "pull");
  PyObject *advanced = KosMatch_StepAdvance(&step);
  KosMatch_StepClear(&step);
  Py_DECREF(advanced);

  /* a list the only holder of itself, where a move in place would
     write */
  PyObject *list = Py_BuildValue("[ii]", 7, 8);
  if (refuse_over_a_sequence(cursor, list))
    return -1;
  TEST_ASSERT_EQ(PyList_GET_SIZE(list), 2, "the list keeps its size");
  TEST_ASSERT_EQ(PyLong_AsLong(PyList_GET_ITEM(list, 0)), 7, "first item");
  TEST_ASSERT_EQ(PyLong_AsLong(PyList_GET_ITEM(list, 1)), 8, "second item");
  TEST_ASSERT_REFCNT(list, 1, "list released by both");
  Py_DECREF(list);

  /* a pair cell */
  PyObject *spine = make_spine(1);
  if (refuse_over_a_sequence(cursor, spine))
    return -1;
  TEST_ASSERT_REFCNT(spine, 1, "cell released by both");

  /* a cursor, not a mark */
  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &other), 1, "descend");
  if (refuse_over_a_sequence(cursor, other))
    return -1;
  TEST_ASSERT_EQ(((KosMatchCursor *) other)->index, 0, "other unmoved");
  TEST_ASSERT_REFCNT(other, 1, "other released by both");

  Py_DECREF(other);
  Py_DECREF(spine);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


/* === captures === */


static int test_captures_over_a_sequence(void) {
  PyObject *subj = Py_BuildValue("(ii)", 1, 2);
  PyObject *names = Py_BuildValue("(ss)", "a", "b");
  PyObject *bound = PyList_New(0);
  PyObject *values[2] = { bound, NULL };
  PyObject *start = NULL;
  PyObject *cursor = NULL;

  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &start), 1, "the start");
  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  PyObject *out = KosMatch_Captures(names, values, start, cursor);
  TEST_ASSERT_NOT_NULL(out, "captures");
  TEST_ASSERT(PyDict_CheckExact(out), "a plain dict for a sequence");
  TEST_ASSERT_EQ(PyDict_GET_SIZE(out), 1, "the unbound slot is absent");
  TEST_ASSERT_SAME(PyDict_GetItemString(out, "a"), bound, "bound under a");
  TEST_ASSERT_REFCNT(bound, 2, "the dict holds the value");
  Py_DECREF(out);
  TEST_ASSERT_REFCNT(bound, 1, "released");

  Py_DECREF(cursor);
  Py_DECREF(start);
  Py_DECREF(bound);
  Py_DECREF(names);
  Py_DECREF(subj);
  return 0;
}


static int test_captures_over_a_spine(void) {
  PyObject *subj = make_spine(3);
  PyObject *names = Py_BuildValue("(s)", "a");
  PyObject *bound = PyList_New(0);
  PyObject *values[1] = { bound };
  PyObject *third = KosPair_CDR(KosPair_CDR(subj));

  /* a span of two cells: the source is the tuple of their cars */
  PyObject *out = KosMatch_Captures(names, values, subj, third);
  TEST_ASSERT_NOT_NULL(out, "captures");
  TEST_ASSERT(KosDatumFrame_Check(out), "a DatumFrame for a spine");
  PyObject *source = ((KosDatumFrame *) out)->source;
  TEST_ASSERT(PyTuple_CheckExact(source) && PyTuple_GET_SIZE(source) == 2,
              "the span's cars");
  TEST_ASSERT_SAME(PyDict_GetItemString(out, "a"), bound, "bound under a");
  Py_DECREF(out);

  /* a span to the end: the start cell is the source */
  out = KosMatch_Captures(names, values, subj, KosNil);
  TEST_ASSERT_NOT_NULL(out, "captures");
  TEST_ASSERT_SAME(((KosDatumFrame *) out)->source, subj, "the spine");
  Py_DECREF(out);

  /* a start that no longer reaches the cursor: the subject's spine
     was rewritten under the walk */
  PyObject *other = make_spine(1);
  TEST_ASSERT_NULL(KosMatch_Captures(names, values, other, third),
                   "disjoint span");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  Py_DECREF(other);

  TEST_ASSERT_REFCNT(bound, 1, "released");
  TEST_ASSERT_REFCNT(subj, 1, "nothing held");
  Py_DECREF(bound);
  Py_DECREF(names);
  Py_DECREF(subj);
  return 0;
}


static int test_span_looping_behind_the_cursor(void) {
  PyObject *names = Py_BuildValue("(s)", "a");
  PyObject *bound = PyList_New(0);
  PyObject *values[1] = { bound };
  PyObject *unbound[1] = { NULL };
  PyObject *spine = make_spine(3);
  PyObject *second = KosPair_CDR(spine);
  PyObject *third = KosPair_CDR(second);

  /* a predicate's setcdr closes a loop behind the cursor's cell: the
     span from either cell before it never reaches the cursor */
  Py_INCREF(third);
  KosPair_SETCDR(second, spine);

  TEST_ASSERT_NULL(KosMatch_Captures(names, values, spine, third),
                   "captures over a looping span");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  TEST_ASSERT_NULL(KosMatch_Iteration(names, unbound, second, third),
                   "iteration over a looping span");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");

  /* a start that no longer reaches the cursor: the iteration raises
     as the captures do */
  PyObject *other = make_spine(1);
  TEST_ASSERT_NULL(KosMatch_Iteration(names, unbound, other, third),
                   "disjoint span");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  Py_DECREF(other);

  KosPair_SETCDR(second, third);
  Py_DECREF(third);
  TEST_ASSERT_REFCNT(bound, 1, "released");
  TEST_ASSERT_REFCNT(spine, 1, "nothing held");
  Py_DECREF(bound);
  Py_DECREF(names);
  Py_DECREF(spine);
  return 0;
}


static int test_iteration_values(void) {
  PyObject *names = Py_BuildValue("(s)", "x");
  PyObject *values[1] = { NULL };
  PyObject *spine = make_spine(3);
  PyObject *second = KosPair_CDR(spine);
  PyObject *third = KosPair_CDR(second);

  /* no slot bound: one cell boxes, more make a tuple of boxes */
  PyObject *out = KosMatch_Iteration(names, values, spine, second);
  TEST_ASSERT_NOT_NULL(out, "one cell");
  TEST_ASSERT(KosDatum_Check(out), "boxed");
  TEST_ASSERT_EQ(PyLong_AsLong(((KosDatum *) out)->datum), 1, "the car");
  Py_DECREF(out);

  out = KosMatch_Iteration(names, values, spine, third);
  TEST_ASSERT_NOT_NULL(out, "two cells");
  TEST_ASSERT(PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 2,
              "a tuple");
  TEST_ASSERT(KosDatum_Check(PyTuple_GET_ITEM(out, 1)), "of boxes");
  Py_DECREF(out);

  /* a slot bound: the captures */
  PyObject *bound = PyLong_FromLong(5);
  values[0] = bound;
  out = KosMatch_Iteration(names, values, spine, second);
  TEST_ASSERT_NOT_NULL(out, "captures");
  TEST_ASSERT(KosDatumFrame_Check(out), "a frame");
  Py_DECREF(out);
  values[0] = NULL;

  /* a zero-width span is an error */
  TEST_ASSERT_NULL(KosMatch_Iteration(names, values, spine, spine),
                   "no advance");
  TEST_ASSERT(clear_expected(PyExc_RuntimeError), "RuntimeError");

  /* a sequence: the item, or the slice */
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;
  PyObject *start = NULL;
  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &start), 1, "the start");
  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  ((KosMatchCursor *) cursor)->index = 1;
  out = KosMatch_Iteration(names, values, start, cursor);
  TEST_ASSERT_NOT_NULL(out, "one item");
  TEST_ASSERT_EQ(PyLong_AsLong(out), 1, "raw");
  Py_DECREF(out);
  ((KosMatchCursor *) cursor)->index = 3;
  out = KosMatch_Iteration(names, values, start, cursor);
  TEST_ASSERT_NOT_NULL(out, "three items");
  TEST_ASSERT(PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 3,
              "a slice");
  Py_DECREF(out);

  Py_DECREF(cursor);
  Py_DECREF(start);
  Py_DECREF(subj);
  Py_DECREF(bound);
  TEST_ASSERT_REFCNT(spine, 1, "nothing held");
  Py_DECREF(spine);
  Py_DECREF(names);
  return 0;
}


/* === the element tests, read through the box === */


static int test_core_types_through_the_box(void) {
  PyObject *name = PyUnicode_FromString("sym");
  PyObject *sym = KosSymbol_FromString(name);
  PyObject *kw = KosKeyword_FromString(name);
  PyObject *spine = make_spine(1);
  PyObject *num = PyLong_FromLong(1);
  PyObject *tup = PyTuple_New(0);
  PyObject *lst = PyList_New(0);
  struct { PyObject *val; KosCoreType core; } cases[] = {
    { spine, KOS_CORE_PAIR },
    { sym, KOS_CORE_SYMBOL },
    { kw, KOS_CORE_KEYWORD },
    { num, KOS_CORE_INT },
    { name, KOS_CORE_STR },
    { tup, KOS_CORE_TUPLE },
    { lst, KOS_CORE_LIST },
  };
  size_t count = sizeof(cases) / sizeof(cases[0]);

  for (size_t i = 0; i < count; i++) {
    PyObject *boxed = make_datum(cases[i].val);
    TEST_ASSERT_NOT_NULL(boxed, "box");
    for (size_t j = 0; j < count; j++) {
      int want = i == j ? 1 : 0;
      TEST_ASSERT_EQ(KosMatch_CoreTypeTest(boxed, cases[j].core), want,
                     "the boxed value's core type alone");
      TEST_ASSERT_EQ(KosMatch_CoreTypeTest(cases[i].val, cases[j].core), want,
                     "and raw");
    }
    Py_DECREF(boxed);
  }

  TEST_ASSERT_EQ(KosMatch_CoreTypeTest(num, KOS_CORE_COUNT), -1,
                 "the sentinel is an error");
  TEST_ASSERT(clear_expected(PyExc_SystemError), "SystemError");

  Py_DECREF(lst);
  Py_DECREF(tup);
  Py_DECREF(num);
  Py_DECREF(spine);
  Py_DECREF(kw);
  Py_DECREF(sym);
  Py_DECREF(name);
  return 0;
}


static int test_literal_and_same_through_the_box(void) {
  PyObject *five = PyLong_FromLong(5);
  PyObject *six = PyLong_FromLong(6);
  PyObject *boxed = make_datum(five);
  PyObject *other = make_datum(five);

  TEST_ASSERT_EQ(KosMatch_LiteralTest(boxed, five), 1, "equal literal");
  TEST_ASSERT_EQ(KosMatch_LiteralTest(boxed, six), 0, "unequal literal");
  TEST_ASSERT_EQ(KosMatch_SameTest(boxed, other), 1, "same through two boxes");
  TEST_ASSERT_EQ(KosMatch_SameTest(boxed, six), 0, "not the same");

  /* a frame on either side is never the same */
  PyObject *names = PyTuple_New(0);
  PyObject *spine = make_spine(1);
  PyObject *frame = KosMatch_Captures(names, NULL, spine, KosNil);
  TEST_ASSERT_NOT_NULL(frame, "frame");
  TEST_ASSERT_EQ(KosMatch_SameTest(frame, frame), 0, "a frame is not same");
  Py_DECREF(frame);
  Py_DECREF(spine);
  Py_DECREF(names);

  Py_DECREF(other);
  Py_DECREF(boxed);
  Py_DECREF(six);
  Py_DECREF(five);
  return 0;
}


static int test_mapping_through_the_box(void) {
  PyObject *map = Py_BuildValue("{si}", "k", 1);
  PyObject *boxed = make_datum(map);
  PyObject *num = PyLong_FromLong(1);
  PyObject *key = PyUnicode_FromString("k");
  PyObject *absent = PyUnicode_FromString("z");
  PyObject *out = NULL;

  TEST_ASSERT_EQ(KosMatch_MapTest(boxed), 1, "a dict through the box");
  TEST_ASSERT_EQ(KosMatch_MapTest(num), 0, "an int is not a mapping");
  TEST_ASSERT_EQ(KosMatch_Item(boxed, key, &out), 1, "present");
  TEST_ASSERT_EQ(PyLong_AsLong(out), 1, "the value");
  Py_DECREF(out);
  TEST_ASSERT_EQ(KosMatch_Item(boxed, absent, &out), 0, "absent");
  TEST_ASSERT_EQ(PyDict_GET_SIZE(map), 1, "the probe inserted nothing");

  Py_DECREF(absent);
  Py_DECREF(key);
  Py_DECREF(num);
  Py_DECREF(boxed);
  Py_DECREF(map);
  return 0;
}


static int test_class_and_fields_through_the_box(void) {
  PyObject *env = PyDict_New();
  PyObject *out = NULL;

  TEST_ASSERT_NOT_NULL(env, "env");
  PyObject *ran = PyRun_String(
      "class P:\n"
      "    __match_args__ = ('x', 'y')\n"
      "    def __init__(self, x, y):\n"
      "        self.x = x\n"
      "        self.y = y\n"
      "p = P(1, 2)\n",
      Py_file_input, env, env);
  if (! ran) {
    PyErr_Print();
    Py_DECREF(env);
    return -1;
  }
  Py_DECREF(ran);
  PyObject *cls = PyDict_GetItemString(env, "P");
  PyObject *inst = PyDict_GetItemString(env, "p");
  PyObject *boxed = make_datum(inst);
  PyObject *yname = PyUnicode_FromString("y");
  PyObject *zname = PyUnicode_FromString("z");

  TEST_ASSERT_EQ(KosMatch_ClassTest(boxed, cls, 2), 1,
                 "an instance through the box, two positional");
  TEST_ASSERT_EQ(KosMatch_ClassTest(boxed, (PyObject *) &PyLong_Type, 0), 0,
                 "not an int");
  TEST_ASSERT_EQ(KosMatch_ClassTest(boxed, cls, 3), -1,
                 "three positional over two match args");
  TEST_ASSERT(clear_expected(PyExc_TypeError), "TypeError");

  TEST_ASSERT_EQ(KosMatch_FieldPos(boxed, cls, 1, &out), 1,
                 "field by position");
  TEST_ASSERT_EQ(PyLong_AsLong(out), 2, "y");
  Py_DECREF(out);
  TEST_ASSERT_EQ(KosMatch_FieldPos(boxed, cls, 2, &out), -1,
                 "past the match args");
  TEST_ASSERT(clear_expected(PyExc_TypeError), "TypeError");

  TEST_ASSERT_EQ(KosMatch_FieldKw(boxed, yname, &out), 1, "field by name");
  TEST_ASSERT_EQ(PyLong_AsLong(out), 2, "y");
  Py_DECREF(out);
  TEST_ASSERT_EQ(KosMatch_FieldKw(boxed, zname, &out), 0,
                 "a missing field misses");

  Py_DECREF(zname);
  Py_DECREF(yname);
  Py_DECREF(boxed);
  Py_DECREF(env);
  return 0;
}


/* === the core cursor === */


/**
   Give each cell of spine the position (1, n), n counting from 1.
*/
static void set_positions(PyObject *spine) {
  long n = 1;

  for (PyObject *cell = spine; KosPair_Check(cell) && ! KosNilP(cell);
       cell = KosPair_CDR(cell))
    KosPair_POSITION(cell) = Py_BuildValue("(il)", 1, n++);
}


/**
   The second member of a Datum's position tuple, or -1.
*/
static long datum_line(PyObject *datum) {
  PyObject *pos = ((KosDatum *) datum)->position;

  if (! PyTuple_Check(pos) || PyTuple_GET_SIZE(pos) != 2)
    return -1;
  return PyLong_AsLong(PyTuple_GET_ITEM(pos, 1));
}


/**
   Pull one element from *cursor, its stack entry the only holder, and
   store the moved cursor back. The element as a new reference; NULL
   at the end or on an error.
*/
static PyObject *core_pull(PyObject **cursor) {
  PyObject *elem = NULL;
  PyObject *next = NULL;

  if (KosMatchCursor_Next(*cursor, 1, &elem, &next) != 1)
    return NULL;
  Py_SETREF(*cursor, next);
  return elem;
}


static int test_core_descend_gate(void) {
  PyObject *tup = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *lst = Py_BuildValue("[ii]", 1, 2);
  PyObject *spine = make_spine(3);
  PyObject *boxed = make_datum(spine);
  PyObject *str = PyUnicode_FromString("ab");
  PyObject *map = PyDict_New();
  PyObject *num = PyLong_FromLong(7);
  PyObject *improper = KosPair_New(num, num);
  PyObject *cursor = NULL;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(tup, &cursor), 1, "a tuple");
  TEST_ASSERT(KosMatchCursor_Check(cursor), "a cursor");
  TEST_ASSERT_EQ(KosMatchCursor_KIND(cursor), KOS_MATCH_CURSOR,
                 "a sequence cursor");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 3, "three remain");
  TEST_ASSERT_REFCNT(tup, 2, "the cursor holds the tuple");
  Py_DECREF(cursor);

  TEST_ASSERT_EQ(KosMatchCursor_Descend(lst, &cursor), 1, "a list");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 2, "two remain");
  Py_DECREF(cursor);

  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "a spine");
  TEST_ASSERT_EQ(KosMatchCursor_KIND(cursor), KOS_MATCH_SPINE,
                 "a spine cursor");
  TEST_ASSERT_SAME(((KosMatchCursor *) cursor)->seq, spine,
                   "at the first cell");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 3, "counted once");
  TEST_ASSERT_REFCNT(spine, 3, "the box and the cursor hold the spine");
  Py_DECREF(cursor);

  TEST_ASSERT_EQ(KosMatchCursor_Descend(boxed, &cursor), 1,
                 "a boxed spine");
  TEST_ASSERT_SAME(((KosMatchCursor *) cursor)->seq, spine,
                   "read through the box");
  Py_DECREF(cursor);

  TEST_ASSERT_EQ(KosMatchCursor_Descend(KosNil, &cursor), 1, "nil");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 0, "an empty spine");
  Py_DECREF(cursor);

  TEST_ASSERT_EQ(KosMatchCursor_Descend(str, &cursor), 0, "a str misses");
  TEST_ASSERT_NULL(cursor, "no cursor on a miss");
  TEST_ASSERT_EQ(KosMatchCursor_Descend(map, &cursor), 0,
                 "a mapping misses");
  TEST_ASSERT_EQ(KosMatchCursor_Descend(num, &cursor), 0, "an int misses");
  TEST_ASSERT_EQ(KosMatchCursor_Descend(improper, &cursor), 0,
                 "an improper spine misses");
  TEST_ASSERT_NULL(cursor, "no cursor on a miss");

  /* a looping spine is refused by the count */
  PyObject *second = KosPair_CDR(spine);
  PyObject *third = KosPair_CDR(second);
  Py_INCREF(third);
  KosPair_SETCDR(second, spine);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), -1,
                 "a looping spine raises");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  KosPair_SETCDR(second, third);
  Py_DECREF(third);

  TEST_ASSERT_REFCNT(tup, 1, "released");
  TEST_ASSERT_REFCNT(spine, 2, "released");
  Py_DECREF(improper);
  Py_DECREF(num);
  Py_DECREF(map);
  Py_DECREF(str);
  Py_DECREF(boxed);
  Py_DECREF(spine);
  Py_DECREF(lst);
  Py_DECREF(tup);
  return 0;
}


static int test_core_next_over_a_sequence(void) {
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;
  PyObject *elem = NULL;
  PyObject *next = NULL;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  for (long i = 1; i <= 3; i++) {
    TEST_ASSERT_EQ(KosMatchCursor_Next(cursor, 1, &elem, &next), 1, "next");
    TEST_ASSERT_SAME(elem, PyTuple_GET_ITEM(subj, i - 1), "the item itself");
    TEST_ASSERT_SAME(next, cursor, "the only holder moves in place");
    TEST_ASSERT_REFCNT(cursor, 2, "the entry's and the returned reference");
    Py_DECREF(next);
    Py_DECREF(elem);
    TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 3 - i, "remaining");
  }
  TEST_ASSERT_EQ(KosMatchCursor_Next(cursor, 1, &elem, &next), 0,
                 "a pull at the end misses");
  TEST_ASSERT_NULL(elem, "no element");
  TEST_ASSERT_NULL(next, "no cursor");

  /* a cursor a cloned segment also holds stays where it was */
  PyObject *held = NULL;
  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &held), 1, "descend");
  Py_INCREF(held);
  TEST_ASSERT_EQ(KosMatchCursor_Next(held, 1, &elem, &next), 1, "next");
  TEST_ASSERT_NOT_SAME(next, held, "a shared cursor is not moved");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(held), 3, "the held one stays");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(next), 2, "the new one advanced");
  TEST_ASSERT_REFCNT(next, 1, "a new cursor");
  Py_DECREF(next);
  Py_DECREF(elem);
  Py_DECREF(held);
  Py_DECREF(held);

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "every cursor released the subject");
  Py_DECREF(subj);
  return 0;
}


static int test_core_next_over_a_spine(void) {
  PyObject *subj = make_spine(3);
  PyObject *cursor = NULL;

  set_positions(subj);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");

  PyObject *cell = subj;
  for (long i = 1; i <= 3; i++) {
    TEST_ASSERT_SAME(((KosMatchCursor *) cursor)->seq, cell,
                     "the cursor's cell");
    TEST_ASSERT_REFCNT(cell, 2, "the spine's and the cursor's reference");
    PyObject *elem = core_pull(&cursor);
    TEST_ASSERT_NOT_NULL(elem, "next");
    TEST_ASSERT(KosDatum_Check(elem), "a Datum from a spine");
    TEST_ASSERT_EQ(PyLong_AsLong(((KosDatum *) elem)->datum), i, "the car");
    TEST_ASSERT_EQ(datum_line(elem), i, "the cell's position");
    TEST_ASSERT_SAME(((KosDatum *) elem)->binding, Py_None, "no binding");
    TEST_ASSERT_REFCNT(elem, 1, "a fresh box");
    Py_DECREF(elem);
    TEST_ASSERT_REFCNT(cell, 1, "the cursor moved off the cell");
    cell = KosPair_CDR(cell);
  }

  TEST_ASSERT(KosNilP(((KosMatchCursor *) cursor)->seq), "at the terminator");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 0, "none remain");
  TEST_ASSERT_NULL(core_pull(&cursor), "a pull at the end misses");
  TEST_ASSERT(! PyErr_Occurred(), "a miss");

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


static int test_core_spine_edited_ahead(void) {
  PyObject *subj = make_spine(3);
  PyObject *second = KosPair_CDR(subj);
  PyObject *third = KosPair_CDR(second);
  PyObject *cursor = NULL;
  PyObject *elem;

  /* a cut ahead of the cursor: the pull at the cut misses */
  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  elem = core_pull(&cursor);
  TEST_ASSERT_NOT_NULL(elem, "first");
  Py_DECREF(elem);
  Py_INCREF(third);
  KosPair_SETCDR(second, KosNil);
  elem = core_pull(&cursor);
  TEST_ASSERT_NOT_NULL(elem, "second");
  Py_DECREF(elem);
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 1, "the count stands");
  TEST_ASSERT_NULL(core_pull(&cursor), "the cut is the end");
  TEST_ASSERT(! PyErr_Occurred(), "a miss");
  KosPair_SETCDR(second, third);
  Py_DECREF(third);
  Py_DECREF(cursor);

  /* a loop ahead of the cursor: the count ends the walk */
  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  KosPair_SETCDR(third, subj);
  for (long i = 1; i <= 3; i++) {
    elem = core_pull(&cursor);
    TEST_ASSERT_NOT_NULL(elem, "pull");
    TEST_ASSERT_EQ(PyLong_AsLong(((KosDatum *) elem)->datum), i, "the car");
    Py_DECREF(elem);
  }
  TEST_ASSERT_NULL(core_pull(&cursor), "the count ends the walk");
  TEST_ASSERT(! PyErr_Occurred(), "a miss");
  KosPair_SETCDR(third, KosNil);
  Py_DECREF(cursor);

  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


static int test_core_single(void) {
  PyObject *inner = PyLong_FromLong(5000);
  PyObject *val = make_datum(inner);
  PyObject *cursor = KosMatchCursor_Single(val);
  PyObject *span = NULL;
  PyObject *next = NULL;

  TEST_ASSERT_NOT_NULL(cursor, "single");
  TEST_ASSERT_EQ(KosMatchCursor_KIND(cursor), KOS_MATCH_SINGLE, "the kind");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 1, "one element");
  TEST_ASSERT_REFCNT(val, 2, "the cursor holds the value");

  PyObject *elem = core_pull(&cursor);
  TEST_ASSERT_SAME(elem, val, "the value itself, not read through Datum");
  Py_DECREF(elem);
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 0, "none remain");
  TEST_ASSERT_NULL(core_pull(&cursor), "a pull at the end misses");
  Py_DECREF(cursor);

  cursor = KosMatchCursor_Single(val);
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 1, 0, 1, &span, &next), 1,
                 "an empty tail before a suffix of one");
  TEST_ASSERT(PyTuple_CheckExact(span) && PyTuple_GET_SIZE(span) == 0,
              "the empty tuple");
  Py_DECREF(span);
  Py_DECREF(next);
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 0, 0, 1, &span, &next), 1,
                 "the whole tail");
  TEST_ASSERT(PyTuple_CheckExact(span) && PyTuple_GET_SIZE(span) == 1
              && PyTuple_GET_ITEM(span, 0) == val, "a tuple of the value");
  Py_DECREF(span);
  Py_DECREF(next);
  Py_DECREF(cursor);

  TEST_ASSERT_REFCNT(val, 1, "released");
  Py_DECREF(val);
  Py_DECREF(inner);
  return 0;
}


static int test_core_list_read_live(void) {
  PyObject *subj = Py_BuildValue("[iiii]", 1, 2, 3, 4);
  PyObject *cursor = NULL;
  PyObject *span = NULL;
  PyObject *next = NULL;
  PyObject *elem;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  PyObject *start = KosMatchMark_New(cursor, 0);
  TEST_ASSERT_NOT_NULL(start, "mark");
  elem = core_pull(&cursor);
  Py_XDECREF(elem);
  PyObject *mid = KosMatchMark_New(cursor, 0);
  TEST_ASSERT_NOT_NULL(mid, "mark");
  elem = core_pull(&cursor);
  Py_XDECREF(elem);

  /* user code empties the list under the walk */
  TEST_ASSERT_EQ(PyList_SetSlice(subj, 0, 4, NULL), 0, "clear");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 0, "read live");
  TEST_ASSERT_NULL(core_pull(&cursor), "a pull misses");
  TEST_ASSERT(! PyErr_Occurred(), "a miss");

  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 0, 0, 1, &span, &next), 1,
                 "the tail of what is left");
  TEST_ASSERT(PyTuple_CheckExact(span) && PyTuple_GET_SIZE(span) == 0,
              "nothing is left");
  TEST_ASSERT_SAME(next, cursor, "moved in place");
  Py_DECREF(span);
  Py_DECREF(next);

  TEST_ASSERT_NULL(KosMatchMark_At(mid), "the element is gone");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  TEST_ASSERT_NULL(KosMatchMark_Span(start, cursor), "the span is gone");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");

  /* a list that grows is read live as well */
  PyObject *more = Py_BuildValue("[iiii]", 1, 2, 3, 9);
  TEST_ASSERT_EQ(PyList_SetSlice(subj, 0, 0, more), 0, "refill");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 2, "read live");
  elem = core_pull(&cursor);
  TEST_ASSERT_EQ(PyLong_AsLong(elem), 3, "the item now at the index");
  Py_DECREF(elem);
  Py_DECREF(more);

  Py_DECREF(mid);
  Py_DECREF(start);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


static int test_core_bound_and_sync(void) {
  PyObject *subj = Py_BuildValue("(iiiii)", 1, 2, 3, 4, 5);
  PyObject *cursor = NULL;
  PyObject *bound = NULL;
  PyObject *elem;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatchCursor_Bound(cursor, 6, &bound), 0,
                 "fewer than w remain");
  TEST_ASSERT_NULL(bound, "no cursor on a miss");
  TEST_ASSERT_EQ(KosMatchCursor_Bound(cursor, 2, &bound), 1, "bound");
  TEST_ASSERT_NOT_SAME(bound, cursor, "a cursor of its own");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(bound), 3, "ends two early");

  for (long i = 1; i <= 3; i++) {
    elem = core_pull(&bound);
    TEST_ASSERT_EQ(PyLong_AsLong(elem), i, "the element");
    Py_DECREF(elem);
  }
  TEST_ASSERT_NULL(core_pull(&bound), "a pull at the bound misses");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 5, "the parent stays");

  /* a bound inside a bound adds to its reserve */
  PyObject *inner = NULL;
  TEST_ASSERT_EQ(KosMatchCursor_Bound(cursor, 2, &inner), 1, "bound");
  PyObject *innermost = NULL;
  TEST_ASSERT_EQ(KosMatchCursor_Bound(inner, 1, &innermost), 1, "bound");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(innermost), 2, "ends three early");
  Py_DECREF(innermost);
  Py_DECREF(inner);

  PyObject *synced = KosMatchCursor_Sync(cursor, bound, 1);
  TEST_ASSERT_SAME(synced, cursor, "moved in place");
  Py_DECREF(synced);
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 2, "at the bound's end");
  elem = core_pull(&cursor);
  TEST_ASSERT_EQ(PyLong_AsLong(elem), 4, "the suffix");
  Py_DECREF(elem);
  Py_DECREF(bound);
  Py_DECREF(cursor);

  /* over a spine */
  PyObject *spine = make_spine(4);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatchCursor_Bound(cursor, 1, &bound), 1, "bound");
  for (long i = 1; i <= 3; i++) {
    elem = core_pull(&bound);
    TEST_ASSERT_NOT_NULL(elem, "pull");
    Py_DECREF(elem);
  }
  TEST_ASSERT_NULL(core_pull(&bound), "a pull at the bound misses");

  /* a cursor a cloned segment also holds syncs to a new cursor */
  Py_INCREF(cursor);
  synced = KosMatchCursor_Sync(cursor, bound, 1);
  TEST_ASSERT_NOT_SAME(synced, cursor, "a new cursor");
  TEST_ASSERT_SAME(((KosMatchCursor *) synced)->seq,
                   KosPair_CDR(KosPair_CDR(KosPair_CDR(spine))),
                   "at the bound's cell");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(synced), 1, "one left");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 4, "the held one stays");
  Py_DECREF(cursor);
  Py_DECREF(synced);
  Py_DECREF(bound);
  Py_DECREF(cursor);

  TEST_ASSERT_REFCNT(spine, 1, "released");
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(spine);
  Py_DECREF(subj);
  return 0;
}


static int test_core_tail(void) {
  PyObject *subj = Py_BuildValue("(iiiii)", 1, 2, 3, 4, 5);
  PyObject *cursor = NULL;
  PyObject *span = NULL;
  PyObject *next = NULL;
  PyObject *elem;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 5, 0, 1, &span, &next), 0,
                 "fewer than w remain");
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 1, 4, 1, &span, &next), 0,
                 "a span below min");
  TEST_ASSERT_NULL(span, "no span on a miss");
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 1, 3, 1, &span, &next), 1,
                 "tail");
  TEST_ASSERT(PyTuple_CheckExact(span) && PyTuple_GET_SIZE(span) == 3,
              "a tuple of three");
  TEST_ASSERT_EQ(PyLong_AsLong(PyTuple_GET_ITEM(span, 0)), 2, "from 2");
  TEST_ASSERT_SAME(next, cursor, "moved in place");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 1, "the suffix is left");
  Py_DECREF(span);
  Py_DECREF(next);
  Py_DECREF(cursor);

  /* a list's tail is a tuple, as the engine's REST binds it */
  PyObject *lst = Py_BuildValue("[iii]", 1, 2, 3);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(lst, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 0, 0, 1, &span, &next), 1,
                 "tail");
  TEST_ASSERT(PyTuple_CheckExact(span) && PyTuple_GET_SIZE(span) == 3,
              "a tuple");
  Py_DECREF(span);
  Py_DECREF(next);
  Py_DECREF(cursor);
  Py_DECREF(lst);

  /* a spine's tail to its end is the boxed sub-spine */
  PyObject *spine = make_spine(4);
  PyObject *second = KosPair_CDR(spine);
  PyObject *fourth = KosPair_CDR(KosPair_CDR(second));
  set_positions(spine);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 0, 0, 1, &span, &next), 1,
                 "tail");
  TEST_ASSERT(KosDatum_Check(span), "a Datum");
  TEST_ASSERT_SAME(((KosDatum *) span)->datum, second, "over the sub-spine");
  TEST_ASSERT_EQ(datum_line(span), 2, "at its first cell's position");
  TEST_ASSERT(KosNilP(((KosMatchCursor *) next)->seq), "at the end");
  Py_DECREF(span);
  Py_DECREF(next);
  Py_DECREF(cursor);

  /* before a suffix, a tuple of a Datum per cell */
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 1, 0, 1, &span, &next), 1,
                 "tail");
  TEST_ASSERT(PyTuple_CheckExact(span) && PyTuple_GET_SIZE(span) == 3,
              "three cells");
  for (Py_ssize_t i = 0; i < 3; i++) {
    PyObject *item = PyTuple_GET_ITEM(span, i);
    TEST_ASSERT(KosDatum_Check(item), "a Datum per cell");
    TEST_ASSERT_EQ(PyLong_AsLong(((KosDatum *) item)->datum), i + 1, "car");
    TEST_ASSERT_EQ(datum_line(item), i + 1, "the cell's position");
  }
  TEST_ASSERT_SAME(((KosMatchCursor *) next)->seq, fourth, "at the suffix");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(next), 1, "one left");
  Py_DECREF(span);
  Py_DECREF(next);

  /* an empty tail is the terminator, wherever it sits */
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 1, 0, 1, &span, &next), 1,
                 "an empty tail");
  TEST_ASSERT(KosDatum_Check(span), "a Datum");
  TEST_ASSERT(KosNilP(((KosDatum *) span)->datum), "over the terminator");
  TEST_ASSERT_SAME(((KosMatchCursor *) next)->seq, fourth, "not moved");
  Py_DECREF(span);
  Py_DECREF(next);
  Py_DECREF(cursor);

  TEST_ASSERT_REFCNT(spine, 1, "released");
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(spine);
  Py_DECREF(subj);
  return 0;
}


/* === the core marks === */


static int test_mark_scope_and_reset(void) {
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;
  PyObject *elem;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  PyObject *mark = KosMatchMark_New(cursor, 0);
  TEST_ASSERT_NOT_NULL(mark, "mark");
  TEST_ASSERT(KosMatchMark_Check(mark), "a mark");
  TEST_ASSERT_EQ(KosMatchMark_COUNT(mark), 0, "no slots");
  TEST_ASSERT_EQ(Py_SIZE(mark), KOS_MATCH_MARK_SLOTS, "default capacity");
  TEST_ASSERT(! PyObject_GC_IsTracked(mark), "a mark of no slots untracked");
  TEST_ASSERT_REFCNT(subj, 3, "the cursor and the mark hold the subject");

  elem = core_pull(&cursor);
  Py_DECREF(elem);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  PyObject *reset = KosMatchMark_Reset(cursor, mark, 1);
  TEST_ASSERT_SAME(reset, cursor, "moved in place");
  Py_DECREF(reset);
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 3, "back at the mark");

  elem = core_pull(&cursor);
  Py_DECREF(elem);
  Py_INCREF(cursor);
  reset = KosMatchMark_Reset(cursor, mark, 1);
  TEST_ASSERT_NOT_SAME(reset, cursor, "a shared cursor is not moved");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(reset), 3, "back at the mark");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 2, "the held one stays");
  Py_DECREF(cursor);
  Py_DECREF(reset);
  Py_DECREF(mark);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");

  /* over a spine, the mark holds the cell and the count */
  PyObject *spine = make_spine(3);
  PyObject *second = KosPair_CDR(spine);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  mark = KosMatchMark_New(cursor, 2);
  TEST_ASSERT_SAME(((KosMatchMark *) mark)->seq, second, "at the cell");
  TEST_ASSERT_EQ(((KosMatchMark *) mark)->index, 1, "one pulled");
  TEST_ASSERT(PyObject_GC_IsTracked(mark), "a mark with slots is tracked");
  TEST_ASSERT_NULL(KosMatchMark_SLOT(mark, 0), "unbound");
  TEST_ASSERT_NULL(KosMatchMark_SLOT(mark, 1), "unbound");
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 0, "at the end");
  reset = KosMatchMark_Reset(cursor, mark, 1);
  Py_DECREF(reset);
  TEST_ASSERT_SAME(((KosMatchCursor *) cursor)->seq, second, "the cell");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 2, "the count");

  Py_DECREF(mark);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(spine, 1, "released");
  Py_DECREF(spine);
  Py_DECREF(subj);
  return 0;
}


static int test_mark_at_and_span(void) {
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;
  PyObject *out;
  PyObject *elem;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  PyObject *mark = KosMatchMark_New(cursor, 0);
  out = KosMatchMark_Span(mark, cursor);
  TEST_ASSERT(PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 0,
              "an empty span is the empty tuple");
  Py_DECREF(out);
  out = KosMatchMark_At(mark);
  TEST_ASSERT_SAME(out, PyTuple_GET_ITEM(subj, 0), "the element");
  Py_DECREF(out);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  out = KosMatchMark_Span(mark, cursor);
  TEST_ASSERT_SAME(out, PyTuple_GET_ITEM(subj, 0), "one is the element");
  Py_DECREF(out);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  out = KosMatchMark_Span(mark, cursor);
  TEST_ASSERT(PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 2,
              "two are a tuple");
  Py_DECREF(out);
  PyObject *later = KosMatchMark_New(cursor, 0);
  PyObject *reset = KosMatchMark_Reset(cursor, mark, 1);
  Py_DECREF(reset);
  TEST_ASSERT_NULL(KosMatchMark_Span(later, cursor),
                   "a cursor behind the mark");
  TEST_ASSERT(clear_expected(PyExc_SystemError), "SystemError");
  Py_DECREF(later);
  Py_DECREF(mark);
  Py_DECREF(cursor);

  /* over a spine */
  PyObject *spine = make_spine(3);
  PyObject *second = KosPair_CDR(spine);
  PyObject *third = KosPair_CDR(second);
  set_positions(spine);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  mark = KosMatchMark_New(cursor, 0);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  elem = core_pull(&cursor);
  Py_DECREF(elem);

  out = KosMatchMark_At(mark);
  TEST_ASSERT(KosDatum_Check(out), "a Datum, as the pull gave it");
  TEST_ASSERT_EQ(PyLong_AsLong(((KosDatum *) out)->datum), 1, "the car");
  TEST_ASSERT_EQ(datum_line(out), 1, "the cell's position");
  Py_DECREF(out);

  out = KosMatchMark_Span(mark, cursor);
  TEST_ASSERT(PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 2,
              "two cells");
  TEST_ASSERT(KosDatum_Check(PyTuple_GET_ITEM(out, 1)), "a Datum per cell");
  TEST_ASSERT_EQ(datum_line(PyTuple_GET_ITEM(out, 1)), 2, "positions");
  Py_DECREF(out);

  /* user code cuts the spine between the mark and the cursor */
  Py_INCREF(second);
  KosPair_SETCDR(spine, KosNil);
  TEST_ASSERT_NULL(KosMatchMark_Span(mark, cursor), "the span is cut");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  out = KosMatchMark_At(mark);
  TEST_ASSERT_NOT_NULL(out, "the mark's own cell is still read");
  Py_DECREF(out);
  KosPair_SETCDR(spine, second);
  Py_DECREF(second);
  TEST_ASSERT_SAME(((KosMatchCursor *) cursor)->seq, third, "the cursor");
  Py_DECREF(mark);
  Py_DECREF(cursor);

  /* over a single value */
  PyObject *val = PyLong_FromLong(4000);
  cursor = KosMatchCursor_Single(val);
  mark = KosMatchMark_New(cursor, 0);
  out = KosMatchMark_At(mark);
  TEST_ASSERT_SAME(out, val, "the value");
  Py_DECREF(out);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  out = KosMatchMark_Span(mark, cursor);
  TEST_ASSERT_SAME(out, val, "a span of one is the value");
  Py_DECREF(out);
  Py_DECREF(mark);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(val, 1, "released");
  Py_DECREF(val);

  TEST_ASSERT_REFCNT(spine, 1, "released");
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(spine);
  Py_DECREF(subj);
  return 0;
}


static int test_mark_store_and_clear(void) {
  PyObject *subj = make_spine(2);
  PyObject *xname = PyUnicode_FromString("x");
  PyObject *yname = PyUnicode_FromString("y");
  PyObject *cursor = NULL;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  PyObject *mark = KosMatchMark_New(cursor, 3);
  TEST_ASSERT_EQ(KosMatchMark_COUNT(mark), 3, "three slots");

  /* a stored Datum takes the slot's name as its binding */
  PyObject *elem = core_pull(&cursor);
  Py_INCREF(elem);
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 0, elem, xname), 0, "store");
  TEST_ASSERT_SAME(KosMatchMark_SLOT(mark, 0), elem, "slot 0");
  TEST_ASSERT_SAME(((KosDatum *) elem)->binding, xname, "bound to x");
  TEST_ASSERT_REFCNT(elem, 2, "the slot took the stored reference");

  /* and each Datum item of a tuple */
  PyObject *one = PyLong_FromLong(1001);
  PyObject *a = make_datum(one);
  PyObject *b = make_datum(one);
  PyObject *pair = PyTuple_Pack(3, a, one, b);
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 1, pair, yname), 0, "store");
  TEST_ASSERT_SAME(((KosDatum *) a)->binding, yname, "item bound to y");
  TEST_ASSERT_SAME(((KosDatum *) b)->binding, yname, "item bound to y");

  /* no name, no binding */
  PyObject *c = make_datum(one);
  Py_INCREF(c);
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 2, c, NULL), 0, "store");
  TEST_ASSERT_SAME(((KosDatum *) c)->binding, Py_None, "unbound");

  /* a store over a bound slot releases what it held */
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 0, PyLong_FromLong(1002), NULL), 0,
                 "store over");
  TEST_ASSERT_REFCNT(elem, 1, "the old value released");
  Py_DECREF(elem);

  /* an index outside the slots is refused, the value released */
  PyObject *stray = PyLong_FromLong(1003);
  Py_INCREF(stray);
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 3, stray, NULL), -1, "refused");
  TEST_ASSERT(clear_expected(PyExc_SystemError), "SystemError");
  TEST_ASSERT_REFCNT(stray, 1, "consumed on the error path");
  Py_DECREF(stray);

  PyObject *slots = Py_BuildValue("(ii)", 0, 2);
  TEST_ASSERT_EQ(KosMatchMark_Clear(mark, slots), 0, "clear");
  TEST_ASSERT_NULL(KosMatchMark_SLOT(mark, 0), "slot 0 emptied");
  TEST_ASSERT_NOT_NULL(KosMatchMark_SLOT(mark, 1), "slot 1 kept");
  TEST_ASSERT_NULL(KosMatchMark_SLOT(mark, 2), "slot 2 emptied");
  TEST_ASSERT_REFCNT(c, 1, "released");
  TEST_ASSERT_EQ(KosMatchMark_Clear(mark, slots), 0, "clear of empty slots");
  Py_DECREF(slots);
  slots = Py_BuildValue("(i)", 5);
  TEST_ASSERT_EQ(KosMatchMark_Clear(mark, slots), -1, "refused");
  TEST_ASSERT(clear_expected(PyExc_SystemError), "SystemError");
  Py_DECREF(slots);

  TEST_ASSERT_REFCNT(pair, 1, "the slot holds the tuple");
  Py_DECREF(mark);
  TEST_ASSERT_REFCNT(a, 1, "the mark released the tuple");
  Py_DECREF(c);
  Py_DECREF(b);
  Py_DECREF(a);
  Py_DECREF(one);
  Py_DECREF(cursor);
  Py_DECREF(yname);
  Py_DECREF(xname);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


static int test_mark_writable_copies_when_shared(void) {
  PyObject *subj = Py_BuildValue("(ii)", 1, 2);
  PyObject *val = PyLong_FromLong(2001);
  PyObject *other = PyLong_FromLong(2002);
  PyObject *cursor = NULL;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  PyObject *mark = KosMatchMark_New(cursor, 2);
  Py_INCREF(val);
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 0, val, NULL), 0, "store");

  /* the stack entry alone holds it: the mark itself */
  PyObject *w = KosMatchMark_Writable(mark, 1);
  TEST_ASSERT_SAME(w, mark, "unshared");
  TEST_ASSERT_REFCNT(mark, 2, "a new reference to it");
  Py_DECREF(w);

  /* a cloned segment holds it too: a copy */
  Py_INCREF(mark);
  w = KosMatchMark_Writable(mark, 1);
  TEST_ASSERT_NOT_SAME(w, mark, "shared");
  TEST_ASSERT(KosMatchMark_Check(w), "a mark");
  TEST_ASSERT_REFCNT(w, 1, "the copy is the caller's");
  TEST_ASSERT_EQ(KosMatchMark_COUNT(w), 2, "the same slot count");
  TEST_ASSERT_SAME(((KosMatchMark *) w)->seq, subj, "the same position");
  TEST_ASSERT_EQ(((KosMatchMark *) w)->index, 0, "the same index");
  TEST_ASSERT_SAME(KosMatchMark_SLOT(w, 0), val, "the same values");
  TEST_ASSERT_REFCNT(val, 3, "held by both marks");
  TEST_ASSERT(PyObject_GC_IsTracked(w), "tracked");

  Py_INCREF(other);
  TEST_ASSERT_EQ(KosMatchMark_Store(w, 1, other, NULL), 0, "store");
  TEST_ASSERT_EQ(KosMatchMark_Store(w, 0, PyLong_FromLong(2003), NULL), 0,
                 "store over");
  TEST_ASSERT_NULL(KosMatchMark_SLOT(mark, 1), "the original is unwritten");
  TEST_ASSERT_SAME(KosMatchMark_SLOT(mark, 0), val, "and keeps its value");
  TEST_ASSERT_REFCNT(val, 2, "the copy released its reference");

  Py_DECREF(w);
  TEST_ASSERT_REFCNT(other, 1, "released with the copy");
  Py_DECREF(mark);
  Py_DECREF(mark);
  TEST_ASSERT_REFCNT(val, 1, "released with the mark");

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(other);
  Py_DECREF(val);
  Py_DECREF(subj);
  return 0;
}


static int test_mark_captures(void) {
  PyObject *names = Py_BuildValue("(ss)", "a", "b");
  PyObject *short_names = Py_BuildValue("(s)", "a");
  PyObject *val = PyList_New(0);
  PyObject *cursor = NULL;
  PyObject *mark;
  PyObject *out;
  PyObject *elem;

  /* over a tuple and over a list, a plain dict of the bound slots */
  PyObject *subjects[2] = {
    Py_BuildValue("(ii)", 1, 2),
    Py_BuildValue("[ii]", 1, 2),
  };
  for (int s = 0; s < 2; s++) {
    TEST_ASSERT_EQ(KosMatchCursor_Descend(subjects[s], &cursor), 1, "desc");
    mark = KosMatchMark_New(cursor, 2);
    elem = core_pull(&cursor);
    Py_DECREF(elem);
    Py_INCREF(val);
    TEST_ASSERT_EQ(KosMatchMark_Store(mark, 0, val, NULL), 0, "store");

    out = KosMatchMark_Captures(mark, cursor, names);
    TEST_ASSERT_NOT_NULL(out, "captures");
    TEST_ASSERT(PyDict_CheckExact(out), "a plain dict");
    TEST_ASSERT_EQ(PyDict_GET_SIZE(out), 1, "the unbound slot is absent");
    TEST_ASSERT_SAME(PyDict_GetItemString(out, "a"), val, "bound under a");
    Py_DECREF(out);

    TEST_ASSERT_NULL(KosMatchMark_Captures(mark, cursor, short_names),
                     "a name per slot");
    TEST_ASSERT(clear_expected(PyExc_SystemError), "SystemError");

    Py_DECREF(mark);
    Py_DECREF(cursor);
    TEST_ASSERT_REFCNT(subjects[s], 1, "released");
    Py_DECREF(subjects[s]);
  }

  /* over a spine, a DatumFrame over the span */
  PyObject *spine = make_spine(3);
  PyObject *second = KosPair_CDR(spine);
  set_positions(spine);

  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  mark = KosMatchMark_New(cursor, 2);
  elem = core_pull(&cursor);
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 1, elem, NULL), 0, "store");
  elem = core_pull(&cursor);
  Py_DECREF(elem);

  out = KosMatchMark_Captures(mark, cursor, names);
  TEST_ASSERT_NOT_NULL(out, "captures");
  TEST_ASSERT(KosDatumFrame_Check(out), "a DatumFrame");
  TEST_ASSERT_EQ(PyDict_GET_SIZE(out), 1, "the unbound slot is absent");
  TEST_ASSERT_SAME(PyDict_GetItemString(out, "b"), KosMatchMark_SLOT(mark, 1),
                   "bound under b");
  TEST_ASSERT_SAME(((KosDatumFrame *) out)->source, second,
                   "a span to the end: its first cell");
  TEST_ASSERT_SAME(((KosDatumFrame *) out)->position,
                   KosPair_POSITION(second), "the first cell's position");
  TEST_ASSERT_REFCNT(mark, 1, "the frame does not hold the mark");
  Py_DECREF(out);

  out = KosMatchMark_Dict(mark, names);
  TEST_ASSERT(PyDict_CheckExact(out), "a pass's captures are a plain dict");
  Py_DECREF(out);
  Py_DECREF(mark);
  Py_DECREF(cursor);

  /* a span short of the end: the cars, read through Datum */
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  mark = KosMatchMark_New(cursor, 2);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  out = KosMatchMark_Captures(mark, cursor, names);
  TEST_ASSERT_EQ(PyLong_AsLong(((KosDatumFrame *) out)->source), 1,
                 "one cell: its car");
  TEST_ASSERT_SAME(((KosDatumFrame *) out)->position,
                   KosPair_POSITION(spine), "the first cell's position");
  Py_DECREF(out);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  out = KosMatchMark_Captures(mark, cursor, names);
  PyObject *source = ((KosDatumFrame *) out)->source;
  TEST_ASSERT(PyTuple_CheckExact(source) && PyTuple_GET_SIZE(source) == 2,
              "two cells: a tuple of their cars");
  TEST_ASSERT_EQ(PyLong_AsLong(PyTuple_GET_ITEM(source, 1)), 2, "the cars");
  Py_DECREF(out);

  /* user code cuts the span */
  Py_INCREF(second);
  KosPair_SETCDR(spine, KosNil);
  TEST_ASSERT_NULL(KosMatchMark_Captures(mark, cursor, names), "cut");
  TEST_ASSERT(clear_expected(PyExc_ValueError), "ValueError");
  KosPair_SETCDR(spine, second);
  Py_DECREF(second);

  Py_DECREF(mark);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(spine, 1, "released");
  TEST_ASSERT_REFCNT(val, 1, "released");
  Py_DECREF(spine);
  Py_DECREF(val);
  Py_DECREF(short_names);
  Py_DECREF(names);
  return 0;
}


static int test_mark_above_default_capacity(void) {
  PyObject *subj = Py_BuildValue("(i)", 1);
  PyObject *val = PyLong_FromLong(3001);
  PyObject *names = PyTuple_New(20);
  PyObject *cursor = NULL;

  for (Py_ssize_t i = 0; i < 20; i++)
    PyTuple_SET_ITEM(names, i, PyUnicode_FromFormat("s%zd", i));

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  PyObject *big = KosMatchMark_New(cursor, 20);
  TEST_ASSERT_NOT_NULL(big, "mark");
  TEST_ASSERT_EQ(Py_SIZE(big), 20, "allocated at its count");
  TEST_ASSERT_EQ(KosMatchMark_COUNT(big), 20, "twenty slots");
  for (Py_ssize_t i = 0; i < 20; i++)
    TEST_ASSERT_NULL(KosMatchMark_SLOT(big, i), "unbound");

  Py_INCREF(val);
  TEST_ASSERT_EQ(KosMatchMark_Store(big, 19, val, NULL), 0, "the last slot");
  PyObject *out = KosMatchMark_Captures(big, cursor, names);
  TEST_ASSERT_EQ(PyDict_GET_SIZE(out), 1, "one bound");
  TEST_ASSERT_SAME(PyDict_GetItemString(out, "s19"), val, "under s19");
  Py_DECREF(out);

  Py_INCREF(big);
  PyObject *copy = KosMatchMark_Writable(big, 1);
  TEST_ASSERT_NOT_SAME(copy, big, "a copy");
  TEST_ASSERT_EQ(Py_SIZE(copy), 20, "at the same size");
  TEST_ASSERT_SAME(KosMatchMark_SLOT(copy, 19), val, "the same values");
  Py_DECREF(copy);
  Py_DECREF(big);
  Py_DECREF(big);

  /* a released large mark is never handed out as a default one */
  PyObject *small = KosMatchMark_New(cursor, 0);
  TEST_ASSERT_EQ(Py_SIZE(small), KOS_MATCH_MARK_SLOTS, "default capacity");
  Py_DECREF(small);

  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(val, 1, "released");
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(names);
  Py_DECREF(val);
  Py_DECREF(subj);
  return 0;
}


static int test_mark_freelist_reuse(void) {
  PyObject *subj = Py_BuildValue("(i)", 1);
  PyObject *cursor = NULL;
  PyObject *drain[8];

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");

  /* take what earlier tests left on the freelist, so the next release
     has room */
  for (int i = 0; i < 8; i++)
    drain[i] = KosMatchMark_New(cursor, 0);

  PyObject *mark = KosMatchMark_New(cursor, 4);
  TEST_ASSERT(PyObject_GC_IsTracked(mark), "tracked with slots");
  void *was = mark;
  Py_DECREF(mark);

  mark = KosMatchMark_New(cursor, 0);
#ifndef KOS_NOFREELIST
  TEST_ASSERT_SAME(mark, was, "freelist reuse (LIFO)");
#else
  (void) was;
#endif
  TEST_ASSERT_EQ(KosMatchMark_COUNT(mark), 0, "the new count");
  TEST_ASSERT_EQ(Py_SIZE(mark), KOS_MATCH_MARK_SLOTS, "default capacity");
  TEST_ASSERT(! PyObject_GC_IsTracked(mark), "untracked without slots");
  TEST_ASSERT_REFCNT(mark, 1, "a fresh reference");
  Py_DECREF(mark);

  mark = KosMatchMark_New(cursor, KOS_MATCH_MARK_SLOTS);
  TEST_ASSERT_EQ(Py_SIZE(mark), KOS_MATCH_MARK_SLOTS, "a full default mark");
  for (Py_ssize_t i = 0; i < KOS_MATCH_MARK_SLOTS; i++)
    TEST_ASSERT_NULL(KosMatchMark_SLOT(mark, i), "unbound");
  Py_DECREF(mark);

  for (int i = 0; i < 8; i++)
    Py_DECREF(drain[i]);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);
  return 0;
}


/**
   A pass dict of k1 to v1 and, when n is 2, k2 to v2. New reference.
*/
static PyObject *make_pass(
    int n,
    const char *k1,
    long v1,
    const char *k2,
    long v2) {

  PyObject *d = PyDict_New();
  PyObject *v = PyLong_FromLong(v1);

  PyDict_SetItemString(d, k1, v);
  Py_DECREF(v);
  if (n > 1) {
    v = PyLong_FromLong(v2);
    PyDict_SetItemString(d, k2, v);
    Py_DECREF(v);
  }
  return d;
}


static int test_mark_columns(void) {
  PyObject *subj = Py_BuildValue("(i)", 1);
  PyObject *names = Py_BuildValue("(sss)", "a", "b", "c");
  PyObject *slots = Py_BuildValue("(iii)", 0, 1, 2);
  PyObject *keep = PyLong_FromLong(4001);
  PyObject *cursor = NULL;
  PyObject *col;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  PyObject *mark = KosMatchMark_New(cursor, 4);

  /* every pass binds a; the second leaves b unbound; none binds c */
  PyObject *first = make_pass(2, "a", 4011, "b", 4012);
  PyObject *second = make_pass(1, "a", 4021, NULL, 0);
  PyObject *items = PyList_New(0);
  PyList_Append(items, first);
  PyList_Append(items, second);
  Py_INCREF(items);   /* a cloned segment's reference */

  Py_INCREF(keep);
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 2, keep, NULL), 0, "store");
  TEST_ASSERT_EQ(KosMatchMark_Columns(mark, items, names, slots), 0,
                 "columns");

  col = KosMatchMark_SLOT(mark, 0);
  TEST_ASSERT(col && PyList_CheckExact(col) && PyList_GET_SIZE(col) == 2,
              "a value per pass");
  TEST_ASSERT_SAME(PyList_GET_ITEM(col, 0), PyDict_GetItemString(first, "a"),
                   "the first pass's value");
  TEST_ASSERT_SAME(PyList_GET_ITEM(col, 1), PyDict_GetItemString(second, "a"),
                   "the second pass's value");

  col = KosMatchMark_SLOT(mark, 1);
  TEST_ASSERT(col && PyList_CheckExact(col) && PyList_GET_SIZE(col) == 1,
              "only the passes that bound b");
  TEST_ASSERT_SAME(PyList_GET_ITEM(col, 0), PyDict_GetItemString(first, "b"),
                   "the first pass's value");

  col = KosMatchMark_SLOT(mark, 2);
  TEST_ASSERT(col && PyList_CheckExact(col) && PyList_GET_SIZE(col) == 0,
              "no pass bound c: the empty list");
  TEST_ASSERT_REFCNT(keep, 1, "the replaced value released");
  TEST_ASSERT_NULL(KosMatchMark_SLOT(mark, 3), "an unnamed slot untouched");
  TEST_ASSERT_REFCNT(items, 2, "the items list is read, not held");
  TEST_ASSERT_EQ(PyList_GET_SIZE(items), 2, "nor written");

  /* no passes: an empty list for every name */
  PyObject *none = PyList_New(0);
  TEST_ASSERT_EQ(KosMatchMark_Columns(mark, none, names, slots), 0,
                 "columns");
  for (Py_ssize_t i = 0; i < 3; i++) {
    col = KosMatchMark_SLOT(mark, i);
    TEST_ASSERT(col && PyList_CheckExact(col) && PyList_GET_SIZE(col) == 0,
                "an empty list per name");
  }

  PyObject *one_slot = Py_BuildValue("(i)", 0);
  TEST_ASSERT_EQ(KosMatchMark_Columns(mark, items, names, one_slot), -1,
                 "a slot per name");
  TEST_ASSERT(clear_expected(PyExc_SystemError), "SystemError");
  Py_DECREF(one_slot);

  Py_DECREF(mark);
  TEST_ASSERT_REFCNT(first, 2, "the items list's and ours");
  Py_DECREF(none);
  Py_DECREF(items);
  Py_DECREF(items);
  Py_DECREF(second);
  Py_DECREF(first);
  Py_DECREF(cursor);
  Py_DECREF(keep);
  Py_DECREF(slots);
  Py_DECREF(names);
  Py_DECREF(subj);
  return 0;
}


static int test_mark_pass(void) {
  PyObject *names = Py_BuildValue("(s)", "x");
  PyObject *subj = Py_BuildValue("(iii)", 4101, 4102, 4103);
  PyObject *cursor = NULL;
  PyObject *elem;
  PyObject *out;

  /* no slot bound: the span, one element as itself, more as a tuple */
  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  PyObject *mark = KosMatchMark_New(cursor, 1);
  elem = core_pull(&cursor);
  out = KosMatchMark_Pass(mark, cursor, names);
  TEST_ASSERT_SAME(out, elem, "one element: the element");
  Py_DECREF(out);

  PyObject *second = core_pull(&cursor);
  out = KosMatchMark_Pass(mark, cursor, names);
  TEST_ASSERT(out && PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 2,
              "two elements: a tuple");
  TEST_ASSERT_SAME(PyTuple_GET_ITEM(out, 1), second, "the elements");
  Py_DECREF(out);
  Py_DECREF(second);

  /* a bound slot: the plain dict of the bound slots */
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 0, elem, NULL), 0, "store");
  out = KosMatchMark_Pass(mark, cursor, names);
  TEST_ASSERT(out && PyDict_CheckExact(out), "a plain dict");
  TEST_ASSERT_SAME(PyDict_GetItemString(out, "x"), KosMatchMark_SLOT(mark, 0),
                   "bound under x");
  Py_DECREF(out);
  Py_DECREF(mark);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");

  /* over a spine, the span is boxed */
  PyObject *spine = make_spine(2);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  mark = KosMatchMark_New(cursor, 1);
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  out = KosMatchMark_Pass(mark, cursor, names);
  TEST_ASSERT(KosDatum_Check(out), "a Datum");
  TEST_ASSERT_EQ(PyLong_AsLong(((KosDatum *) out)->datum), 1, "the car");
  Py_DECREF(out);
  Py_DECREF(mark);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(spine, 1, "released");

  Py_DECREF(spine);
  Py_DECREF(subj);
  Py_DECREF(names);
  return 0;
}


/* === runner === */


int run_matcher_tests(void) {
  BEGIN_TEST_SUITE("Matcher Step Tests");

  RUN_TEST(test_sequence_walk);
  RUN_TEST(test_descend_gate_and_widths);
  RUN_TEST(test_spine_walk);
  RUN_TEST(test_box_reads_a_boxed_car_once);
  RUN_TEST(test_rest_over_a_spine);
  RUN_TEST(test_rest_over_a_sequence);
  RUN_TEST(test_list_shrinks_under_the_walk);
  RUN_TEST(test_list_subclass_shrinks_under_the_walk);
  RUN_TEST(test_looping_spine_is_refused);
  RUN_TEST(test_collect_over_a_spine);
  RUN_TEST(test_advance_under_a_mark);
  RUN_TEST(test_mark_remark_and_reset);
  RUN_TEST(test_remark_and_reset_refuse_a_foreign_mark);

  RUN_TEST(test_captures_over_a_sequence);
  RUN_TEST(test_captures_over_a_spine);
  RUN_TEST(test_span_looping_behind_the_cursor);
  RUN_TEST(test_iteration_values);

  RUN_TEST(test_core_types_through_the_box);
  RUN_TEST(test_literal_and_same_through_the_box);
  RUN_TEST(test_mapping_through_the_box);
  RUN_TEST(test_class_and_fields_through_the_box);

  RUN_TEST(test_core_descend_gate);
  RUN_TEST(test_core_next_over_a_sequence);
  RUN_TEST(test_core_next_over_a_spine);
  RUN_TEST(test_core_spine_edited_ahead);
  RUN_TEST(test_core_single);
  RUN_TEST(test_core_list_read_live);
  RUN_TEST(test_core_bound_and_sync);
  RUN_TEST(test_core_tail);

  RUN_TEST(test_mark_scope_and_reset);
  RUN_TEST(test_mark_at_and_span);
  RUN_TEST(test_mark_store_and_clear);
  RUN_TEST(test_mark_writable_copies_when_shared);
  RUN_TEST(test_mark_captures);
  RUN_TEST(test_mark_above_default_capacity);
  RUN_TEST(test_mark_freelist_reuse);
  RUN_TEST(test_mark_columns);
  RUN_TEST(test_mark_pass);

  return 0;
}


// The end.
