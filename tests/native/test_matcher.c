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

   Native tests for the matcher's step helpers: the element tests read
   through a Datum box, the cursor steps over a sequence, a pair spine
   and a single value, and the marks. Each step is checked for its hit
   and miss and the references it takes and releases, the copy of a
   shared mark, and the captures over a sequence and a spine; the
   subjects that read past a sequence or loop a spine run here under
   ASan and Valgrind.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


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
  PyObject *frame = PyObject_CallNoArgs((PyObject *) &KosDatumFrameType);
  TEST_ASSERT_NOT_NULL(frame, "frame");
  TEST_ASSERT_EQ(KosMatch_SameTest(frame, frame), 0, "a frame is not same");
  Py_DECREF(frame);

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

  TEST_ASSERT_EQ(KosMatch_FieldPos(boxed, cls, 1, NULL, &out), 1,
                 "field by position");
  TEST_ASSERT_EQ(PyLong_AsLong(out), 2, "y");
  Py_DECREF(out);
  TEST_ASSERT_EQ(
      KosMatch_FieldPos(boxed, cls, 1, KosMatch_PlainMatchArgs(cls), &out),
      1, "field by position through the given tuple");
  TEST_ASSERT_EQ(PyLong_AsLong(out), 2, "y");
  Py_DECREF(out);
  TEST_ASSERT_EQ(KosMatch_FieldPos(boxed, cls, 2, NULL, &out), -1,
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


static int test_class_test_through_the_box(void) {
  PyObject *env = PyDict_New();

  TEST_ASSERT_NOT_NULL(env, "env");
  PyObject *ran = PyRun_String(
      "class P:\n"
      "    __match_args__ = ('x', 'y')\n"
      "class Q:\n"
      "    pass\n"
      "class R:\n"
      "    __match_args__ = ['x']\n"
      "p = P()\n"
      "q = Q()\n"
      "r = R()\n",
      Py_file_input, env, env);
  if (! ran) {
    PyErr_Print();
    Py_DECREF(env);
    return -1;
  }
  Py_DECREF(ran);
  PyObject *pcls = PyDict_GetItemString(env, "P");
  PyObject *qcls = PyDict_GetItemString(env, "Q");
  PyObject *rcls = PyDict_GetItemString(env, "R");
  PyObject *boxed = make_datum(PyDict_GetItemString(env, "p"));
  PyObject *q = PyDict_GetItemString(env, "q");

  TEST_ASSERT_EQ(KosMatch_TypeTest(boxed, pcls), 1, "an instance");
  TEST_ASSERT_EQ(KosMatch_ClassArgs(pcls, 2, NULL), 0, "two positionals");
  TEST_ASSERT_EQ(KosMatch_ClassArgs(pcls, 0, NULL), 0, "no positionals");
  TEST_ASSERT_EQ(KosMatch_TypeTest(q, pcls), 0, "a non-instance misses");
  TEST_ASSERT(! PyErr_Occurred(), "no error on the miss");
  TEST_ASSERT_EQ(KosMatch_ClassArgs(pcls, 3, NULL), -1,
                 "past the match args");
  TEST_ASSERT(clear_expected(PyExc_TypeError), "TypeError");

  TEST_ASSERT_EQ(KosMatch_ClassArgs(qcls, 0, NULL), 0,
                 "no match args and no positionals");
  TEST_ASSERT_EQ(KosMatch_ClassArgs(qcls, 1, NULL), -1, "no match args");
  TEST_ASSERT(clear_expected(PyExc_TypeError), "TypeError");
  TEST_ASSERT_EQ(KosMatch_ClassArgs(rcls, 1, NULL), -1,
                 "match args not a tuple");
  TEST_ASSERT(clear_expected(PyExc_TypeError), "TypeError");

  PyObject *plain = KosMatch_PlainMatchArgs(pcls);
  TEST_ASSERT(plain == PyDict_GetItemString(
                  ((PyTypeObject *) pcls)->tp_dict, "__match_args__"),
              "the class dict's tuple");
  TEST_ASSERT_EQ(KosMatch_ClassArgs(pcls, 2, plain), 0,
                 "two positionals through the given tuple");
  TEST_ASSERT(KosMatch_PlainMatchArgs(qcls) == NULL, "no match args");
  TEST_ASSERT(KosMatch_PlainMatchArgs(rcls) == NULL, "a list");
  TEST_ASSERT(! PyErr_Occurred(), "no error from the plain reads");

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
   Pull one element from *cursor, its stack entry the only holder;
   a copy the step makes is stored back. The element as a new
   reference; NULL at the end or on an error.
*/
static PyObject *core_pull(PyObject **cursor) {
  PyObject *elem = NULL;
  PyObject *next = NULL;

  if (KosMatchCursor_Next(*cursor, 1, &elem, &next) != 1)
    return NULL;
  if (next != *cursor)
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
    TEST_ASSERT_REFCNT(cursor, 1, "no new reference");
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
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 0, 0, 1, &span, &next), 1,
                 "the whole tail");
  TEST_ASSERT(PyTuple_CheckExact(span) && PyTuple_GET_SIZE(span) == 1
              && PyTuple_GET_ITEM(span, 0) == val, "a tuple of the value");
  Py_DECREF(span);
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


static int test_core_unbound(void) {
  PyObject *subj = Py_BuildValue("(iiiii)", 1, 2, 3, 4, 5);
  PyObject *cursor = NULL;
  PyObject *bound = NULL;
  PyObject *inner = NULL;
  PyObject *elem;

  TEST_ASSERT_EQ(KosMatchCursor_Descend(subj, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatchCursor_Bound(cursor, 2, &bound), 1, "bound");
  TEST_ASSERT_EQ(KosMatchCursor_Bound(bound, 1, &inner), 1, "bound");
  elem = core_pull(&inner);
  TEST_ASSERT_EQ(PyLong_AsLong(elem), 1, "the element");
  Py_DECREF(elem);

  /* a copy at the same position, reading to the subject's end */
  PyObject *copy = KosMatchCursor_Unbound(inner);
  TEST_ASSERT_NOT_NULL(copy, "unbound");
  TEST_ASSERT_NOT_SAME(copy, inner, "a cursor of its own");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(copy), 4, "no reserve");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(inner), 1, "the bound one stays");
  for (long i = 2; i <= 5; i++) {
    elem = core_pull(&copy);
    TEST_ASSERT_EQ(PyLong_AsLong(elem), i, "past the bound");
    Py_DECREF(elem);
  }
  TEST_ASSERT_NULL(core_pull(&copy), "a pull at the end misses");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(inner), 1, "the bound one stays");
  Py_DECREF(copy);

  /* over an unbounded cursor, the same remaining elements */
  copy = KosMatchCursor_Unbound(cursor);
  TEST_ASSERT_NOT_NULL(copy, "unbound");
  TEST_ASSERT_NOT_SAME(copy, cursor, "a cursor of its own");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(copy), 5, "all of it");
  Py_DECREF(copy);
  Py_DECREF(inner);
  Py_DECREF(bound);
  Py_DECREF(cursor);

  /* over a spine */
  PyObject *spine = make_spine(4);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(spine, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatchCursor_Bound(cursor, 3, &bound), 1, "bound");
  elem = core_pull(&bound);
  TEST_ASSERT_NOT_NULL(elem, "pull");
  Py_DECREF(elem);
  TEST_ASSERT_NULL(core_pull(&bound), "a pull at the bound misses");
  copy = KosMatchCursor_Unbound(bound);
  TEST_ASSERT_NOT_NULL(copy, "unbound");
  TEST_ASSERT_SAME(((KosMatchCursor *) copy)->seq, KosPair_CDR(spine),
                   "at the bound's cell");
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(copy), 3, "past the bound");
  for (long i = 0; i < 3; i++) {
    elem = core_pull(&copy);
    TEST_ASSERT_NOT_NULL(elem, "pull");
    Py_DECREF(elem);
  }
  TEST_ASSERT_NULL(core_pull(&copy), "a pull at the end misses");
  Py_DECREF(copy);
  Py_DECREF(bound);
  Py_DECREF(cursor);

  /* over a single value */
  cursor = KosMatchCursor_Single(subj);
  copy = KosMatchCursor_Unbound(cursor);
  TEST_ASSERT_NOT_NULL(copy, "unbound");
  elem = core_pull(&copy);
  TEST_ASSERT_SAME(elem, subj, "the value");
  Py_DECREF(elem);
  TEST_ASSERT_EQ(KosMatchCursor_Remaining(cursor), 1, "the single stays");
  Py_DECREF(copy);
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
  Py_DECREF(cursor);

  /* a list's tail is a tuple, as the engine's REST binds it */
  PyObject *lst = Py_BuildValue("[iii]", 1, 2, 3);
  TEST_ASSERT_EQ(KosMatchCursor_Descend(lst, &cursor), 1, "descend");
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 0, 0, 1, &span, &next), 1,
                 "tail");
  TEST_ASSERT(PyTuple_CheckExact(span) && PyTuple_GET_SIZE(span) == 3,
              "a tuple");
  Py_DECREF(span);
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

  /* an empty tail is the terminator, wherever it sits */
  TEST_ASSERT_EQ(KosMatchCursor_Tail(cursor, 1, 0, 1, &span, &next), 1,
                 "an empty tail");
  TEST_ASSERT(KosDatum_Check(span), "a Datum");
  TEST_ASSERT(KosNilP(((KosDatum *) span)->datum), "over the terminator");
  TEST_ASSERT_SAME(((KosMatchCursor *) next)->seq, fourth, "not moved");
  Py_DECREF(span);
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
  TEST_ASSERT_SAME(KosMatchMark_Reset(cursor, mark, 1), cursor,
                   "moved in place");
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
  TEST_ASSERT_SAME(KosMatchMark_Reset(cursor, mark, 1), cursor,
                   "moved in place");
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
  TEST_ASSERT_REFCNT(mark, 1, "no new reference");

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

  /* a span to the end is its first cell, the cells after it unread,
     so a cut behind the cursor leaves it whole */
  elem = core_pull(&cursor);
  Py_DECREF(elem);
  Py_INCREF(second);
  KosPair_SETCDR(spine, KosNil);
  out = KosMatchMark_Captures(mark, cursor, names);
  TEST_ASSERT_NOT_NULL(out, "captures to the end");
  TEST_ASSERT_SAME(((KosDatumFrame *) out)->source, spine, "the first cell");
  Py_DECREF(out);
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
  out = KosMatchMark_Pass(mark, cursor, names, 0);
  TEST_ASSERT_SAME(out, elem, "one element: the element");
  Py_DECREF(out);

  PyObject *second = core_pull(&cursor);
  out = KosMatchMark_Pass(mark, cursor, names, 0);
  TEST_ASSERT(out && PyTuple_CheckExact(out) && PyTuple_GET_SIZE(out) == 2,
              "two elements: a tuple");
  TEST_ASSERT_SAME(PyTuple_GET_ITEM(out, 1), second, "the elements");
  Py_DECREF(out);
  Py_DECREF(second);

  /* a bound slot: the plain dict of the bound slots */
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 0, elem, NULL), 0, "store");
  out = KosMatchMark_Pass(mark, cursor, names, 0);
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
  out = KosMatchMark_Pass(mark, cursor, names, 0);
  TEST_ASSERT(KosDatum_Check(out), "a Datum");
  TEST_ASSERT_EQ(PyLong_AsLong(((KosDatum *) out)->datum), 1, "the car");

  /* a bound slot over a spine: a plain dict, or with captures set the
     DatumFrame MATCH_CAPTURES gives */
  TEST_ASSERT_EQ(KosMatchMark_Store(mark, 0, out, NULL), 0, "store");
  out = KosMatchMark_Pass(mark, cursor, names, 0);
  TEST_ASSERT(out && PyDict_CheckExact(out), "a plain dict");
  Py_DECREF(out);
  out = KosMatchMark_Pass(mark, cursor, names, 1);
  TEST_ASSERT(KosDatumFrame_Check(out), "a DatumFrame");
  TEST_ASSERT_SAME(PyDict_GetItemString(out, "x"), KosMatchMark_SLOT(mark, 0),
                   "bound under x");
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

  RUN_TEST(test_core_types_through_the_box);
  RUN_TEST(test_literal_and_same_through_the_box);
  RUN_TEST(test_mapping_through_the_box);
  RUN_TEST(test_class_and_fields_through_the_box);
  RUN_TEST(test_class_test_through_the_box);

  RUN_TEST(test_core_descend_gate);
  RUN_TEST(test_core_next_over_a_sequence);
  RUN_TEST(test_core_next_over_a_spine);
  RUN_TEST(test_core_spine_edited_ahead);
  RUN_TEST(test_core_single);
  RUN_TEST(test_core_list_read_live);
  RUN_TEST(test_core_bound_and_sync);
  RUN_TEST(test_core_unbound);
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
