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
   tests/native/test_eval.c

   Native tests for the evaluator: opcode dispatch, OBJECT_ADD,
   and delimited continuation (reset/shift/k) behavior.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === Helpers === */


static PyObject *create_test_nspec(void) {

    PyObject *name = PyUnicode_FromString("test");
    PyObject *locals = PyTuple_New(0);
    PyObject *closures = PyTuple_New(0);

    PyObject *nspec = KosNamespaceSpec_New(name, locals, closures);

    Py_DECREF(name);
    Py_DECREF(locals);
    Py_DECREF(closures);

    return nspec;
}


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


/* === Test: OBJECT_ADD and RETURN === */


static int test_eval_object_add_return(void) {
    /*
      Run bytecode: CONST 1, CONST 5, OBJECT_ADD 2, RETURN.
      Expect KOS_EV_RETURN with value 6.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *five = PyLong_FromLong(5);

    PyObject *instr = PyList_New(4);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, five));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_OBJECT_ADD, 2));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(one);
    Py_DECREF(five);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = kos_drive(ctx, &ev);

    TEST_ASSERT(res == 0, "kos_drive succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyLong_Check(ev.payload), "payload is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 6, "result is 6");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: reset (+ 1 (shift k (k 5))) => 6 === */


static int test_eval_reset_shift_invoke(void) {
    /*
      Bytecode: PUSH_PROMPT 0, SET_PROMPT_HANDLER 8, CONST 1, SHIFT 0,
      OBJECT_ADD 2, POP_PROMPT, RETURN. Handler at 8: LABEL 8, CONST 5,
      INVOKE_K, RETURN. Expect final KOS_EV_RETURN with value 6.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *zero = PyLong_FromLong(0);
    PyObject *one = PyLong_FromLong(1);
    PyObject *five = PyLong_FromLong(5);
    PyObject *label_8 = PyLong_FromLong(8);

    PyObject *instr = PyList_New(12);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_PUSH_PROMPT, zero));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_SET_PROMPT_HANDLER, label_8));
    PyList_SET_ITEM(instr, 2, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_SHIFT, zero));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_OBJECT_ADD, 2));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_POP_PROMPT, 0));
    PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_RETURN, 0));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_NOOP, 0));
    PyList_SET_ITEM(instr, 8, make_instr(KOS_OP_LABEL, label_8));
    PyList_SET_ITEM(instr, 9, make_instr(KOS_OP_CONST, five));
    PyList_SET_ITEM(instr, 10, make_instr_int(KOS_OP_INVOKE_K, 0));
    PyList_SET_ITEM(instr, 11, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(zero);
    Py_DECREF(one);
    Py_DECREF(five);
    Py_DECREF(label_8);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = kos_drive(ctx, &ev);

    if (res != 0) {
      if (PyErr_Occurred())
        PyErr_Print();
      else
        fprintf(stderr, "  kos_drive returned %d (no exception set)\n", res);
      fflush(stderr);
    }
    TEST_ASSERT(res == 0, "kos_drive succeeded");
    if (ev.kind != KOS_EV_RETURN) {
      fprintf(stderr, "  ev.kind=%d (expected KOS_EV_RETURN), payload=%p\n",
              (int) ev.kind, (void *) ev.payload);
      fflush(stderr);
    }
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyLong_Check(ev.payload), "payload is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 6, "result is 6");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test runner === */


int run_eval_tests(void) {

    BEGIN_TEST_SUITE("Eval Tests");

    RUN_TEST(test_eval_object_add_return);
    RUN_TEST(test_eval_reset_shift_invoke);

    return 0;
}


// The end.
