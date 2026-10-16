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
   ASan and Valgrind.

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

    /* the handler stores next into the cursor slot and clears the
       step; the step's reference to next moves to the slot */
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
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 0, "a pull past the live end misses");
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
  TEST_ASSERT_EQ(KosMatch_Available(a, 5), 1, "five are available around the loop");

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


static int test_position_and_reset(void) {
  PyObject *subj = Py_BuildValue("(iii)", 1, 2, 3);
  PyObject *cursor = NULL;
  PyObject *out = NULL;
  KosMatchStep step;

  TEST_ASSERT_EQ(KosMatch_Descend(subj, ANY_WIDTH, &cursor), 1, "descend");
  PyObject *mark = KosMatch_Position(cursor);
  TEST_ASSERT_SAME(mark, cursor, "the cursor itself");
  TEST_ASSERT_REFCNT(cursor, 2, "a new reference");

  /* under a mark the advance replaces: the mark keeps its position */
  TEST_ASSERT_EQ(KosMatch_Pull(cursor, &step), 1, "pull");
  PyObject *advanced = KosMatch_StepAdvance(&step);
  KosMatch_StepClear(&step);
  TEST_ASSERT_NOT_NULL(advanced, "advanced");
  TEST_ASSERT(advanced != cursor, "a held cursor advances by replacement");
  TEST_ASSERT(KosMatchCursor_Check(advanced), "a sequence cursor");
  TEST_ASSERT_EQ(((KosMatchCursor *) advanced)->index, 1, "past one");
  TEST_ASSERT_EQ(((KosMatchCursor *) cursor)->index, 0, "the mark untouched");
  TEST_ASSERT_SAME(((KosMatchCursor *) advanced)->seq, subj, "same subject");

  out = KosMatch_Reset(mark);
  TEST_ASSERT_SAME(out, cursor, "the saved cursor is the cursor again");
  Py_DECREF(out);
  Py_DECREF(advanced);
  Py_DECREF(mark);
  Py_DECREF(cursor);
  TEST_ASSERT_REFCNT(subj, 1, "released");
  Py_DECREF(subj);

  /* a spine: the cell is the position, and the reset hands it back */
  PyObject *spine = make_spine(2);
  mark = KosMatch_Position(spine);
  TEST_ASSERT_SAME(mark, spine, "the cell itself");
  TEST_ASSERT_REFCNT(spine, 2, "a new reference");
  out = KosMatch_Reset(mark);
  TEST_ASSERT_SAME(out, spine, "the saved cell is the cursor again");
  Py_DECREF(out);
  Py_DECREF(mark);
  TEST_ASSERT_REFCNT(spine, 1, "released");
  Py_DECREF(spine);
  return 0;
}


/* === captures === */


static int test_captures_over_a_sequence(void) {
  PyObject *subj = Py_BuildValue("(ii)", 1, 2);
  PyObject *names = Py_BuildValue("(ss)", "a", "b");
  PyObject *bound = PyList_New(0);
  PyObject *values[2] = { bound, NULL };
  PyObject *start = PyLong_FromLong(0);
  PyObject *cursor = NULL;

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
  TEST_ASSERT(PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 2, "a tuple");
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
  TEST_ASSERT(PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 3, "a slice");
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

  TEST_ASSERT_EQ(KosMatch_FieldPos(boxed, cls, 1, &out), 1, "field by position");
  TEST_ASSERT_EQ(PyLong_AsLong(out), 2, "y");
  Py_DECREF(out);
  TEST_ASSERT_EQ(KosMatch_FieldPos(boxed, cls, 2, &out), -1, "past the match args");
  TEST_ASSERT(clear_expected(PyExc_TypeError), "TypeError");

  TEST_ASSERT_EQ(KosMatch_FieldKw(boxed, yname, &out), 1, "field by name");
  TEST_ASSERT_EQ(PyLong_AsLong(out), 2, "y");
  Py_DECREF(out);
  TEST_ASSERT_EQ(KosMatch_FieldKw(boxed, zname, &out), 0, "a missing field misses");

  Py_DECREF(zname);
  Py_DECREF(yname);
  Py_DECREF(boxed);
  Py_DECREF(env);
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
  RUN_TEST(test_looping_spine_is_refused);
  RUN_TEST(test_collect_over_a_spine);
  RUN_TEST(test_position_and_reset);

  RUN_TEST(test_captures_over_a_sequence);
  RUN_TEST(test_captures_over_a_spine);
  RUN_TEST(test_iteration_values);

  RUN_TEST(test_core_types_through_the_box);
  RUN_TEST(test_literal_and_same_through_the_box);
  RUN_TEST(test_mapping_through_the_box);
  RUN_TEST(test_class_and_fields_through_the_box);

  return 0;
}


// The end.
