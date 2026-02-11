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
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 1));

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
    int res = koskari_eval(ctx, &ev);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyLong_Check(ev.payload), "payload is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 6, "result is 6");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: try/except - raise caught, handler result returned === */


static int test_eval_try_except_catch(void) {
    /*
      Bytecode: PUSH_TRY 3, CONST (ValueError), RAISE 1, EXCEPTION, POP 1,
      CONST 42, POP_TRY, RETURN. Handler at index 3. Expect KOS_EV_RETURN 42.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *exc_inst = PyObject_CallFunction(
        (PyObject *) PyExc_ValueError, "s", "test");
    TEST_ASSERT_NOT_NULL(exc_inst, "create ValueError instance");
    PyObject *forty_two = PyLong_FromLong(42);
    TEST_ASSERT_NOT_NULL(forty_two, "create 42");
    PyObject *label_3 = PyLong_FromLong(3);
    TEST_ASSERT_NOT_NULL(label_3, "create label 3");

    /* Handler at index 3; LABEL 3 defines it for bytecode label resolution */
    PyObject *instr = PyList_New(9);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_TRY, 3));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, exc_inst));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RAISE, 1));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_LABEL, label_3));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_EXCEPTION, 0));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_POP, 1));
    PyList_SET_ITEM(instr, 6, make_instr(KOS_OP_CONST, forty_two));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_POP_TRY, 0));
    PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyLong_Check(ev.payload), "payload is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 42, "result is 42");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: try/finally - raise causes finally to run, then re-raise === */


static int test_eval_try_finally_rerase(void) {
    /*
      Bytecode: PUSH_FINALLY 3, CONST (ValueError), RAISE 1, (finally at 3:)
      CONST 99, POP_FINALLY, RETURN. After POP_FINALLY we re-raise; no handler
      left. Expect KOS_EV_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *exc_inst = PyObject_CallFunction(
        (PyObject *) PyExc_ValueError, "s", "test");
    TEST_ASSERT_NOT_NULL(exc_inst, "create ValueError instance");
    PyObject *ninety_nine = PyLong_FromLong(99);
    TEST_ASSERT_NOT_NULL(ninety_nine, "create 99");
    PyObject *label_3 = PyLong_FromLong(3);
    TEST_ASSERT_NOT_NULL(label_3, "create label 3");

    /* Finally handler at index 3; LABEL 3 defines it for bytecode label resolution */
    PyObject *instr = PyList_New(8);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_FINALLY, 3));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, exc_inst));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RAISE, 1));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_LABEL, label_3));
    PyList_SET_ITEM(instr, 4, make_instr(KOS_OP_CONST, ninety_nine));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_POP_FINALLY, 0));
    PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_RETURN, 1));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_NOOP, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyExceptionInstance_Check(ev.payload), "payload is exception");

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
    int res = koskari_eval(ctx, &ev);

    if (res != 0) {
      if (PyErr_Occurred())
        PyErr_Print();
      else
        fprintf(stderr, "  koskari_eval returned %d (no exception set)\n", res);
      fflush(stderr);
    }
    TEST_ASSERT(res == 0, "koskari_eval succeeded");
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


/* === Test: POP_PROMPT without prompt => rejected at Code construction === */

static int test_eval_pop_prompt_without_prompt(void) {
    /*
      Malformed bytecode (POP without matching PUSH) is rejected during
      Code construction by extent validation, not at runtime.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_POP_PROMPT, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 0));

    PyErr_Clear();
    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NULL(code_obj, "Code creation fails for malformed bytecode");
    TEST_ASSERT(PyErr_Occurred(), "error is set");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
                "exception is RuntimeError");

    PyErr_Clear();
    Py_DECREF(instr);
    Py_DECREF(nspec);

    return 0;
}


/* === Test: INVOKE_K stack underrun === */

static int test_eval_invoke_k_stack_underrun(void) {
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_INVOKE_K, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyExceptionInstance_Check(ev.payload), "payload is exception");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: INVOKE_K wrong type => TypeError === */

static int test_eval_invoke_k_wrong_type(void) {
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *five = PyLong_FromLong(5);
    PyObject *one_twenty_three = PyLong_FromLong(123);

    PyObject *instr = PyList_New(4);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, five));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, one_twenty_three));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_INVOKE_K, 0));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(five);
    Py_DECREF(one_twenty_three);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyExceptionInstance_Check(ev.payload), "payload is exception");
    TEST_ASSERT(PyErr_GivenExceptionMatches((PyObject *) Py_TYPE(ev.payload),
                                            PyExc_TypeError),
                "exception is TypeError");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test runner === */

int run_eval_tests(void) {

    BEGIN_TEST_SUITE("Eval Tests");

    RUN_TEST(test_eval_object_add_return);
    RUN_TEST(test_eval_try_except_catch);
    RUN_TEST(test_eval_try_finally_rerase);
    RUN_TEST(test_eval_reset_shift_invoke);
    RUN_TEST(test_eval_pop_prompt_without_prompt);
    RUN_TEST(test_eval_invoke_k_stack_underrun);
    RUN_TEST(test_eval_invoke_k_wrong_type);

    return 0;
}


// The end.
