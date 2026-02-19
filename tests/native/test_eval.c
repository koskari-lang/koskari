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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

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


/* === Test: domain non-null on fresh context === */


static int test_domain_non_null(void) {
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

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
    TEST_ASSERT_NOT_NULL(KosContext_DOMAIN(ctx), "domain is non-null");

    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: two contexts without TLS get distinct domains === */


static int test_domain_fresh_when_no_tls(void) {
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns1 = PyDict_New();
    PyObject *ns2 = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns1, "create ns1");
    TEST_ASSERT_NOT_NULL(ns2, "create ns2");

    KosDynFrame *frame1 = kos_dynframe_new(NULL, (KosCode *) code_obj, ns1, NULL);
    KosDynFrame *frame2 = kos_dynframe_new(NULL, (KosCode *) code_obj, ns2, NULL);
    TEST_ASSERT_NOT_NULL(frame1, "create frame1");
    TEST_ASSERT_NOT_NULL(frame2, "create frame2");

    KosContext *ctx1 = kos_context_new(frame1);
    KosContext *ctx2 = kos_context_new(frame2);
    TEST_ASSERT_NOT_NULL(ctx1, "create ctx1");
    TEST_ASSERT_NOT_NULL(ctx2, "create ctx2");
    TEST_ASSERT_NOT_NULL(KosContext_DOMAIN(ctx1), "ctx1 domain non-null");
    TEST_ASSERT_NOT_NULL(KosContext_DOMAIN(ctx2), "ctx2 domain non-null");
    TEST_ASSERT_NOT_SAME(KosContext_DOMAIN(ctx1), KosContext_DOMAIN(ctx2),
                         "contexts without TLS get distinct domains");

    kos_context_free(ctx1);
    kos_context_free(ctx2);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: kos_ctx_current before and after eval === */


static int test_eval_ctx_current_outside_eval(void) {
    TEST_ASSERT(kos_ctx_current() == NULL, "no current context before eval");

    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(42);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(kos_ctx_current() == NULL, "no current context after eval");

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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

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
      Bytecode: PUSH_PROMPT 0, CONST 1, PUSH_SHIFT 0, CONST 5, INVOKE_K 0,
      POP_SHIFT 0, OBJECT_ADD 2, POP_PROMPT 0, RETURN 1.
      (+ 1 (shift k (k 5))) => 6. Shift body: k on stack, CONST 5, INVOKE_K.
      Continuation: OBJECT_ADD 2 (1+5=6), POP_PROMPT, RETURN.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *zero = PyLong_FromLong(0);
    PyObject *one = PyLong_FromLong(1);
    PyObject *five = PyLong_FromLong(5);

    PyObject *instr = PyList_New(9);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_PUSH_PROMPT, zero));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 2, make_instr(KOS_OP_PUSH_SHIFT, zero));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_CONST, five));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_INVOKE_K, 0));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_POP_SHIFT, 0));
    PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_OBJECT_ADD, 2));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_POP_PROMPT, 0));
    PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(zero);
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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

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


/* === Test: PUSH_CC, CAPTURE_CC, POP_CC (undelimited continuation) === */


static int test_eval_push_cc_capture_cc_pop_cc_returns_continuation(void) {
    /*
      Bytecode: PUSH_CC (no arg; resume PC from block table), CAPTURE_CC,
      POP_CC, LABEL "resume", RETURN 1. Result is the continuation on the
      stack (no invoke).
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(5);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_CC, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_CAPTURE_CC, 0));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_POP_CC, 0));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_LABEL, PyUnicode_FromString("resume")));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_RETURN, 1));

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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyObject_TypeCheck(ev.payload, &KosPyContinuationType),
                "result is Continuation");

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


/* === Test: CALL continuation stack underrun === */

static int test_eval_call_continuation_underrun(void) {
    /* CALL 1 needs callable + 1 arg; we have only one value on stack */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_CALL, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(one);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyExceptionInstance_Check(ev.payload), "payload is exception");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: CALL wrong type (non-callable) => TypeError === */

static int test_eval_call_wrong_type(void) {
    /* CALL 1 with int as callable => TypeError */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *five = PyLong_FromLong(5);
    PyObject *one_twenty_three = PyLong_FromLong(123);

    PyObject *instr = PyList_New(4);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, five));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, one_twenty_three));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_CALL, 1));
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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

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


/* === Test: OBJECT_ADD stack underrun === */


static int test_eval_object_add_underrun(void) {
    /*
      OBJECT_ADD 2 needs 2 values; we have only 1. Expect KOS_EV_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_OBJECT_ADD, 2));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(one);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: RETURN stack underrun === */


static int test_eval_return_underrun(void) {
    /*
      RETURN 2 needs 2 values; we have only 1. Expect KOS_EV_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 2));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_NOOP, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(one);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: DUP stack underrun === */


static int test_eval_dup_underrun(void) {
    /*
      DUP 1 needs 1 value; stack is empty. Expect KOS_EV_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_DUP, 1));
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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: CALL 0 with empty stack === */


static int test_eval_call_empty_stack(void) {
    /*
      CALL 0 needs callable on stack; stack is empty. Expect KOS_EV_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_CALL, 0));
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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: POP_TRY without PUSH_TRY => rejected at Code construction === */


static int test_eval_pop_try_without_push_try(void) {
    /*
      Malformed bytecode (POP_TRY without matching PUSH) rejected at
      Code construction by extent validation.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_POP_TRY, 0));
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


/* === Test: POP_FINALLY without PUSH_FINALLY => rejected at Code construction === */


static int test_eval_pop_finally_without_push(void) {
    /*
      Malformed bytecode (POP_FINALLY without matching PUSH) rejected
      at Code construction by extent validation.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_POP_FINALLY, 0));
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


/* === Test: POP_CC without PUSH_CC => rejected at Code construction === */


static int test_eval_pop_cc_without_push_cc(void) {
    /*
      Malformed bytecode (POP_CC without matching PUSH) rejected at
      Code construction by extent validation.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_POP_CC, 0));
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


static int test_eval_invoke_k_underrun(void) {
    /*
      INVOKE_K needs value and continuation on stack. Empty stack or
      only one value causes underrun. Expect KOS_EV_RAISE.
    */
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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: INVOKE_K wrong type (int as continuation) => TypeError === */


static int test_eval_invoke_k_wrong_type(void) {
    /*
      INVOKE_K expects DelimitedContinuation at TOS-1; int => TypeError.
      Stack: value, cont. We use two ints so cont is int.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *five = PyLong_FromLong(5);
    PyObject *ninety_nine = PyLong_FromLong(99);

    PyObject *instr = PyList_New(4);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, ninety_nine));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, five));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_INVOKE_K, 0));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(five);
    Py_DECREF(ninety_nine);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyErr_GivenExceptionMatches((PyObject *) Py_TYPE(ev.payload),
                                            PyExc_TypeError),
                "exception is TypeError");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: CALL 0 with callable => resume with None === */


static int test_eval_call_zero_args(void) {
    /*
      call/cc (lambda (k) (k)) => None. CALL 0 invokes continuation
      with Py_None as resume_val.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(6);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_CC, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_CAPTURE_CC, 0));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_CALL, 0));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_POP_CC, 0));
    PyList_SET_ITEM(instr, 4, make_instr(KOS_OP_LABEL, PyUnicode_FromString("resume")));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_RETURN, 1));

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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT(ev.payload != NULL && Py_IsNone(ev.payload), "result is None");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: FAST on dict ns => RuntimeError === */


static int test_eval_fast_on_dict_ns(void) {
    /*
      FAST requires KosNamespace with fast slots; PyDict => RuntimeError.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_FAST, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

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
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: BUILD_FUNCTION (no defaults) => Function on stack === */


static int test_eval_build_function_no_defaults(void) {
    /*
      Bytecode: CONST (FunctionSpec), BUILD_FUNCTION 0, RETURN 1.
      FunctionSpec = (name=NULL, formals_spec=empty, code=inner).
      Inner code is just RETURN 0. Frame ns must be KosNamespace so
      KosFunction_New can use it as parent_ns. Expect KOS_EV_RETURN
      with a KosFunction.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *inner_instr = PyList_New(1);
    TEST_ASSERT_NOT_NULL(inner_instr, "inner instr list");
    PyList_SET_ITEM(inner_instr, 0, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *inner_code = KosCode_New(nspec, inner_instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(inner_code, "inner Code");
    Py_DECREF(inner_instr);

    PyObject *formals_spec = KosFormalsSpec_New(
        NULL, NULL, NULL, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(formals_spec, "formals_spec");

    PyObject *fspec = KosFunctionSpec_New(NULL, formals_spec, inner_code);
    TEST_ASSERT_NOT_NULL(fspec, "FunctionSpec");
    Py_DECREF(formals_spec);
    Py_DECREF(inner_code);

    PyObject *instr = PyList_New(3);
    TEST_ASSERT_NOT_NULL(instr, "instr list");
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, fspec));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_BUILD_FUNCTION, 0));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");
    Py_DECREF(instr);

    PyObject *ns = KosNamespace_FromSpec(nspec, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");
    Py_DECREF(nspec);

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyObject_TypeCheck(ev.payload, &KosFunctionType),
                "result is Function");

    Py_XDECREF(ev.payload);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test runner === */

int run_eval_tests(void) {

    BEGIN_TEST_SUITE("Eval Tests");

    RUN_TEST(test_eval_object_add_return);
    RUN_TEST(test_domain_non_null);
    RUN_TEST(test_domain_fresh_when_no_tls);
    RUN_TEST(test_eval_ctx_current_outside_eval);
    RUN_TEST(test_eval_try_except_catch);
    RUN_TEST(test_eval_try_finally_rerase);
    RUN_TEST(test_eval_reset_shift_invoke);
    RUN_TEST(test_eval_push_cc_capture_cc_pop_cc_returns_continuation);
    RUN_TEST(test_eval_pop_prompt_without_prompt);
    RUN_TEST(test_eval_call_continuation_underrun);
    RUN_TEST(test_eval_call_wrong_type);
    RUN_TEST(test_eval_build_function_no_defaults);

    RUN_TEST(test_eval_object_add_underrun);
    RUN_TEST(test_eval_return_underrun);
    RUN_TEST(test_eval_dup_underrun);
    RUN_TEST(test_eval_call_empty_stack);
    RUN_TEST(test_eval_pop_try_without_push_try);
    RUN_TEST(test_eval_pop_finally_without_push);
    RUN_TEST(test_eval_pop_cc_without_push_cc);
    RUN_TEST(test_eval_invoke_k_underrun);
    RUN_TEST(test_eval_invoke_k_wrong_type);
    RUN_TEST(test_eval_call_zero_args);
    RUN_TEST(test_eval_fast_on_dict_ns);

    return 0;
}


// The end.
