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
   Memory tests for atom types (symbol and keyword)

   Tests the interning mechanism and borrowed-reference pattern used
   by symbols and keywords.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === symbol tests === */


static int test_symbol_create(void) {
    /*
      Test that symbols can be created and have correct initial state.
    */
    PyObject *name = PyUnicode_FromString("test_symbol_create");
    TEST_ASSERT_NOT_NULL(name, "create name string");

    PyObject *sym = KosSymbol_FromString(name);
    TEST_ASSERT_NOT_NULL(sym, "create symbol");

    /* Symbol should be the correct type */
    TEST_ASSERT(KosSymbol_CheckExact(sym), "symbol type check");

    Py_DECREF(sym);
    Py_DECREF(name);
    return 0;
}


static int test_symbol_interned(void) {
    /*
      Test that symbols with the same name return the same object.
    */
    PyObject *name = PyUnicode_FromString("test_symbol_interned");
    TEST_ASSERT_NOT_NULL(name, "create name string");

    PyObject *sym1 = KosSymbol_FromString(name);
    TEST_ASSERT_NOT_NULL(sym1, "create first symbol");

    PyObject *sym2 = KosSymbol_FromString(name);
    TEST_ASSERT_NOT_NULL(sym2, "create second symbol");

    /* Should be the exact same object */
    TEST_ASSERT_SAME(sym1, sym2, "symbols are interned");

    Py_DECREF(sym2);
    Py_DECREF(sym1);
    Py_DECREF(name);
    return 0;
}


static int test_symbol_intern_refcount(void) {
    /*
      Test the borrowed-reference interning pattern.

      When a symbol is first created, it's added to the intern dict
      and then DECREF'd, leaving it with refcount 1 from the caller.
      The intern dict holds a "borrowed" reference (not counted).

      When retrieved again, the refcount increases.
    */
    PyObject *name = PyUnicode_FromString("test_symbol_intern_refcount");
    TEST_ASSERT_NOT_NULL(name, "create name string");

    PyObject *sym1 = KosSymbol_FromString(name);
    TEST_ASSERT_NOT_NULL(sym1, "create symbol");

    /* After creation, refcount should be 1 (our reference only) */
    /* Note: intern dict has borrowed ref, not counted */
    Py_ssize_t initial_refcnt = Py_REFCNT(sym1);
    TEST_ASSERT_EQ(initial_refcnt, 1, "initial refcount is 1");

    /* Getting same symbol again should increment refcount */
    PyObject *sym2 = KosSymbol_FromString(name);
    TEST_ASSERT_NOT_NULL(sym2, "get symbol again");
    TEST_ASSERT_SAME(sym1, sym2, "same object returned");
    TEST_ASSERT_EQ(Py_REFCNT(sym1), 2, "refcount now 2");

    Py_DECREF(sym2);
    TEST_ASSERT_EQ(Py_REFCNT(sym1), 1, "refcount back to 1");

    Py_DECREF(sym1);
    Py_DECREF(name);
    return 0;
}


static int test_symbol_dealloc_removes_from_intern(void) {
    /*
      Test that when a symbol's refcount drops to 0, it removes
      itself from the intern dictionary.

      We verify this by creating a symbol, releasing all refs, then
      creating a symbol with the same name - it should be a new object.
    */
    PyObject *name = PyUnicode_FromString("test_symbol_dealloc_unique_name_12345");
    TEST_ASSERT_NOT_NULL(name, "create name string");

    /* Create symbol and capture its address */
    PyObject *sym1 = KosSymbol_FromString(name);
    TEST_ASSERT_NOT_NULL(sym1, "create first symbol");

    void *sym1_addr = (void *)sym1;

    /* Release our reference - symbol should be deallocated */
    Py_DECREF(sym1);

    /* Create symbol with same name - should be new object */
    PyObject *sym2 = KosSymbol_FromString(name);
    TEST_ASSERT_NOT_NULL(sym2, "create second symbol after dealloc");

    /*
      Note: we can't rely on the address being different due to memory
      reuse, but we can verify the symbol works correctly and has
      correct refcount (indicating it's a fresh allocation).
    */
    TEST_ASSERT_EQ(Py_REFCNT(sym2), 1, "new symbol has refcount 1");
    (void)sym1_addr;  /* silence unused warning */

    Py_DECREF(sym2);
    Py_DECREF(name);
    return 0;
}


static int test_symbol_different_names(void) {
    /*
      Test that symbols with different names are different objects.
    */
    PyObject *name1 = PyUnicode_FromString("symbol_name_one");
    PyObject *name2 = PyUnicode_FromString("symbol_name_two");
    TEST_ASSERT_NOT_NULL(name1, "create first name");
    TEST_ASSERT_NOT_NULL(name2, "create second name");

    PyObject *sym1 = KosSymbol_FromString(name1);
    PyObject *sym2 = KosSymbol_FromString(name2);
    TEST_ASSERT_NOT_NULL(sym1, "create first symbol");
    TEST_ASSERT_NOT_NULL(sym2, "create second symbol");

    TEST_ASSERT_NOT_SAME(sym1, sym2, "different names are different symbols");

    Py_DECREF(sym2);
    Py_DECREF(sym1);
    Py_DECREF(name2);
    Py_DECREF(name1);
    return 0;
}


/* === keyword tests === */


static int test_keyword_create(void) {
    /*
      Test that keywords can be created and have correct initial state.
    */
    PyObject *name = PyUnicode_FromString("test_keyword_create");
    TEST_ASSERT_NOT_NULL(name, "create name string");

    PyObject *kwd = KosKeyword_FromString(name);
    TEST_ASSERT_NOT_NULL(kwd, "create keyword");

    TEST_ASSERT(KosKeyword_CheckExact(kwd), "keyword type check");

    Py_DECREF(kwd);
    Py_DECREF(name);
    return 0;
}


static int test_keyword_interned(void) {
    /*
      Test that keywords with the same name return the same object.
    */
    PyObject *name = PyUnicode_FromString("test_keyword_interned");
    TEST_ASSERT_NOT_NULL(name, "create name string");

    PyObject *kwd1 = KosKeyword_FromString(name);
    TEST_ASSERT_NOT_NULL(kwd1, "create first keyword");

    PyObject *kwd2 = KosKeyword_FromString(name);
    TEST_ASSERT_NOT_NULL(kwd2, "create second keyword");

    TEST_ASSERT_SAME(kwd1, kwd2, "keywords are interned");

    Py_DECREF(kwd2);
    Py_DECREF(kwd1);
    Py_DECREF(name);
    return 0;
}


static int test_keyword_colon_stripping(void) {
    /*
      Test that keywords strip leading and trailing colons, and that
      different colon variants resolve to the same interned keyword.
    */
    PyObject *name_plain = PyUnicode_FromString("mykey");
    PyObject *name_leading = PyUnicode_FromString(":mykey");
    PyObject *name_trailing = PyUnicode_FromString("mykey:");
    PyObject *name_both = PyUnicode_FromString(":mykey:");

    TEST_ASSERT_NOT_NULL(name_plain, "create plain name");
    TEST_ASSERT_NOT_NULL(name_leading, "create leading colon name");
    TEST_ASSERT_NOT_NULL(name_trailing, "create trailing colon name");
    TEST_ASSERT_NOT_NULL(name_both, "create both colons name");

    PyObject *kwd1 = KosKeyword_FromString(name_plain);
    PyObject *kwd2 = KosKeyword_FromString(name_leading);
    PyObject *kwd3 = KosKeyword_FromString(name_trailing);
    PyObject *kwd4 = KosKeyword_FromString(name_both);

    TEST_ASSERT_NOT_NULL(kwd1, "create keyword from plain");
    TEST_ASSERT_NOT_NULL(kwd2, "create keyword from leading");
    TEST_ASSERT_NOT_NULL(kwd3, "create keyword from trailing");
    TEST_ASSERT_NOT_NULL(kwd4, "create keyword from both");

    /* All should be the same object */
    TEST_ASSERT_SAME(kwd1, kwd2, "leading colon stripped");
    TEST_ASSERT_SAME(kwd1, kwd3, "trailing colon stripped");
    TEST_ASSERT_SAME(kwd1, kwd4, "both colons stripped");

    Py_DECREF(kwd4);
    Py_DECREF(kwd3);
    Py_DECREF(kwd2);
    Py_DECREF(kwd1);
    Py_DECREF(name_both);
    Py_DECREF(name_trailing);
    Py_DECREF(name_leading);
    Py_DECREF(name_plain);
    return 0;
}


static int test_keyword_intern_refcount(void) {
    /*
      Test the borrowed-reference pattern for keywords.
    */
    PyObject *name = PyUnicode_FromString("test_keyword_intern_refcount");
    TEST_ASSERT_NOT_NULL(name, "create name string");

    PyObject *kwd1 = KosKeyword_FromString(name);
    TEST_ASSERT_NOT_NULL(kwd1, "create keyword");

    TEST_ASSERT_EQ(Py_REFCNT(kwd1), 1, "initial refcount is 1");

    PyObject *kwd2 = KosKeyword_FromString(name);
    TEST_ASSERT_SAME(kwd1, kwd2, "same object returned");
    TEST_ASSERT_EQ(Py_REFCNT(kwd1), 2, "refcount now 2");

    Py_DECREF(kwd2);
    TEST_ASSERT_EQ(Py_REFCNT(kwd1), 1, "refcount back to 1");

    Py_DECREF(kwd1);
    Py_DECREF(name);
    return 0;
}


/* === gensym tests === */


static int test_gensym_unique(void) {
    /*
      Test that gensym generates unique symbols each time.
    */
    PyObject *name = PyUnicode_FromString("gensym_test");
    TEST_ASSERT_NOT_NULL(name, "create prefix");

    PyObject *sym1 = KosGensym_FromString(name, NULL);
    PyObject *sym2 = KosGensym_FromString(name, NULL);
    PyObject *sym3 = KosGensym_FromString(name, NULL);

    TEST_ASSERT_NOT_NULL(sym1, "create first gensym");
    TEST_ASSERT_NOT_NULL(sym2, "create second gensym");
    TEST_ASSERT_NOT_NULL(sym3, "create third gensym");

    /* All should be different objects */
    TEST_ASSERT_NOT_SAME(sym1, sym2, "gensyms are unique (1,2)");
    TEST_ASSERT_NOT_SAME(sym2, sym3, "gensyms are unique (2,3)");
    TEST_ASSERT_NOT_SAME(sym1, sym3, "gensyms are unique (1,3)");

    /* All should be symbols */
    TEST_ASSERT(KosSymbol_CheckExact(sym1), "gensym is symbol type");
    TEST_ASSERT(KosSymbol_CheckExact(sym2), "gensym is symbol type");
    TEST_ASSERT(KosSymbol_CheckExact(sym3), "gensym is symbol type");

    Py_DECREF(sym3);
    Py_DECREF(sym2);
    Py_DECREF(sym1);
    Py_DECREF(name);
    return 0;
}


static int test_gensym_no_name(void) {
    /*
      Test that gensym works without a name prefix.
    */
    PyObject *sym = KosGensym_FromString(NULL, NULL);
    TEST_ASSERT_NOT_NULL(sym, "create gensym without name");
    TEST_ASSERT(KosSymbol_CheckExact(sym), "gensym is symbol type");

    Py_DECREF(sym);
    return 0;
}


/* === symbol/keyword distinction tests === */


static int test_symbol_keyword_distinct(void) {
    /*
      Test that symbols and keywords with the same name are different.
    */
    PyObject *name = PyUnicode_FromString("shared_name");
    TEST_ASSERT_NOT_NULL(name, "create name string");

    PyObject *sym = KosSymbol_FromString(name);
    PyObject *kwd = KosKeyword_FromString(name);

    TEST_ASSERT_NOT_NULL(sym, "create symbol");
    TEST_ASSERT_NOT_NULL(kwd, "create keyword");

    TEST_ASSERT_NOT_SAME(sym, kwd, "symbol and keyword are different");
    TEST_ASSERT(KosSymbol_CheckExact(sym), "sym is symbol");
    TEST_ASSERT(KosKeyword_CheckExact(kwd), "kwd is keyword");
    TEST_ASSERT(!KosKeyword_Check(sym), "sym is not keyword");
    TEST_ASSERT(!KosSymbol_Check(kwd), "kwd is not symbol");

    Py_DECREF(kwd);
    Py_DECREF(sym);
    Py_DECREF(name);
    return 0;
}


/* === stress tests === */


static int test_symbol_stress_alloc(void) {
    /*
      Stress test: create and destroy many symbols.
      This helps detect memory leaks under load.
    */
    char namebuf[64];
    PyObject *symbols[100];

    /* Create many unique symbols */
    for (int i = 0; i < 100; i++) {
        snprintf(namebuf, sizeof(namebuf), "stress_symbol_%d", i);
        PyObject *name = PyUnicode_FromString(namebuf);
        TEST_ASSERT_NOT_NULL(name, "create stress name");

        symbols[i] = KosSymbol_FromString(name);
        TEST_ASSERT_NOT_NULL(symbols[i], "create stress symbol");

        Py_DECREF(name);
    }

    /* Verify each has refcount of 1 */
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_EQ(Py_REFCNT(symbols[i]), 1, "stress symbol refcount");
    }

    /* Release all */
    for (int i = 0; i < 100; i++) {
        Py_DECREF(symbols[i]);
    }

    return 0;
}


/* === test suite runner === */


int run_atom_tests(void) {
    BEGIN_TEST_SUITE("Atom Memory Tests");

    /* Symbol tests */
    RUN_TEST(test_symbol_create);
    RUN_TEST(test_symbol_interned);
    RUN_TEST(test_symbol_intern_refcount);
    RUN_TEST(test_symbol_dealloc_removes_from_intern);
    RUN_TEST(test_symbol_different_names);

    /* Keyword tests */
    RUN_TEST(test_keyword_create);
    RUN_TEST(test_keyword_interned);
    RUN_TEST(test_keyword_colon_stripping);
    RUN_TEST(test_keyword_intern_refcount);

    /* Gensym tests */
    RUN_TEST(test_gensym_unique);
    RUN_TEST(test_gensym_no_name);

    /* Cross-type tests */
    RUN_TEST(test_symbol_keyword_distinct);

    /* Stress tests */
    RUN_TEST(test_symbol_stress_alloc);

    return 0;
}


/* The end. */
