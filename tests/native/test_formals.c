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
   Memory tests for Formals and FormalsSpec types

   Tests the formals parameter specification and application mechanisms.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === FormalsSpec tests === */


static int test_formals_spec_create_empty(void) {
    /*
      Test that FormalsSpec can be created with no arguments.
    */
    PyObject *fspec = KosFormalsSpec_New(NULL, NULL, NULL, NULL, NULL, NULL,
                                         NULL);
    TEST_ASSERT_NOT_NULL(fspec, "create empty FormalsSpec");

    TEST_ASSERT(KosFormalsSpec_CheckExact(fspec), "FormalsSpec type check");
    TEST_ASSERT_NULL(KosFormalsSpec_POS(fspec), "pos is NULL");
    TEST_ASSERT_NULL(KosFormalsSpec_POS_ONLY(fspec), "pos_only is NULL");
    TEST_ASSERT_NULL(KosFormalsSpec_KWD(fspec), "kwd is NULL");
    TEST_ASSERT_NULL(KosFormalsSpec_KWD_ONLY(fspec), "kwd_only is NULL");
    TEST_ASSERT_NULL(KosFormalsSpec_STAR(fspec), "star is NULL");
    TEST_ASSERT_NULL(KosFormalsSpec_STARSTAR(fspec), "starstar is NULL");
    TEST_ASSERT_NULL(KosFormalsSpec_BY_REFS(fspec), "by_refs is NULL");

    Py_DECREF(fspec);
    return 0;
}


static int test_formals_spec_create_with_pos(void) {
    /*
      Test that FormalsSpec can be created with positional arguments.
    */
    PyObject *pos = Py_BuildValue("(ss)", "a", "b");
    TEST_ASSERT_NOT_NULL(pos, "create pos tuple");

    PyObject *fspec = KosFormalsSpec_New(pos, NULL, NULL, NULL, NULL, NULL,
                                         NULL);
    TEST_ASSERT_NOT_NULL(fspec, "create FormalsSpec with pos");

    TEST_ASSERT(KosFormalsSpec_CheckExact(fspec), "FormalsSpec type check");
    TEST_ASSERT_NOT_NULL(KosFormalsSpec_POS(fspec), "pos is not NULL");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(KosFormalsSpec_POS(fspec)), 2,
                   "pos has 2 elements");

    Py_DECREF(fspec);
    Py_DECREF(pos);
    return 0;
}


static int test_formals_spec_create_with_star(void) {
    /*
      Test that FormalsSpec can be created with star arguments.
    */
    PyObject *star = PyUnicode_FromString("args");
    PyObject *starstar = PyUnicode_FromString("kwargs");
    TEST_ASSERT_NOT_NULL(star, "create star string");
    TEST_ASSERT_NOT_NULL(starstar, "create starstar string");

    PyObject *fspec = KosFormalsSpec_New(NULL, NULL, NULL, NULL, star, starstar,
                                         NULL);
    TEST_ASSERT_NOT_NULL(fspec, "create FormalsSpec with star");

    TEST_ASSERT(KosFormalsSpec_CheckExact(fspec), "FormalsSpec type check");
    TEST_ASSERT_NOT_NULL(KosFormalsSpec_STAR(fspec), "star is not NULL");
    TEST_ASSERT_NOT_NULL(KosFormalsSpec_STARSTAR(fspec), "starstar is not NULL");

    Py_DECREF(fspec);
    Py_DECREF(starstar);
    Py_DECREF(star);
    return 0;
}


static int test_formals_spec_full(void) {
    /*
      Test FormalsSpec with all fields populated.
    */
    PyObject *pos = Py_BuildValue("(s)", "a");
    PyObject *pos_only = Py_BuildValue("(s)", "b");
    PyObject *kwd = Py_BuildValue("(s)", "c");
    PyObject *kwd_only = Py_BuildValue("(s)", "d");
    PyObject *star = PyUnicode_FromString("args");
    PyObject *starstar = PyUnicode_FromString("kwargs");

    TEST_ASSERT_NOT_NULL(pos, "create pos");
    TEST_ASSERT_NOT_NULL(pos_only, "create pos_only");
    TEST_ASSERT_NOT_NULL(kwd, "create kwd");
    TEST_ASSERT_NOT_NULL(kwd_only, "create kwd_only");
    TEST_ASSERT_NOT_NULL(star, "create star");
    TEST_ASSERT_NOT_NULL(starstar, "create starstar");

    PyObject *fspec = KosFormalsSpec_New(pos, pos_only, kwd, kwd_only,
                                         star, starstar, NULL);
    TEST_ASSERT_NOT_NULL(fspec, "create full FormalsSpec");

    TEST_ASSERT_EQ(PyTuple_GET_SIZE(KosFormalsSpec_POS(fspec)), 1, "pos size");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(KosFormalsSpec_POS_ONLY(fspec)), 1,
                   "pos_only size");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(KosFormalsSpec_KWD(fspec)), 1, "kwd size");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(KosFormalsSpec_KWD_ONLY(fspec)), 1,
                   "kwd_only size");

    Py_DECREF(fspec);
    Py_DECREF(starstar);
    Py_DECREF(star);
    Py_DECREF(kwd_only);
    Py_DECREF(kwd);
    Py_DECREF(pos_only);
    Py_DECREF(pos);
    return 0;
}


static int test_formals_spec_refcount(void) {
    /*
      Test that FormalsSpec properly manages reference counts.

      PySequence_Tuple on a tuple returns the same tuple with incremented
      refcount, so our original pos gets retained by the FormalsSpec.
    */
    PyObject *pos = Py_BuildValue("(ss)", "x", "y");
    TEST_ASSERT_NOT_NULL(pos, "create pos tuple");
    Py_ssize_t initial_refcnt = Py_REFCNT(pos);

    PyObject *fspec = KosFormalsSpec_New(pos, NULL, NULL, NULL, NULL, NULL,
                                         NULL);
    TEST_ASSERT_NOT_NULL(fspec, "create FormalsSpec");

    /* FormalsSpec retains the tuple, so refcount should be incremented */
    TEST_ASSERT_EQ(Py_REFCNT(pos), initial_refcnt + 1, "pos refcount incremented");

    Py_DECREF(fspec);

    /* After releasing fspec, refcount should be back to initial */
    TEST_ASSERT_EQ(Py_REFCNT(pos), initial_refcnt, "pos refcount restored");

    Py_DECREF(pos);
    return 0;
}


/* === Formals tests === */


static int test_formals_create_from_spec(void) {
    /*
      Test creating Formals from FormalsSpec.
    */
    PyObject *pos = Py_BuildValue("(ss)", "a", "b");
    TEST_ASSERT_NOT_NULL(pos, "create pos tuple");

    PyObject *fspec = KosFormalsSpec_New(pos, NULL, NULL, NULL, NULL, NULL,
                                         NULL);
    TEST_ASSERT_NOT_NULL(fspec, "create FormalsSpec");

    PyObject *formals = KosFormals_New(fspec, NULL);
    TEST_ASSERT_NOT_NULL(formals, "create Formals from spec");

    TEST_ASSERT(KosFormals_Check(formals), "Formals type check");
    TEST_ASSERT_NOT_NULL(KosFormals_POS(formals), "Formals pos not NULL");
    TEST_ASSERT_NULL(KosFormals_DEFAULTS(formals), "Formals defaults NULL");

    Py_DECREF(formals);
    Py_DECREF(fspec);
    Py_DECREF(pos);
    return 0;
}


static int test_formals_create_full(void) {
    /*
      Test creating Formals with all fields.
    */
    PyObject *pos = NULL;
    PyObject *kwd = NULL;
    PyObject *star = NULL;
    PyObject *defaults = NULL;
    PyObject *formals = NULL;
    int result = 0;

    pos = Py_BuildValue("(s)", "foo");
    kwd = Py_BuildValue("(s)", "bar");
    star = PyUnicode_FromString("args");
    defaults = PyDict_New();
    if (! pos || ! kwd || ! star || ! defaults)
        goto cleanup;

    PyObject *default_bar = PyLong_FromLong(42);
    if (! default_bar)
        goto cleanup;
    PyDict_SetItemString(defaults, "bar", default_bar);
    Py_DECREF(default_bar);

    formals = KosFormals_NewFull(pos, NULL, kwd, NULL,
                                star, NULL, defaults, NULL);
    if (! formals)
        goto cleanup;

    if (! KosFormals_Check(formals)) {
        fprintf(stderr, "  FAIL: Formals type check\n");
        result = -1;
        goto cleanup;
    }
    if (! KosFormals_POS(formals) || ! KosFormals_KWD(formals)
        || ! KosFormals_STAR(formals) || ! KosFormals_DEFAULTS(formals)) {
        fprintf(stderr, "  FAIL: formals fields not NULL\n");
        result = -1;
        goto cleanup;
    }

 cleanup:
    Py_XDECREF(formals);
    Py_XDECREF(defaults);
    Py_XDECREF(star);
    Py_XDECREF(kwd);
    Py_XDECREF(pos);
    return result;
}


static int test_formals_apply_simple(void) {
    /*
      Test applying formals to a namespace with simple positional args.
    */
    PyObject *pos = Py_BuildValue("(ss)", "a", "b");
    TEST_ASSERT_NOT_NULL(pos, "create pos tuple");

    PyObject *formals = KosFormals_NewFull(pos, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(formals, "create Formals");

    PyObject *ns = KosNamespace_NewFull(NULL, NULL, pos, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create Namespace");

    PyObject *args = Py_BuildValue("(ii)", 1, 2);
    TEST_ASSERT_NOT_NULL(args, "create args tuple");

    int result = KosFormals_Apply(formals, ns, args, NULL);
    TEST_ASSERT_EQ(result, 0, "apply succeeds");

    PyObject *a_val = KosNamespace_Deref(ns, PyTuple_GET_ITEM(pos, 0));
    TEST_ASSERT_NOT_NULL(a_val, "get a value");
    TEST_ASSERT_EQ(PyLong_AsLong(a_val), 1, "a == 1");
    Py_DECREF(a_val);

    PyObject *b_val = KosNamespace_Deref(ns, PyTuple_GET_ITEM(pos, 1));
    TEST_ASSERT_NOT_NULL(b_val, "get b value");
    TEST_ASSERT_EQ(PyLong_AsLong(b_val), 2, "b == 2");
    Py_DECREF(b_val);

    Py_DECREF(args);
    Py_DECREF(ns);
    Py_DECREF(formals);
    Py_DECREF(pos);
    return 0;
}


static int test_formals_apply_with_kwargs(void) {
    /*
      Test applying formals with keyword arguments.
    */
    PyObject *pos = Py_BuildValue("(s)", "a");
    PyObject *kwd = Py_BuildValue("(s)", "b");
    TEST_ASSERT_NOT_NULL(pos, "create pos");
    TEST_ASSERT_NOT_NULL(kwd, "create kwd");

    PyObject *local_syms = Py_BuildValue("(ss)", "a", "b");
    TEST_ASSERT_NOT_NULL(local_syms, "create local_syms");

    PyObject *formals = KosFormals_NewFull(pos, NULL, kwd, NULL,
                                           NULL, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(formals, "create Formals");

    PyObject *ns = KosNamespace_NewFull(NULL, NULL, local_syms, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create Namespace");

    PyObject *args = Py_BuildValue("(i)", 100);
    PyObject *kwargs = PyDict_New();
    PyDict_SetItemString(kwargs, "b", PyLong_FromLong(200));
    TEST_ASSERT_NOT_NULL(args, "create args");
    TEST_ASSERT_NOT_NULL(kwargs, "create kwargs");

    int result = KosFormals_Apply(formals, ns, args, kwargs);
    TEST_ASSERT_EQ(result, 0, "apply succeeds");

    PyObject *a_name = PyUnicode_FromString("a");
    PyObject *b_name = PyUnicode_FromString("b");

    PyObject *a_val = KosNamespace_Deref(ns, a_name);
    TEST_ASSERT_NOT_NULL(a_val, "get a value");
    TEST_ASSERT_EQ(PyLong_AsLong(a_val), 100, "a == 100");
    Py_DECREF(a_val);

    PyObject *b_val = KosNamespace_Deref(ns, b_name);
    TEST_ASSERT_NOT_NULL(b_val, "get b value");
    TEST_ASSERT_EQ(PyLong_AsLong(b_val), 200, "b == 200");
    Py_DECREF(b_val);

    Py_DECREF(b_name);
    Py_DECREF(a_name);
    Py_DECREF(kwargs);
    Py_DECREF(args);
    Py_DECREF(ns);
    Py_DECREF(formals);
    Py_DECREF(local_syms);
    Py_DECREF(kwd);
    Py_DECREF(pos);
    return 0;
}


static int test_formals_apply_with_star(void) {
    /*
      Test applying formals with star (variadic) arguments.
    */
    PyObject *pos = Py_BuildValue("(s)", "a");
    PyObject *star = PyUnicode_FromString("rest");
    TEST_ASSERT_NOT_NULL(pos, "create pos");
    TEST_ASSERT_NOT_NULL(star, "create star");

    PyObject *local_syms = Py_BuildValue("(ss)", "a", "rest");
    TEST_ASSERT_NOT_NULL(local_syms, "create local_syms");

    PyObject *formals = KosFormals_NewFull(pos, NULL, NULL, NULL,
                                           star, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(formals, "create Formals");

    PyObject *ns = KosNamespace_NewFull(NULL, NULL, local_syms, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create Namespace");

    PyObject *args = Py_BuildValue("(iiii)", 1, 2, 3, 4);
    TEST_ASSERT_NOT_NULL(args, "create args");

    int result = KosFormals_Apply(formals, ns, args, NULL);
    TEST_ASSERT_EQ(result, 0, "apply succeeds");

    PyObject *a_name = PyUnicode_FromString("a");
    PyObject *rest_name = PyUnicode_FromString("rest");

    PyObject *a_val = KosNamespace_Deref(ns, a_name);
    TEST_ASSERT_NOT_NULL(a_val, "get a value");
    TEST_ASSERT_EQ(PyLong_AsLong(a_val), 1, "a == 1");
    Py_DECREF(a_val);

    PyObject *rest_val = KosNamespace_Deref(ns, rest_name);
    TEST_ASSERT_NOT_NULL(rest_val, "get rest value");
    TEST_ASSERT(PyTuple_Check(rest_val), "rest is tuple");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(rest_val), 3, "rest has 3 elements");
    Py_DECREF(rest_val);

    Py_DECREF(rest_name);
    Py_DECREF(a_name);
    Py_DECREF(args);
    Py_DECREF(ns);
    Py_DECREF(formals);
    Py_DECREF(local_syms);
    Py_DECREF(star);
    Py_DECREF(pos);
    return 0;
}


static int test_formals_apply_with_defaults(void) {
    /*
      Test applying formals with default values.
    */
    PyObject *pos = NULL;
    PyObject *kwd = NULL;
    PyObject *defaults = NULL;
    PyObject *local_syms = NULL;
    PyObject *formals = NULL;
    PyObject *ns = NULL;
    PyObject *args = NULL;
    PyObject *a_name = NULL;
    PyObject *b_name = NULL;
    PyObject *a_val = NULL;
    PyObject *b_val = NULL;
    int result = 0;

    pos = Py_BuildValue("(s)", "a");
    kwd = Py_BuildValue("(s)", "b");
    defaults = PyDict_New();
    if (! pos || ! kwd || ! defaults)
        goto cleanup;
    PyObject *default_b = PyLong_FromLong(999);
    if (! default_b)
        goto cleanup;
    PyDict_SetItemString(defaults, "b", default_b);
    Py_DECREF(default_b);

    local_syms = Py_BuildValue("(ss)", "a", "b");
    if (! local_syms)
        goto cleanup;

    formals = KosFormals_NewFull(pos, NULL, kwd, NULL,
                                NULL, NULL, defaults, NULL);
    if (! formals)
        goto cleanup;

    ns = KosNamespace_NewFull(NULL, NULL, local_syms, NULL, NULL);
    if (! ns)
        goto cleanup;

    args = Py_BuildValue("(i)", 42);
    if (! args)
        goto cleanup;

    if (KosFormals_Apply(formals, ns, args, NULL) != 0) {
        fprintf(stderr, "  FAIL: apply succeeds\n");
        result = -1;
        goto cleanup;
    }

    a_name = PyUnicode_FromString("a");
    b_name = PyUnicode_FromString("b");
    if (! a_name || ! b_name)
        goto cleanup;

    a_val = KosNamespace_Deref(ns, a_name);
    if (! a_val) {
        fprintf(stderr, "  FAIL: get a value\n");
        result = -1;
        goto cleanup;
    }
    if (PyLong_AsLong(a_val) != 42) {
        fprintf(stderr, "  FAIL: a == 42\n");
        result = -1;
        Py_DECREF(a_val);
        goto cleanup;
    }
    Py_DECREF(a_val);
    a_val = NULL;

    b_val = KosNamespace_Deref(ns, b_name);
    if (! b_val) {
        fprintf(stderr, "  FAIL: get b value\n");
        result = -1;
        goto cleanup;
    }
    if (PyLong_AsLong(b_val) != 999) {
        fprintf(stderr, "  FAIL: b == 999 (default)\n");
        result = -1;
        Py_DECREF(b_val);
        goto cleanup;
    }
    Py_DECREF(b_val);
    b_val = NULL;

 cleanup:
    Py_XDECREF(b_val);
    Py_XDECREF(a_val);
    Py_XDECREF(b_name);
    Py_XDECREF(a_name);
    Py_XDECREF(args);
    Py_XDECREF(ns);
    Py_XDECREF(formals);
    Py_XDECREF(local_syms);
    Py_XDECREF(defaults);
    Py_XDECREF(kwd);
    Py_XDECREF(pos);
    return result;
}


static int test_formals_apply_missing_arg_error(void) {
    /*
      Test that applying formals with missing required argument raises error.
    */
    PyObject *pos = Py_BuildValue("(ss)", "a", "b");
    TEST_ASSERT_NOT_NULL(pos, "create pos");

    PyObject *formals = KosFormals_NewFull(pos, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(formals, "create Formals");

    PyObject *ns = KosNamespace_NewFull(NULL, NULL, pos, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create Namespace");

    /* Only provide one argument when two are required */
    PyObject *args = Py_BuildValue("(i)", 1);
    TEST_ASSERT_NOT_NULL(args, "create args");

    int result = KosFormals_Apply(formals, ns, args, NULL);
    TEST_ASSERT_EQ(result, -1, "apply fails with missing arg");
    TEST_ASSERT(PyErr_Occurred(), "error is set");

    PyErr_Clear();

    Py_DECREF(args);
    Py_DECREF(ns);
    Py_DECREF(formals);
    Py_DECREF(pos);
    return 0;
}


static int test_formals_apply_too_many_args_error(void) {
    /*
      Test that applying formals with too many arguments raises error.
    */
    PyObject *pos = Py_BuildValue("(s)", "a");
    TEST_ASSERT_NOT_NULL(pos, "create pos");

    PyObject *formals = KosFormals_NewFull(pos, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(formals, "create Formals");

    PyObject *ns = KosNamespace_NewFull(NULL, NULL, pos, NULL, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create Namespace");

    /* Provide too many arguments */
    PyObject *args = Py_BuildValue("(iii)", 1, 2, 3);
    TEST_ASSERT_NOT_NULL(args, "create args");

    int result = KosFormals_Apply(formals, ns, args, NULL);
    TEST_ASSERT_EQ(result, -1, "apply fails with too many args");
    TEST_ASSERT(PyErr_Occurred(), "error is set");

    PyErr_Clear();

    Py_DECREF(args);
    Py_DECREF(ns);
    Py_DECREF(formals);
    Py_DECREF(pos);
    return 0;
}


/* === FormalsSpec namespace_spec tests === */


static int test_formals_spec_namespace_spec(void) {
    /*
      Test that FormalsSpec.namespace_spec creates proper NamespaceSpec.
    */
    PyObject *pos = Py_BuildValue("(ss)", "a", "b");
    PyObject *kwd = Py_BuildValue("(s)", "c");
    TEST_ASSERT_NOT_NULL(pos, "create pos");
    TEST_ASSERT_NOT_NULL(kwd, "create kwd");

    PyObject *fspec = KosFormalsSpec_New(pos, NULL, kwd, NULL, NULL, NULL,
                                         NULL);
    TEST_ASSERT_NOT_NULL(fspec, "create FormalsSpec");

    PyObject *nspec = KosFormalsSpec_NamespaceSpec(fspec, NULL);
    TEST_ASSERT_NOT_NULL(nspec, "create NamespaceSpec from FormalsSpec");
    TEST_ASSERT(KosNamespaceSpec_CheckExact(nspec), "is NamespaceSpec");

    /* Should have locals for a, b, c */
    PyObject *locals = KosNamespaceSpec_LOCAL_SYMS(nspec);
    TEST_ASSERT_NOT_NULL(locals, "locals not NULL");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(locals), 3, "3 local symbols");

    Py_DECREF(nspec);
    Py_DECREF(fspec);
    Py_DECREF(kwd);
    Py_DECREF(pos);
    return 0;
}


/* === stress tests === */


static int test_formals_stress_alloc(void) {
    /*
      Stress test: create and destroy many formals.
    */
    PyObject *pos = Py_BuildValue("(ss)", "x", "y");
    TEST_ASSERT_NOT_NULL(pos, "create pos");

    for (int i = 0; i < 100; i++) {
        PyObject *fspec = KosFormalsSpec_New(pos, NULL, NULL, NULL, NULL, NULL,
                                             NULL);
        TEST_ASSERT_NOT_NULL(fspec, "create FormalsSpec");

        PyObject *formals = KosFormals_New(fspec, NULL);
        TEST_ASSERT_NOT_NULL(formals, "create Formals");

        Py_DECREF(formals);
        Py_DECREF(fspec);
    }

    Py_DECREF(pos);
    return 0;
}


/* === test suite runner === */


int run_formals_tests(void) {
    BEGIN_TEST_SUITE("Formals Memory Tests");

    /* FormalsSpec tests */
    RUN_TEST(test_formals_spec_create_empty);
    RUN_TEST(test_formals_spec_create_with_pos);
    RUN_TEST(test_formals_spec_create_with_star);
    RUN_TEST(test_formals_spec_full);
    RUN_TEST(test_formals_spec_refcount);
    RUN_TEST(test_formals_spec_namespace_spec);

    /* Formals tests */
    RUN_TEST(test_formals_create_from_spec);
    RUN_TEST(test_formals_create_full);

    /* Apply tests */
    RUN_TEST(test_formals_apply_simple);
    RUN_TEST(test_formals_apply_with_kwargs);
    RUN_TEST(test_formals_apply_with_star);
    RUN_TEST(test_formals_apply_with_defaults);
    RUN_TEST(test_formals_apply_missing_arg_error);
    RUN_TEST(test_formals_apply_too_many_args_error);

    /* Stress tests */
    RUN_TEST(test_formals_stress_alloc);

    return 0;
}


/* The end. */
