/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   tests/native/test_paramref.c

   Native tests for ParamRef: deref, store, clear, isEmpty with
   active domain scope.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === Helpers === */


static PyObject *make_instr(int op, PyObject *arg) {
    PyObject *pair = PyTuple_New(2);
    PyTuple_SET_ITEM(pair, 0, PyLong_FromLong(op));
    Py_INCREF(arg);
    PyTuple_SET_ITEM(pair, 1, arg);
    return pair;
}


static PyObject *make_instr_int(int op, int arg) {
    PyObject *pair = PyTuple_New(2);
    PyTuple_SET_ITEM(pair, 0, PyLong_FromLong(op));
    PyTuple_SET_ITEM(pair, 1, PyLong_FromLong(arg));
    return pair;
}


static PyObject *create_minimal_code(void) {
    PyObject *name = PyUnicode_FromString("test");
    PyObject *locals = PyTuple_New(0);
    PyObject *closures = PyTuple_New(0);

    PyObject *nspec = KosNamespaceSpec_New(name, locals, closures);
    Py_DECREF(name);
    Py_DECREF(locals);
    Py_DECREF(closures);

    if (! nspec)
        return NULL;

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return code;
}


/* === Test: ParamRef deref with default when no domain === */


static int test_paramref_deref_no_domain_returns_default(void) {
    PyObject *key = PyUnicode_FromString("foo");
    PyObject *default_val = PyLong_FromLong(42);
    PyObject *param_ref;
    PyObject *value;

    TEST_ASSERT_NOT_NULL(key, "create key");
    TEST_ASSERT_NOT_NULL(default_val, "create default");
    param_ref = KosParamRef_New(key, default_val);
    TEST_ASSERT_NOT_NULL(param_ref, "create ParamRef");

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref returns value");
    TEST_ASSERT(PyLong_Check(value), "value is int");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 42, "value is default");

    Py_DECREF(value);
    Py_DECREF(param_ref);
    Py_DECREF(default_val);
    Py_DECREF(key);

    return 0;
}


/* === Test: ParamRef deref no domain and no default raises === */


static int test_paramref_deref_no_domain_no_default_raises(void) {
    PyObject *key = PyUnicode_FromString("foo");
    PyObject *param_ref;
    PyObject *value;

    TEST_ASSERT_NOT_NULL(key, "create key");
    param_ref = KosParamRef_New(key, NULL);
    TEST_ASSERT_NOT_NULL(param_ref, "create ParamRef");

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NULL(value, "deref returns NULL");
    TEST_ASSERT(PyErr_Occurred(), "error set");

    PyErr_Clear();
    Py_DECREF(param_ref);
    Py_DECREF(key);

    return 0;
}


/* === Test: ParamRef store no domain raises === */


static int test_paramref_store_no_domain_raises(void) {
    PyObject *key = PyUnicode_FromString("foo");
    PyObject *default_val = PyLong_FromLong(0);
    PyObject *param_ref;
    PyObject *val = PyLong_FromLong(99);
    int res;

    TEST_ASSERT_NOT_NULL(key, "create key");
    param_ref = KosParamRef_New(key, default_val);
    TEST_ASSERT_NOT_NULL(param_ref, "create ParamRef");

    res = KosRef_Store(param_ref, val);
    TEST_ASSERT(res == -1, "store returns -1");
    TEST_ASSERT(PyErr_Occurred(), "error set");

    PyErr_Clear();
    Py_DECREF(val);
    Py_DECREF(param_ref);
    Py_DECREF(default_val);
    Py_DECREF(key);

    return 0;
}


/* === Test: ParamRef store and deref inside active scope === */


static int test_paramref_store_deref_inside_scope(void) {
    PyObject *code_obj;
    PyObject *ns;
    KosDynFrame *frame;
    KosContext *ctx;
    PyObject *key;
    PyObject *param_ref;
    PyObject *stored;
    PyObject *value;
    int res;

    code_obj = create_minimal_code();
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    key = PyUnicode_FromString("x");
    param_ref = KosParamRef_New(key, Py_None);
    TEST_ASSERT_NOT_NULL(param_ref, "create ParamRef");

    kos_domain_push_scope(ctx);

    stored = PyLong_FromLong(123);
    res = KosRef_Store(param_ref, stored);
    TEST_ASSERT(res == 0, "store succeeds");
    Py_DECREF(stored);

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref returns value");
    TEST_ASSERT(PyLong_Check(value), "value is int");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 123, "value is stored");

    Py_DECREF(value);

    kos_domain_pop_scope(ctx);

    Py_DECREF(param_ref);
    Py_DECREF(key);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: ParamRef clear inside scope === */


static int test_paramref_clear_inside_scope(void) {
    PyObject *code_obj;
    PyObject *ns;
    KosDynFrame *frame;
    KosContext *ctx;
    PyObject *key;
    PyObject *param_ref;
    PyObject *value;
    int empty;

    code_obj = create_minimal_code();
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    key = PyUnicode_FromString("y");
    param_ref = KosParamRef_New(key, PyLong_FromLong(99));
    TEST_ASSERT_NOT_NULL(param_ref, "create ParamRef");

    kos_domain_push_scope(ctx);

    KosRef_Store(param_ref, PyLong_FromLong(77));

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref before clear");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 77, "stored value");
    Py_DECREF(value);

    KosRef_Clear(param_ref);

    empty = KosRef_IsEmpty(param_ref);
    TEST_ASSERT(empty == 1, "empty after clear");

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref after clear returns default");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 99, "returns default");
    Py_DECREF(value);

    kos_domain_pop_scope(ctx);

    Py_DECREF(param_ref);
    Py_DECREF(key);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: ParamRef isEmpty inside scope === */


static int test_paramref_is_empty_inside_scope(void) {
    PyObject *code_obj;
    PyObject *ns;
    KosDynFrame *frame;
    KosContext *ctx;
    PyObject *key;
    PyObject *param_ref;
    int empty;

    code_obj = create_minimal_code();
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    key = PyUnicode_FromString("z");
    param_ref = KosParamRef_New(key, Py_None);
    TEST_ASSERT_NOT_NULL(param_ref, "create ParamRef");

    kos_domain_push_scope(ctx);

    empty = KosRef_IsEmpty(param_ref);
    TEST_ASSERT(empty == 1, "empty before store");

    KosRef_Store(param_ref, PyLong_FromLong(1));

    empty = KosRef_IsEmpty(param_ref);
    TEST_ASSERT(empty == 0, "not empty after store");

    kos_domain_pop_scope(ctx);

    Py_DECREF(param_ref);
    Py_DECREF(key);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: kos_dyn_push_param / kos_dyn_pop_param enter and restore === */


static int test_dynparam_push_pop_restore(void) {
    PyObject *code_obj;
    PyObject *ns;
    KosDynFrame *frame;
    KosContext *ctx;
    PyObject *key;
    PyObject *param_ref;
    PyObject *params[1];
    PyObject *values[1];
    KosDynParam *param_node;
    PyObject *value;
    long val;

    code_obj = create_minimal_code();
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    key = PyUnicode_FromString("dynparam");
    param_ref = KosParamRef_New(key, PyLong_FromLong(0));
    TEST_ASSERT_NOT_NULL(param_ref, "create ParamRef");

    kos_domain_push_scope(ctx);

    /* Initially unset; deref returns default */
    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref before push");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 0, "default value");
    Py_DECREF(value);

    /* Push param extent with value 999 */
    params[0] = param_ref;
    values[0] = PyLong_FromLong(999);
    param_node = kos_dyn_push_param(ctx, 1, params, values);
    TEST_ASSERT_NOT_NULL(param_node, "push param");
    Py_DECREF(values[0]);

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref inside extent");
    val = PyLong_AsLong(value);
    TEST_ASSERT(val == 999, "value is 999");
    Py_DECREF(value);

    kos_dyn_pop_param(ctx, param_node);

    /* After pop, restored to prior (unset -> default) */
    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref after pop");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 0, "restored to default");
    Py_DECREF(value);

    kos_domain_pop_scope(ctx);

    Py_DECREF(param_ref);
    Py_DECREF(key);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: nested param extent restore === */


static int test_dynparam_nested_restore(void) {
    PyObject *code_obj;
    PyObject *ns;
    KosDynFrame *frame;
    KosContext *ctx;
    PyObject *key;
    PyObject *param_ref;
    PyObject *params[1];
    PyObject *values_outer[1];
    PyObject *values_inner[1];
    KosDynParam *outer_node;
    KosDynParam *inner_node;
    PyObject *value;

    code_obj = create_minimal_code();
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    ns = PyDict_New();
    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    ctx = kos_context_new(frame);
    key = PyUnicode_FromString("nested");
    param_ref = KosParamRef_New(key, PyLong_FromLong(0));
    TEST_ASSERT_NOT_NULL(param_ref, "create ParamRef");

    kos_domain_push_scope(ctx);

    params[0] = param_ref;
    values_outer[0] = PyLong_FromLong(8);
    outer_node = kos_dyn_push_param(ctx, 1, params, values_outer);
    TEST_ASSERT_NOT_NULL(outer_node, "push outer");
    Py_DECREF(values_outer[0]);

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref outer");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 8, "outer value");
    Py_DECREF(value);

    values_inner[0] = PyLong_FromLong(16);
    inner_node = kos_dyn_push_param(ctx, 1, params, values_inner);
    TEST_ASSERT_NOT_NULL(inner_node, "push inner");
    Py_DECREF(values_inner[0]);

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref inner");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 16, "inner value");
    Py_DECREF(value);

    kos_dyn_pop_param(ctx, inner_node);

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref after inner pop");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 8, "restored to outer");
    Py_DECREF(value);

    kos_dyn_pop_param(ctx, outer_node);

    value = KosRef_Deref(param_ref);
    TEST_ASSERT_NOT_NULL(value, "deref after outer pop");
    TEST_ASSERT_EQ(PyLong_AsLong(value), 0, "restored to default");
    Py_DECREF(value);

    kos_domain_pop_scope(ctx);
    Py_DECREF(param_ref);
    Py_DECREF(key);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test runner === */


int run_paramref_tests(void) {
    BEGIN_TEST_SUITE("ParamRef");
    RUN_TEST(test_paramref_deref_no_domain_returns_default);
    RUN_TEST(test_paramref_deref_no_domain_no_default_raises);
    RUN_TEST(test_paramref_store_no_domain_raises);
    RUN_TEST(test_paramref_store_deref_inside_scope);
    RUN_TEST(test_paramref_clear_inside_scope);
    RUN_TEST(test_paramref_is_empty_inside_scope);
    RUN_TEST(test_dynparam_push_pop_restore);
    RUN_TEST(test_dynparam_nested_restore);
    return 0;
}


// The end.
