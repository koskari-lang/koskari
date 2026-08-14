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
   Memory tests for operand stack segments

   Tests reference counting, push/pop operations, growth, the
   freelist, tail extraction, and deep copy for the segment API.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* Use large values to avoid Python's small integer cache */
#define TEST_VAL_1 10001
#define TEST_VAL_2 10002
#define TEST_VAL_3 10003


/* === push / pop === */


static int test_seg_push_ref(void) {
    /*
      PushRef increments the value's refcount; PopRef steals the
      segment's reference back to the caller.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "segment is empty");

    PyObject *value = PyLong_FromLong(TEST_VAL_1);
    TEST_ASSERT_NOT_NULL(value, "create value");
    Py_ssize_t initial = Py_REFCNT(value);

    int result = KosStack_PushRef(&seg, value);
    TEST_ASSERT_EQ(result, 0, "push succeeded");
    TEST_ASSERT_EQ(Py_REFCNT(value), initial + 1, "refcount incremented");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 1, "len is 1");

    PyObject *popped = KosStack_PopRef(&seg);
    TEST_ASSERT_SAME(popped, value, "popped same object");
    TEST_ASSERT_EQ(Py_REFCNT(value), initial + 1, "ref transferred to caller");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "segment empty again");

    Py_DECREF(popped);
    Py_DECREF(value);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_push_steal(void) {
    /*
      PUSH steals the caller's reference; no refcount change.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *value = PyLong_FromLong(TEST_VAL_2);
    TEST_ASSERT_NOT_NULL(value, "create value");
    Py_ssize_t initial = Py_REFCNT(value);

    Py_INCREF(value);
    int result = KosStack_PUSH(&seg, value);
    TEST_ASSERT_EQ(result, 0, "PUSH succeeded");
    TEST_ASSERT_EQ(Py_REFCNT(value), initial + 1, "ref moved, not copied");

    PyObject *popped = KosStack_PopRef(&seg);
    TEST_ASSERT_SAME(popped, value, "popped same object");
    Py_DECREF(popped);

    TEST_ASSERT_EQ(Py_REFCNT(value), initial, "balanced");
    Py_DECREF(value);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_lazy_create(void) {
    /*
      Pushing through a NULL slot lazily allocates the segment.
    */
    KosSegment *seg = NULL;

    PyObject *value = PyLong_FromLong(TEST_VAL_3);
    TEST_ASSERT_NOT_NULL(value, "create value");

    int result = KosStack_PushRef(&seg, value);
    TEST_ASSERT_EQ(result, 0, "push succeeded");
    TEST_ASSERT_NOT_NULL(seg, "segment allocated");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 1, "len is 1");

    Py_DECREF(value);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_pop_underrun(void) {
    /*
      PopRef on an empty segment errors; so does a NULL slot.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *popped = KosStack_PopRef(&seg);
    TEST_ASSERT_NULL(popped, "pop underrun fails");
    TEST_ASSERT(PyErr_Occurred() != NULL, "error set");
    PyErr_Clear();

    KosStack_Release(seg);

    KosSegment *none = NULL;
    popped = KosStack_PopRef(&none);
    TEST_ASSERT_NULL(popped, "pop from NULL slot fails");
    TEST_ASSERT(PyErr_Occurred() != NULL, "error set");
    PyErr_Clear();

    return 0;
}


/* === growth === */


static int test_seg_grow(void) {
    /*
      The 17th push grows the segment (16 -> 32); the slot is updated
      through the ** argument and all values survive.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");
    TEST_ASSERT_EQ(KosSegment_MAX(seg), 16, "birth size 16");

    for (int i = 0; i < 17; i++) {
        PyObject *val = PyLong_FromLong(10000 + i);
        TEST_ASSERT_NOT_NULL(val, "create value");
        int result = KosStack_PushRef(&seg, val);
        Py_DECREF(val);
        TEST_ASSERT_EQ(result, 0, "push succeeded");
    }

    TEST_ASSERT_EQ(KosSegment_LEN(seg), 17, "len is 17");
    TEST_ASSERT_EQ(KosSegment_MAX(seg), 32, "grew to 32");

    for (int i = 16; i >= 0; i--) {
        PyObject *popped = KosStack_PopRef(&seg);
        TEST_ASSERT_NOT_NULL(popped, "pop succeeded");
        TEST_ASSERT_EQ(PyLong_AsLong(popped), 10000 + i, "value intact");
        Py_DECREF(popped);
    }

    KosStack_Release(seg);
    return 0;
}


static int test_seg_grow_limit(void) {
    /*
      A segment holds up to 256 values; the 257th push errors.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    for (int i = 0; i < 256; i++) {
        PyObject *val = PyLong_FromLong(20000 + i);
        TEST_ASSERT_NOT_NULL(val, "create value");
        int result = KosStack_PushRef(&seg, val);
        Py_DECREF(val);
        TEST_ASSERT_EQ(result, 0, "push succeeded");
    }
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 256, "len is 256");
    TEST_ASSERT_EQ(KosSegment_MAX(seg), 256, "max is 256");

    PyObject *extra = PyLong_FromLong(TEST_VAL_1);
    TEST_ASSERT_NOT_NULL(extra, "create value");
    int result = KosStack_PushRef(&seg, extra);
    TEST_ASSERT_EQ(result, -1, "257th push fails");
    TEST_ASSERT(PyErr_Occurred() != NULL, "error set");
    PyErr_Clear();
    Py_DECREF(extra);

    KosStack_Release(seg);
    return 0;
}


/* === peek === */


static int test_seg_peek(void) {
    /*
      Peek borrows TOS; PeekAt addresses from bottom (0) or top (-1).
      Range check is len <= |index| in both directions, so for len 3
      the valid negatives are -1 and -2; -3 is out of range.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *popped = KosStack_Peek(seg);
    TEST_ASSERT_NULL(popped, "peek empty fails");
    PyErr_Clear();

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_2);
    PyObject *v3 = PyLong_FromLong(TEST_VAL_3);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v2);
    KosStack_PushRef(&seg, v3);

    Py_ssize_t before = Py_REFCNT(v3);
    PyObject *top = KosStack_Peek(seg);
    TEST_ASSERT_SAME(top, v3, "peek is TOS");
    TEST_ASSERT_EQ(Py_REFCNT(v3), before, "peek borrows");

    TEST_ASSERT_SAME(KosStack_PeekAt(seg, 0), v1, "index 0 is bottom");
    TEST_ASSERT_SAME(KosStack_PeekAt(seg, 1), v2, "index 1");
    TEST_ASSERT_SAME(KosStack_PeekAt(seg, -1), v3, "-1 is TOS");
    TEST_ASSERT_SAME(KosStack_PeekAt(seg, -2), v2, "-2 below TOS");

    TEST_ASSERT_NULL(KosStack_PeekAt(seg, 3), "index 3 out of range");
    PyErr_Clear();
    TEST_ASSERT_NULL(KosStack_PeekAt(seg, -3), "index -3 out of range");
    PyErr_Clear();

    Py_DECREF(v1);
    Py_DECREF(v2);
    Py_DECREF(v3);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_tail_ptr(void) {
    /*
      TailPtr points count elements back from top; count 0 returns
      NULL without error; over-count errors.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_2);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v2);

    PyObject *const *tail = KosStack_TailPtr(seg, 2);
    TEST_ASSERT_NOT_NULL(tail, "tail ptr");
    TEST_ASSERT_SAME(tail[0], v1, "tail[0]");
    TEST_ASSERT_SAME(tail[1], v2, "tail[1]");

    tail = KosStack_TailPtr(seg, 0);
    TEST_ASSERT_NULL(tail, "count 0 returns NULL");
    TEST_ASSERT(PyErr_Occurred() == NULL, "count 0 sets no error");

    tail = KosStack_TailPtr(seg, 3);
    TEST_ASSERT_NULL(tail, "over-count fails");
    PyErr_Clear();

    Py_DECREF(v1);
    Py_DECREF(v2);
    KosStack_Release(seg);
    return 0;
}


/* === drop === */


static int test_seg_drop(void) {
    /*
      Drop releases count references; over-drop errors without
      changing the segment. Drop1 releases exactly one.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t initial = Py_REFCNT(v1);
    for (int i = 0; i < 3; i++)
        KosStack_PushRef(&seg, v1);
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial + 3, "3 refs held");

    int result = KosStack_Drop(&seg, 4);
    TEST_ASSERT_EQ(result, -1, "over-drop fails");
    PyErr_Clear();
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 3, "len unchanged");

    result = KosStack_Drop(&seg, 2);
    TEST_ASSERT_EQ(result, 0, "drop 2");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial + 1, "2 refs released");

    result = KosStack_Drop1(&seg);
    TEST_ASSERT_EQ(result, 0, "drop 1");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "all refs released");

    result = KosStack_Drop1(&seg);
    TEST_ASSERT_EQ(result, -1, "drop1 underrun fails");
    PyErr_Clear();

    Py_DECREF(v1);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_drop_noerr(void) {
    /*
      DropNoErr drops what it can on over-drop, returns -1, sets no
      error; count 0 is a no-op returning 0.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    TEST_ASSERT_EQ(KosStack_DropNoErr(&seg, 0), 0, "count 0 ok");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t initial = Py_REFCNT(v1);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v1);

    int result = KosStack_DropNoErr(&seg, 5);
    TEST_ASSERT_EQ(result, -1, "over-drop reports -1");
    TEST_ASSERT(PyErr_Occurred() == NULL, "no error set");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "dropped what it had");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "refs released");

    Py_DECREF(v1);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_clear(void) {
    /*
      ClearBlock empties the segment in place, releasing every ref.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t initial = Py_REFCNT(v1);
    for (int i = 0; i < 4; i++)
        KosStack_PushRef(&seg, v1);

    int result = KosStack_Clear(&seg);
    TEST_ASSERT_EQ(result, 0, "clear succeeded");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "segment empty");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "refs released");

    Py_DECREF(v1);
    KosStack_Release(seg);
    return 0;
}


/* === duplicate === */


static int test_seg_duplicate(void) {
    /*
      Duplicate copies the top count elements, increfing each; a
      duplicate past capacity takes the grow path.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_2);
    Py_ssize_t initial = Py_REFCNT(v1);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v2);

    int result = KosStack_Duplicate(&seg, 2);
    TEST_ASSERT_EQ(result, 0, "duplicate succeeded");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 4, "len is 4");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial + 2, "extra ref held");
    TEST_ASSERT_SAME(KosStack_PeekAt(seg, 2), v1, "copy order");
    TEST_ASSERT_SAME(KosStack_PeekAt(seg, 3), v2, "copy order");

    result = KosStack_Duplicate(&seg, 5);
    TEST_ASSERT_EQ(result, -1, "over-duplicate fails");
    PyErr_Clear();

    /* fill to capacity boundary and duplicate across it */
    while (KosSegment_LEN(seg) < 15)
        KosStack_PushRef(&seg, v1);
    result = KosStack_Duplicate(&seg, 4);
    TEST_ASSERT_EQ(result, 0, "duplicate across grow");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 19, "len is 19");
    TEST_ASSERT_EQ(KosSegment_MAX(seg), 32, "grew to 32");

    Py_DECREF(v1);
    Py_DECREF(v2);
    KosStack_Release(seg);
    return 0;
}


/* === tail extraction === */


static int test_seg_pop_tail(void) {
    /*
      PopTail steals count elements into a tuple; count 0 gives an
      empty tuple; over-count errors.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_2);
    Py_ssize_t initial = Py_REFCNT(v1);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v2);

    PyObject *empty = KosStack_PopTail(&seg, 0);
    TEST_ASSERT_NOT_NULL(empty, "count 0 gives tuple");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(empty), 0, "empty tuple");
    Py_DECREF(empty);

    PyObject *tail = KosStack_PopTail(&seg, 2);
    TEST_ASSERT_NOT_NULL(tail, "pop tail");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "segment emptied");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(tail, 0), v1, "order kept");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(tail, 1), v2, "order kept");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial + 1, "ref moved to tuple");
    Py_DECREF(tail);
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "balanced");

    tail = KosStack_PopTail(&seg, 1);
    TEST_ASSERT_NULL(tail, "underrun fails");
    PyErr_Clear();

    Py_DECREF(v1);
    Py_DECREF(v2);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_pop_tail_ptr(void) {
    /*
      PopTailPtr steals count elements into a caller array, deepest
      first; count 0 is a no-op.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_2);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v2);

    TEST_ASSERT_EQ(KosStack_PopTailPtr(&seg, 0, NULL), 0, "count 0 ok");

    PyObject *dest[2] = { NULL, NULL };
    int result = KosStack_PopTailPtr(&seg, 2, dest);
    TEST_ASSERT_EQ(result, 0, "pop tail ptr");
    TEST_ASSERT_SAME(dest[0], v1, "deepest first");
    TEST_ASSERT_SAME(dest[1], v2, "TOS last");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "segment emptied");
    Py_DECREF(dest[0]);
    Py_DECREF(dest[1]);

    result = KosStack_PopTailPtr(&seg, 1, dest);
    TEST_ASSERT_EQ(result, -1, "underrun fails");
    PyErr_Clear();

    Py_DECREF(v1);
    Py_DECREF(v2);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_pop_tail_dicts(void) {
    /*
      PopTailDict consumes key,val pairs; PopTailDictNames consumes a
      names tuple at TOS plus values; PopTailDictFromNames takes the
      names tuple directly. Mismatched counts error.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *key = PyUnicode_FromString("test_key");
    PyObject *val = PyLong_FromLong(TEST_VAL_1);

    /* PopTailDict */
    KosStack_PushRef(&seg, key);
    KosStack_PushRef(&seg, val);
    PyObject *dict = KosStack_PopTailDict(&seg, 1);
    TEST_ASSERT_NOT_NULL(dict, "pop tail dict");
    TEST_ASSERT_EQ(PyDict_Size(dict), 1, "one entry");
    TEST_ASSERT_SAME(PyDict_GetItem(dict, key), val, "mapped");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "consumed");
    Py_DECREF(dict);

    /* PopTailDictNames */
    PyObject *names = PyTuple_Pack(1, key);
    KosStack_PushRef(&seg, val);
    KosStack_PushRef(&seg, names);
    dict = KosStack_PopTailDictNames(&seg, 1);
    TEST_ASSERT_NOT_NULL(dict, "pop tail dict names");
    TEST_ASSERT_SAME(PyDict_GetItem(dict, key), val, "mapped");
    Py_DECREF(dict);

    /* PopTailDictNames count mismatch */
    KosStack_PushRef(&seg, val);
    KosStack_PushRef(&seg, names);
    dict = KosStack_PopTailDictNames(&seg, 2);
    TEST_ASSERT_NULL(dict, "mismatch fails");
    PyErr_Clear();
    KosStack_Clear(&seg);

    /* PopTailDictFromNames */
    KosStack_PushRef(&seg, val);
    dict = KosStack_PopTailDictFromNames(&seg, names);
    TEST_ASSERT_NOT_NULL(dict, "pop tail dict from names");
    TEST_ASSERT_SAME(PyDict_GetItem(dict, key), val, "mapped");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "consumed");
    Py_DECREF(dict);

    Py_DECREF(names);
    Py_DECREF(key);
    Py_DECREF(val);
    KosStack_Release(seg);
    return 0;
}


/* === KosStackSteal === */


static int test_seg_steal_small(void) {
    /*
      A steal within the fast size uses the embedded array; refs move
      without churn; Free releases them.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t initial = Py_REFCNT(v1);
    for (int i = 0; i < 3; i++)
        KosStack_PushRef(&seg, v1);

    KosStackSteal steal;
    int result = KosStackSteal_Tail(&steal, 3, &seg);
    TEST_ASSERT_EQ(result, 0, "steal succeeded");
    TEST_ASSERT_EQ(steal.len, 3, "stole 3");
    TEST_ASSERT(steal.vals == steal.fast, "embedded array");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "segment emptied");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial + 3, "refs moved, not dropped");

    KosStackSteal_Free(&steal);
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "refs released by Free");

    Py_DECREF(v1);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_steal_large(void) {
    /*
      A steal past the fast size mallocs; Free releases the array.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t initial = Py_REFCNT(v1);
    for (int i = 0; i < 20; i++)
        KosStack_PushRef(&seg, v1);

    KosStackSteal steal;
    int result = KosStackSteal_Tail(&steal, 20, &seg);
    TEST_ASSERT_EQ(result, 0, "steal succeeded");
    TEST_ASSERT_EQ(steal.len, 20, "stole 20");
    TEST_ASSERT(steal.vals != steal.fast, "malloc'd array");

    KosStackSteal_Free(&steal);
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "refs released by Free");

    Py_DECREF(v1);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_steal_prepend(void) {
    /*
      TailPrepend puts a borrowed prefix at vals[0]; Free never
      releases the prefix.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *prefix = PyLong_FromLong(TEST_VAL_3);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t prefix_initial = Py_REFCNT(prefix);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v1);

    KosStackSteal steal;
    int result = KosStackSteal_TailPrepend(&steal, prefix, 2, &seg);
    TEST_ASSERT_EQ(result, 0, "steal succeeded");
    TEST_ASSERT_EQ(steal.len, 3, "prefix + 2");
    TEST_ASSERT_EQ(steal.owned_start, 1, "prefix borrowed");
    TEST_ASSERT_SAME(steal.vals[0], prefix, "prefix at 0");
    TEST_ASSERT_EQ(Py_REFCNT(prefix), prefix_initial, "prefix not increfed");

    KosStackSteal_Free(&steal);
    TEST_ASSERT_EQ(Py_REFCNT(prefix), prefix_initial, "prefix untouched");

    Py_DECREF(prefix);
    Py_DECREF(v1);
    KosStack_Release(seg);
    return 0;
}


static int test_seg_steal_underrun(void) {
    /*
      An underrun steal fails but leaves the struct safe for Free.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    KosStack_PushRef(&seg, v1);

    KosStackSteal steal;
    int result = KosStackSteal_Tail(&steal, 5, &seg);
    TEST_ASSERT_EQ(result, -1, "underrun fails");
    PyErr_Clear();
    TEST_ASSERT_EQ(steal.len, 0, "steal left safe");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 1, "segment untouched");
    KosStackSteal_Free(&steal);

    Py_DECREF(v1);
    KosStack_Release(seg);
    return 0;
}


/* === deep copy === */


static int test_seg_deepcopy(void) {
    /*
      DeepCopy yields an independent segment with fresh refs to the
      same values; mutating the copy leaves the original alone.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_2);
    Py_ssize_t initial = Py_REFCNT(v1);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v2);

    KosSegment *copy = KosStack_DeepCopy(seg);
    TEST_ASSERT_NOT_NULL(copy, "copy created");
    TEST_ASSERT(copy != seg, "distinct segment");
    TEST_ASSERT_EQ(KosSegment_LEN(copy), 2, "same length");
    TEST_ASSERT_SAME(KosStack_PeekAt(copy, 0), v1, "same values");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial + 2, "fresh ref taken");

    PyObject *popped = KosStack_PopRef(&copy);
    TEST_ASSERT_SAME(popped, v2, "copy pops v2");
    Py_DECREF(popped);
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 2, "original untouched");

    KosStack_Release(copy);
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial + 1, "copy refs released");

    KosStack_Release(seg);
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "balanced");

    Py_DECREF(v1);
    Py_DECREF(v2);
    return 0;
}


static int test_seg_deepcopy_empty(void) {
    /*
      DeepCopy of an empty segment gives a fresh empty segment;
      DeepCopy of NULL gives NULL without error.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    KosSegment *copy = KosStack_DeepCopy(seg);
    TEST_ASSERT_NOT_NULL(copy, "empty copy created");
    TEST_ASSERT(copy != seg, "distinct segment");
    TEST_ASSERT_EQ(KosSegment_LEN(copy), 0, "empty");
    KosStack_Release(copy);

    TEST_ASSERT_NULL(KosStack_DeepCopy(NULL), "NULL copies to NULL");
    TEST_ASSERT(PyErr_Occurred() == NULL, "no error for NULL");

    KosStack_Release(seg);
    return 0;
}


static int test_seg_deepcopy_grown(void) {
    /*
      DeepCopy of a grown segment carries all values.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    for (int i = 0; i < 40; i++) {
        PyObject *val = PyLong_FromLong(30000 + i);
        KosStack_PushRef(&seg, val);
        Py_DECREF(val);
    }

    KosSegment *copy = KosStack_DeepCopy(seg);
    TEST_ASSERT_NOT_NULL(copy, "copy created");
    TEST_ASSERT_EQ(KosSegment_LEN(copy), 40, "same length");
    for (int i = 0; i < 40; i++) {
        PyObject *item = KosStack_PeekAt(copy, i);
        TEST_ASSERT_EQ(PyLong_AsLong(item), 30000 + i, "value intact");
    }

    KosStack_Release(copy);
    KosStack_Release(seg);
    return 0;
}


/* === freelist === */


static int test_seg_freelist_reuse(void) {
    /*
      A released base-size segment is cached and comes back reset on
      the next allocation (LIFO), rather than a fresh malloc.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    KosStack_PushRef(&seg, v1);
    Py_DECREF(v1);

    KosSegment *was = seg;
    KosStack_Release(seg);

    KosSegment *seg2 = KosStack_NewEmpty();
    TEST_ASSERT_SAME(seg2, was, "freelist reuse (LIFO)");
    TEST_ASSERT_EQ(KosSegment_LEN(seg2), 0, "reset len");
    TEST_ASSERT_EQ(KosSegment_MAX(seg2), 16, "base size");
    TEST_ASSERT_NULL(KosSegment_BACK(seg2), "no stale link");

    KosStack_Release(seg2);
    return 0;
}


static int test_seg_freelist_no_grown(void) {
    /*
      Grown segments are freed, never cached: the allocation after
      releasing one is always base-sized.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    for (int i = 0; i < 17; i++) {
        PyObject *val = PyLong_FromLong(10000 + i);
        KosStack_PushRef(&seg, val);
        Py_DECREF(val);
    }
    TEST_ASSERT_EQ(KosSegment_MAX(seg), 32, "grown");

    KosStack_Release(seg);

    KosSegment *seg2 = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg2, "create segment");
    TEST_ASSERT_EQ(KosSegment_MAX(seg2), 16, "grown segment not cached");

    KosStack_Release(seg2);
    return 0;
}


/* === inline segments === */


static int test_seg_inline_basic(void) {
    /*
      An inline segment holds up to KOS_SEGMENT_INLINE_SLOTS values
      with no spill; Release empties it without freeing.
    */
    KosInlineSegment is;
    KosSegment *seg = kos_inline_segment_init(&is);

    TEST_ASSERT(KosSegment_IS_INLINE(seg), "inline discriminator");
    TEST_ASSERT_EQ(KosSegment_MAX(seg), KOS_SEGMENT_INLINE_SLOTS,
                   "inline capacity");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t initial = Py_REFCNT(v1);

    for (int i = 0; i < KOS_SEGMENT_INLINE_SLOTS; i++)
        KosStack_PushRef(&seg, v1);
    TEST_ASSERT_SAME(seg, &is.hdr, "no spill at capacity");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), KOS_SEGMENT_INLINE_SLOTS, "full");

    KosStack_Release(seg);
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "contents released");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), 0, "emptied in place");

    Py_DECREF(v1);
    return 0;
}


static int test_seg_inline_spill(void) {
    /*
      The push past inline capacity spills to a heap segment through
      the owning slot; values and the back link carry over; the inline
      storage is left empty.
    */
    KosInlineSegment is;
    KosSegment *seg = kos_inline_segment_init(&is);
    KosSegment *sentinel = NULL;

    KosSegment_BACK(seg) = &sentinel;

    for (int i = 0; i < KOS_SEGMENT_INLINE_SLOTS + 1; i++) {
        PyObject *val = PyLong_FromLong(40000 + i);
        int result = KosStack_PushRef(&seg, val);
        Py_DECREF(val);
        TEST_ASSERT_EQ(result, 0, "push succeeded");
    }

    TEST_ASSERT(seg != &is.hdr, "spilled to heap");
    TEST_ASSERT(! KosSegment_IS_INLINE(seg), "heap discriminator");
    TEST_ASSERT_EQ(KosSegment_LEN(seg), KOS_SEGMENT_INLINE_SLOTS + 1,
                   "all values carried");
    TEST_ASSERT(KosSegment_BACK(seg) == &sentinel, "back carried over");
    TEST_ASSERT_EQ(KosSegment_LEN(&is.hdr), 0, "inline left empty");
    TEST_ASSERT_NULL(KosSegment_BACK(&is.hdr), "inline back cleared");

    for (int i = KOS_SEGMENT_INLINE_SLOTS; i >= 0; i--) {
        PyObject *popped = KosStack_PopRef(&seg);
        TEST_ASSERT_EQ(PyLong_AsLong(popped), 40000 + i, "value intact");
        Py_DECREF(popped);
    }

    KosStack_Release(seg);
    return 0;
}


static int test_seg_inline_detach(void) {
    /*
      Detach moves an inline segment's contents to a heap segment for
      storage away from the owner; a heap segment detaches as itself.
    */
    KosInlineSegment is;
    KosSegment *seg = kos_inline_segment_init(&is);

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t initial = Py_REFCNT(v1);
    KosStack_PushRef(&seg, v1);
    KosStack_PushRef(&seg, v1);

    KosSegment *detached = KosStack_Detach(&seg);
    TEST_ASSERT_NOT_NULL(detached, "detached");
    TEST_ASSERT(detached != &is.hdr, "moved to heap");
    TEST_ASSERT(! KosSegment_IS_INLINE(detached), "heap segment");
    TEST_ASSERT_EQ(KosSegment_LEN(detached), 2, "contents moved");
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial + 2, "refs moved, not churned");
    TEST_ASSERT_EQ(KosSegment_LEN(&is.hdr), 0, "inline emptied");

    KosSegment *again = KosStack_Detach(&detached);
    TEST_ASSERT_SAME(again, detached, "heap detaches as itself");

    KosStack_Release(detached);
    TEST_ASSERT_EQ(Py_REFCNT(v1), initial, "balanced");
    Py_DECREF(v1);
    return 0;
}


/* === GC traverse === */


static int count_visit(PyObject *obj, void *arg) {
    (void) obj;
    *((int *) arg) += 1;
    return 0;
}


static int test_seg_traverse(void) {
    /*
      Traverse visits exactly the live slots; a NULL segment visits
      nothing.
    */
    KosSegment *seg = KosStack_NewEmpty();
    TEST_ASSERT_NOT_NULL(seg, "create segment");

    PyObject *v1 = PyLong_FromLong(TEST_VAL_1);
    for (int i = 0; i < 3; i++)
        KosStack_PushRef(&seg, v1);
    KosStack_Drop1(&seg);

    int count = 0;
    int result = KosStack_Traverse(seg, count_visit, &count);
    TEST_ASSERT_EQ(result, 0, "traverse succeeded");
    TEST_ASSERT_EQ(count, 2, "visits live slots only");

    count = 0;
    result = KosStack_Traverse(NULL, count_visit, &count);
    TEST_ASSERT_EQ(result, 0, "NULL traverse succeeded");
    TEST_ASSERT_EQ(count, 0, "nothing visited");

    Py_DECREF(v1);
    KosStack_Release(seg);
    return 0;
}


/* === test suite runner === */


int run_stack_tests(void) {
    BEGIN_TEST_SUITE("Stack Segment Memory Tests");

    /* push / pop */
    RUN_TEST(test_seg_push_ref);
    RUN_TEST(test_seg_push_steal);
    RUN_TEST(test_seg_lazy_create);
    RUN_TEST(test_seg_pop_underrun);

    /* growth */
    RUN_TEST(test_seg_grow);
    RUN_TEST(test_seg_grow_limit);

    /* peek */
    RUN_TEST(test_seg_peek);
    RUN_TEST(test_seg_tail_ptr);

    /* drop */
    RUN_TEST(test_seg_drop);
    RUN_TEST(test_seg_drop_noerr);
    RUN_TEST(test_seg_clear);

    /* duplicate */
    RUN_TEST(test_seg_duplicate);

    /* tail extraction */
    RUN_TEST(test_seg_pop_tail);
    RUN_TEST(test_seg_pop_tail_ptr);
    RUN_TEST(test_seg_pop_tail_dicts);

    /* steal */
    RUN_TEST(test_seg_steal_small);
    RUN_TEST(test_seg_steal_large);
    RUN_TEST(test_seg_steal_prepend);
    RUN_TEST(test_seg_steal_underrun);

    /* deep copy */
    RUN_TEST(test_seg_deepcopy);
    RUN_TEST(test_seg_deepcopy_empty);
    RUN_TEST(test_seg_deepcopy_grown);

    /* freelist */
    RUN_TEST(test_seg_freelist_reuse);
    RUN_TEST(test_seg_freelist_no_grown);

    /* inline segments */
    RUN_TEST(test_seg_inline_basic);
    RUN_TEST(test_seg_inline_spill);
    RUN_TEST(test_seg_inline_detach);

    /* GC */
    RUN_TEST(test_seg_traverse);

    return 0;
}


/* The end. */
