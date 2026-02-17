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
   Test harness for native memory tests

   Provides common macros and utilities for testing memory management
   in Koskari's native types.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#ifndef KOSKARI_TEST_HARNESS_H
#define KOSKARI_TEST_HARNESS_H


#include <Python.h>
#include <stdio.h>
#include <string.h>


/* === test result tracking === */


typedef struct {
    int passed;
    int failed;
    const char *current_test;
} TestContext;


/* Global test context */
extern TestContext test_ctx;


/* === assertion macros === */


#define TEST_ASSERT(cond, msg) do {                                     \
    if (!(cond)) {                                                      \
        fprintf(stderr, "  FAIL: %s\n", (msg));                         \
        fprintf(stderr, "        at %s:%d\n", __FILE__, __LINE__);      \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_EQ(a, b, msg) do {                                  \
    if ((a) != (b)) {                                                   \
        fprintf(stderr, "  FAIL: %s (expected %ld, got %ld)\n",         \
                (msg), (long)(b), (long)(a));                           \
        fprintf(stderr, "        at %s:%d\n", __FILE__, __LINE__);      \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_NOT_NULL(ptr, msg) do {                             \
    if ((ptr) == NULL) {                                                \
        fprintf(stderr, "  FAIL: %s (got NULL)\n", (msg));              \
        fprintf(stderr, "        at %s:%d\n", __FILE__, __LINE__);      \
        if (PyErr_Occurred()) {                                         \
            PyErr_Print();                                              \
        }                                                               \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_NULL(ptr, msg) do {                                 \
    if ((ptr) != NULL) {                                                \
        fprintf(stderr, "  FAIL: %s (expected NULL)\n", (msg));         \
        fprintf(stderr, "        at %s:%d\n", __FILE__, __LINE__);      \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_REFCNT(obj, expected, msg) do {                     \
    Py_ssize_t _refcnt = Py_REFCNT(obj);                                \
    if (_refcnt != (expected)) {                                        \
        fprintf(stderr, "  FAIL: %s (refcount: expected %ld, got %ld)\n", \
                (msg), (long)(expected), (long)_refcnt);                \
        fprintf(stderr, "        at %s:%d\n", __FILE__, __LINE__);      \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_SAME(a, b, msg) do {                                \
    if ((void *)(a) != (void *)(b)) {                                   \
        fprintf(stderr, "  FAIL: %s (objects not identical)\n", (msg)); \
        fprintf(stderr, "        at %s:%d\n", __FILE__, __LINE__);      \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_NOT_SAME(a, b, msg) do {                            \
    if ((void *)(a) == (void *)(b)) {                                   \
        fprintf(stderr, "  FAIL: %s (objects should not be identical)\n", \
                (msg));                                                 \
        fprintf(stderr, "        at %s:%d\n", __FILE__, __LINE__);      \
        return -1;                                                      \
    }                                                                   \
} while(0)


/* === test runner macros === */


#define RUN_TEST(fn) do {                                               \
    test_ctx.current_test = #fn;                                        \
    printf("  %s ... ", #fn);                                           \
    fflush(stdout);                                                     \
    if ((fn)() == 0) {                                                  \
        printf("ok\n");                                                 \
        test_ctx.passed++;                                              \
    } else {                                                            \
        test_ctx.failed++;                                              \
    }                                                                   \
} while(0)


#define BEGIN_TEST_SUITE(name) do {                                     \
    printf("\n%s:\n", (name));                                          \
} while(0)


/* === unit test runners === */

/*
  Each unit declares its test runner function here.
  The function should return 0 on success (all tests passed)
  or non-zero on failure.

  To add a new unit:
  1. Add the declaration here
  2. Create the implementation in test_<unit>.c
  3. Call the function from test_harness.c
*/

int run_atom_tests(void);
int run_pair_tests(void);
int run_namespace_tests(void);
int run_bytecode_tests(void);
int run_formals_tests(void);
int run_values_tests(void);
int run_stack_tests(void);
int run_dynchain_tests(void);
int run_eval_tests(void);
int run_paramref_tests(void);

/* Future units:
int run_tco_tests(void);
*/


#endif


/* The end. */
