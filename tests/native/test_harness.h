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

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
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
        printf("  FAIL: %s\n", (msg));                                  \
        printf("        at %s:%d\n", __FILE__, __LINE__);               \
        fflush(stdout);                                                 \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_EQ(a, b, msg) do {                                  \
    if ((a) != (b)) {                                                   \
        printf("  FAIL: %s (expected %ld, got %ld)\n",                  \
                (msg), (long)(b), (long)(a));                           \
        printf("        at %s:%d\n", __FILE__, __LINE__);               \
        fflush(stdout);                                                 \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_NOT_NULL(ptr, msg) do {                             \
    if ((ptr) == NULL) {                                                \
        printf("  FAIL: %s (got NULL)\n", (msg));                       \
        printf("        at %s:%d\n", __FILE__, __LINE__);               \
        fflush(stdout);                                                 \
        if (PyErr_Occurred()) {                                         \
            PyErr_Print();                                              \
            fflush(stderr);                                             \
        }                                                               \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_NULL(ptr, msg) do {                                 \
    if ((ptr) != NULL) {                                                \
        printf("  FAIL: %s (expected NULL)\n", (msg));                  \
        printf("        at %s:%d\n", __FILE__, __LINE__);               \
        fflush(stdout);                                                 \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_REFCNT(obj, expected, msg) do {                     \
    Py_ssize_t _refcnt = Py_REFCNT(obj);                                \
    if (_refcnt != (expected)) {                                        \
        printf("  FAIL: %s (refcount: expected %ld, got %ld)\n",        \
                (msg), (long)(expected), (long)_refcnt);                \
        printf("        at %s:%d\n", __FILE__, __LINE__);               \
        fflush(stdout);                                                 \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_SAME(a, b, msg) do {                                \
    if ((void *)(a) != (void *)(b)) {                                   \
        printf("  FAIL: %s (objects not identical)\n", (msg));          \
        printf("        at %s:%d\n", __FILE__, __LINE__);               \
        fflush(stdout);                                                 \
        return -1;                                                      \
    }                                                                   \
} while(0)


#define TEST_ASSERT_NOT_SAME(a, b, msg) do {                            \
    if ((void *)(a) == (void *)(b)) {                                   \
        printf("  FAIL: %s (objects should not be identical)\n",        \
                (msg));                                                 \
        printf("        at %s:%d\n", __FILE__, __LINE__);               \
        fflush(stdout);                                                 \
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

int run_arena_tests(void);
int run_atom_tests(void);
int run_pair_tests(void);
int run_matcher_tests(void);
int run_namespace_tests(void);
int run_bytecode_tests(void);
int run_formals_tests(void);
int run_values_tests(void);
int run_stack_tests(void);
int run_dynchain_tests(void);
int run_eval_tests(void);
int run_paramref_tests(void);
int run_property_tests(void);
int run_partial_tests(void);
int run_demand_tests(void);
int run_iter_request_tests(void);
int run_iterate_tests(void);
int run_resume_tests(void);
int run_breaker_tests(void);
int run_domain_tests(void);
int run_lifecycle_tests(void);

/* Future units:
int run_tco_tests(void);
*/


#endif


/* The end. */
