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
   Memory tests for pair and nil types

   Tests the pair free list, reference counting of head/tail contents,
   cyclic structure handling, and nil singleton behavior.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === pair basic tests === */


static int test_pair_create(void) {
    /*
      Test that pairs can be created with head and tail.
    */
    PyObject *head = PyLong_FromLong(1);
    PyObject *tail = PyLong_FromLong(2);
    TEST_ASSERT_NOT_NULL(head, "create head");
    TEST_ASSERT_NOT_NULL(tail, "create tail");

    PyObject *p = KosPair_New(head, tail);
    TEST_ASSERT_NOT_NULL(p, "create pair");
    TEST_ASSERT(KosPair_CheckExact(p), "pair type check");

    /* Verify contents */
    TEST_ASSERT_SAME(KosPair_CAR(p), head, "car is head");
    TEST_ASSERT_SAME(KosPair_CDR(p), tail, "cdr is tail");

    Py_DECREF(p);
    Py_DECREF(tail);
    Py_DECREF(head);
    return 0;
}


static int test_pair_refcount_contents(void) {
    /*
      Test that pairs properly increment/decrement refcounts of their
      head and tail contents.

      Note: We use large integers to avoid Python's small integer cache,
      which keeps small integers (-5 to 256) with very high refcounts.
    */
    PyObject *head = PyLong_FromLong(100000);
    PyObject *tail = PyLong_FromLong(200000);
    TEST_ASSERT_NOT_NULL(head, "create head");
    TEST_ASSERT_NOT_NULL(tail, "create tail");

    Py_ssize_t head_initial = Py_REFCNT(head);
    Py_ssize_t tail_initial = Py_REFCNT(tail);

    /* Create pair - should increment refcounts */
    PyObject *p = KosPair_New(head, tail);
    TEST_ASSERT_NOT_NULL(p, "create pair");

    TEST_ASSERT_EQ(Py_REFCNT(head), head_initial + 1, "head refcount incremented");
    TEST_ASSERT_EQ(Py_REFCNT(tail), tail_initial + 1, "tail refcount incremented");

    /* Destroy pair - should decrement refcounts */
    Py_DECREF(p);

    TEST_ASSERT_EQ(Py_REFCNT(head), head_initial, "head refcount restored");
    TEST_ASSERT_EQ(Py_REFCNT(tail), tail_initial, "tail refcount restored");

    Py_DECREF(tail);
    Py_DECREF(head);
    return 0;
}


static int test_pair_setcar_setcdr_refcount(void) {
    /*
      Test that SETCAR and SETCDR properly manage refcounts.

      Note: We use large integers to avoid Python's small integer cache,
      which keeps small integers (-5 to 256) with very high refcounts.
    */
    PyObject *head1 = PyLong_FromLong(100001);
    PyObject *head2 = PyLong_FromLong(100002);
    PyObject *tail1 = PyLong_FromLong(100010);
    PyObject *tail2 = PyLong_FromLong(100020);

    TEST_ASSERT_NOT_NULL(head1, "create head1");
    TEST_ASSERT_NOT_NULL(head2, "create head2");
    TEST_ASSERT_NOT_NULL(tail1, "create tail1");
    TEST_ASSERT_NOT_NULL(tail2, "create tail2");

    Py_ssize_t head1_initial = Py_REFCNT(head1);
    Py_ssize_t head2_initial = Py_REFCNT(head2);
    Py_ssize_t tail1_initial = Py_REFCNT(tail1);
    Py_ssize_t tail2_initial = Py_REFCNT(tail2);

    PyObject *p = KosPair_New(head1, tail1);
    TEST_ASSERT_NOT_NULL(p, "create pair");

    /* head1 and tail1 should have increased refcount */
    TEST_ASSERT_EQ(Py_REFCNT(head1), head1_initial + 1, "head1 held by pair");
    TEST_ASSERT_EQ(Py_REFCNT(tail1), tail1_initial + 1, "tail1 held by pair");

    /* Replace head with SETCAR */
    KosPair_SETCAR(p, head2);
    TEST_ASSERT_EQ(Py_REFCNT(head1), head1_initial, "head1 released");
    TEST_ASSERT_EQ(Py_REFCNT(head2), head2_initial + 1, "head2 held by pair");

    /* Replace tail with SETCDR */
    KosPair_SETCDR(p, tail2);
    TEST_ASSERT_EQ(Py_REFCNT(tail1), tail1_initial, "tail1 released");
    TEST_ASSERT_EQ(Py_REFCNT(tail2), tail2_initial + 1, "tail2 held by pair");

    Py_DECREF(p);

    TEST_ASSERT_EQ(Py_REFCNT(head2), head2_initial, "head2 released after dealloc");
    TEST_ASSERT_EQ(Py_REFCNT(tail2), tail2_initial, "tail2 released after dealloc");

    Py_DECREF(tail2);
    Py_DECREF(tail1);
    Py_DECREF(head2);
    Py_DECREF(head1);
    return 0;
}


/* === pair free list tests === */


/*
  PAIR_MAX_FREE is 256 in pair.c - pairs are cached in a free list
  when deallocated, up to this limit.
*/
#define PAIR_MAX_FREE 256


static int test_pair_freelist_basic(void) {
    /*
      Basic test: create and destroy pairs to exercise free list.
    */
    PyObject *pairs[10];

    for (int i = 0; i < 10; i++) {
        pairs[i] = KosPair_New(PyLong_FromLong(i), KosNil);
        TEST_ASSERT_NOT_NULL(pairs[i], "create pair for freelist test");
    }

    /* Destroy all - should go to free list */
    for (int i = 0; i < 10; i++) {
        /* Need to release the PyLong we created */
        PyObject *head = KosPair_CAR(pairs[i]);
        Py_DECREF(pairs[i]);
        Py_DECREF(head);
    }

    /* Create again - should reuse from free list */
    for (int i = 0; i < 10; i++) {
        pairs[i] = KosPair_New(PyLong_FromLong(i + 100), KosNil);
        TEST_ASSERT_NOT_NULL(pairs[i], "create pair after freelist");
    }

    /* Cleanup */
    for (int i = 0; i < 10; i++) {
        PyObject *head = KosPair_CAR(pairs[i]);
        Py_DECREF(pairs[i]);
        Py_DECREF(head);
    }

    return 0;
}


static int test_pair_freelist_overflow(void) {
    /*
      Test behavior when creating more pairs than PAIR_MAX_FREE.
      Excess pairs should be actually freed, not cached.
    */
    PyObject *pairs[PAIR_MAX_FREE + 100];
    int total = PAIR_MAX_FREE + 100;

    /* Create more pairs than free list can hold */
    for (int i = 0; i < total; i++) {
        pairs[i] = KosPair_New(PyLong_FromLong(i), KosNil);
        TEST_ASSERT_NOT_NULL(pairs[i], "create overflow pair");
    }

    /* Destroy all - first PAIR_MAX_FREE go to free list, rest are freed */
    for (int i = 0; i < total; i++) {
        PyObject *head = KosPair_CAR(pairs[i]);
        Py_DECREF(pairs[i]);
        Py_DECREF(head);
    }

    /* Create again - test didn't crash and allocations work */
    for (int i = 0; i < 50; i++) {
        PyObject *p = KosPair_New(PyLong_FromLong(i), KosNil);
        TEST_ASSERT_NOT_NULL(p, "create pair after overflow");
        PyObject *head = KosPair_CAR(p);
        Py_DECREF(p);
        Py_DECREF(head);
    }

    return 0;
}


/* === cyclic pair tests === */


static int test_pair_cyclic_create(void) {
    /*
      Test creating a cyclic (self-referential) pair.
    */
    PyObject *p = KosPair_New(PyLong_FromLong(1), KosNil);
    TEST_ASSERT_NOT_NULL(p, "create pair");

    /* Make it cyclic */
    KosPair_SETCDR(p, p);

    /* Verify it's cyclic */
    TEST_ASSERT_SAME(KosPair_CDR(p), p, "pair is cyclic");
    TEST_ASSERT(KosPair_IsRecursive(p), "pair reports recursive");

    /* Dealloc - should not infinite loop */
    PyObject *head = KosPair_CAR(p);
    Py_DECREF(p);
    Py_DECREF(head);

    return 0;
}


static int test_pair_cyclic_chain(void) {
    /*
      Test a longer cyclic chain: a -> b -> c -> a
    */
    PyObject *a = KosPair_New(PyLong_FromLong(1), KosNil);
    PyObject *b = KosPair_New(PyLong_FromLong(2), KosNil);
    PyObject *c = KosPair_New(PyLong_FromLong(3), KosNil);

    TEST_ASSERT_NOT_NULL(a, "create a");
    TEST_ASSERT_NOT_NULL(b, "create b");
    TEST_ASSERT_NOT_NULL(c, "create c");

    /* Link: a -> b -> c -> a */
    KosPair_SETCDR(a, b);
    KosPair_SETCDR(b, c);
    KosPair_SETCDR(c, a);

    TEST_ASSERT(KosPair_IsRecursive(a), "chain is recursive");
    TEST_ASSERT(KosPair_IsRecursive(b), "chain is recursive from b");
    TEST_ASSERT(KosPair_IsRecursive(c), "chain is recursive from c");

    /* Get heads before dealloc */
    PyObject *head_a = KosPair_CAR(a);
    PyObject *head_b = KosPair_CAR(b);
    PyObject *head_c = KosPair_CAR(c);

    /* Dealloc - cyclic GC should handle this */
    Py_DECREF(a);
    Py_DECREF(b);
    Py_DECREF(c);

    Py_DECREF(head_a);
    Py_DECREF(head_b);
    Py_DECREF(head_c);

    /* Force GC to clean up cycles */
    PyGC_Collect();

    return 0;
}


/* === nil tests === */


static int test_nil_singleton(void) {
    /*
      Test that nil is a singleton and has expected properties.
    */
    TEST_ASSERT_NOT_NULL(KosNil, "nil exists");
    TEST_ASSERT(KosNil_Check(KosNil), "nil check passes");
    /* NilType is a subtype of KosPairType */
    TEST_ASSERT(PyType_IsSubtype(Py_TYPE(KosNil), &KosPairType), "nil is pair subtype");

    /* nil should never be deallocated - its dealloc is a no-op */
    Py_ssize_t initial_refcnt = Py_REFCNT(KosNil);

    /* Increment and decrement should work */
    Py_INCREF(KosNil);
    TEST_ASSERT_EQ(Py_REFCNT(KosNil), initial_refcnt + 1, "nil incref works");

    Py_DECREF(KosNil);
    TEST_ASSERT_EQ(Py_REFCNT(KosNil), initial_refcnt, "nil decref works");

    return 0;
}


static int test_nil_in_pair(void) {
    /*
      Test that nil works correctly as pair tail (proper list).
    */
    PyObject *p = KosPair_New(PyLong_FromLong(42), KosNil);
    TEST_ASSERT_NOT_NULL(p, "create pair with nil tail");

    TEST_ASSERT_SAME(KosPair_CDR(p), KosNil, "tail is nil");
    TEST_ASSERT(KosPair_IsProper(p), "pair with nil is proper");
    TEST_ASSERT(!KosPair_IsRecursive(p), "pair with nil is not recursive");

    PyObject *head = KosPair_CAR(p);
    Py_DECREF(p);
    Py_DECREF(head);

    return 0;
}


/* === KosPair_Cons tests === */


static int test_cons_empty(void) {
    /*
      Test that cons with empty sequence returns nil.
    */
    PyObject *empty = PyTuple_New(0);
    TEST_ASSERT_NOT_NULL(empty, "create empty tuple");

    PyObject *result = KosPair_Cons(empty);
    TEST_ASSERT_SAME(result, KosNil, "cons of empty is nil");

    Py_DECREF(result);
    Py_DECREF(empty);
    return 0;
}


static int test_cons_proper_list(void) {
    /*
      Test creating a proper list with KosPair_Cons.
    */
    PyObject *items = Py_BuildValue("(iiO)", 1, 2, KosNil);
    TEST_ASSERT_NOT_NULL(items, "create items tuple");

    PyObject *result = KosPair_Cons(items);
    TEST_ASSERT_NOT_NULL(result, "cons creates pair");
    TEST_ASSERT(KosPair_IsProper(result), "result is proper list");

    Py_DECREF(result);
    Py_DECREF(items);
    return 0;
}


static int test_cons_recursive(void) {
    /*
      Test creating a proper list then making it recursive via KosPair_MakeRecursive.
    */
    PyObject *items = Py_BuildValue("(iiO)", 1, 2, KosNil);
    TEST_ASSERT_NOT_NULL(items, "create items tuple");

    PyObject *result = KosPair_Cons(items);
    TEST_ASSERT_NOT_NULL(result, "cons creates pair");
    TEST_ASSERT_EQ(KosPair_MakeRecursive(result), 0, "MakeRecursive succeeds");
    TEST_ASSERT(KosPair_IsRecursive(result), "result is recursive");

    TEST_ASSERT_EQ(KosPair_BreakRecursive(result), 0, "BreakRecursive succeeds");
    TEST_ASSERT(! KosPair_IsRecursive(result), "result no longer recursive");

    Py_DECREF(result);
    Py_DECREF(items);

    /* Force GC for recursive cleanup */
    PyGC_Collect();

    return 0;
}


/* === stress tests === */


static int test_pair_stress_alloc(void) {
    /*
      Stress test: create and destroy many pairs.
      Exercises free list and normal allocation paths.
    */
    PyObject *pairs[500];

    /* Create many pairs */
    for (int i = 0; i < 500; i++) {
        pairs[i] = KosPair_New(PyLong_FromLong(i), KosNil);
        TEST_ASSERT_NOT_NULL(pairs[i], "create stress pair");
    }

    /* Destroy all */
    for (int i = 0; i < 500; i++) {
        PyObject *head = KosPair_CAR(pairs[i]);
        Py_DECREF(pairs[i]);
        Py_DECREF(head);
    }

    /* Create again */
    for (int i = 0; i < 500; i++) {
        pairs[i] = KosPair_New(PyLong_FromLong(i + 1000), KosNil);
        TEST_ASSERT_NOT_NULL(pairs[i], "create stress pair again");
    }

    /* Destroy all again */
    for (int i = 0; i < 500; i++) {
        PyObject *head = KosPair_CAR(pairs[i]);
        Py_DECREF(pairs[i]);
        Py_DECREF(head);
    }

    return 0;
}


static int test_pair_nested_deep(void) {
    /*
      Test deeply nested pairs to exercise trashcan mechanism.
    */
    PyObject *list = KosNil;
    Py_INCREF(list);

    /* Build a deeply nested list */
    for (int i = 0; i < 1000; i++) {
        PyObject *new_head = PyLong_FromLong(i);
        PyObject *new_pair = KosPair_New(new_head, list);
        TEST_ASSERT_NOT_NULL(new_pair, "create nested pair");
        Py_DECREF(new_head);
        Py_DECREF(list);
        list = new_pair;
    }

    /* Verify it's proper */
    TEST_ASSERT(KosPair_IsProper(list), "deep list is proper");

    /* Dealloc the whole thing - trashcan should prevent stack overflow */
    Py_DECREF(list);

    return 0;
}


/* === test suite runner === */


int run_pair_tests(void) {
    BEGIN_TEST_SUITE("Pair Memory Tests");

    /* Basic pair tests */
    RUN_TEST(test_pair_create);
    RUN_TEST(test_pair_refcount_contents);
    RUN_TEST(test_pair_setcar_setcdr_refcount);

    /* Free list tests */
    RUN_TEST(test_pair_freelist_basic);
    RUN_TEST(test_pair_freelist_overflow);

    /* Cyclic pair tests */
    RUN_TEST(test_pair_cyclic_create);
    RUN_TEST(test_pair_cyclic_chain);

    /* Nil tests */
    RUN_TEST(test_nil_singleton);
    RUN_TEST(test_nil_in_pair);

    /* Cons tests */
    RUN_TEST(test_cons_empty);
    RUN_TEST(test_cons_proper_list);
    RUN_TEST(test_cons_recursive);

    /* Stress tests */
    RUN_TEST(test_pair_stress_alloc);
    RUN_TEST(test_pair_nested_deep);

    return 0;
}


/* The end. */
