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


/* === Test: KosType is not KosCallable (uses default PyObject_Call path) === */


static int test_kostype_is_not_koscallable(void) {
    /*
      KosType is not a KosCallable. (Foo) uses the default PyObject_Call path
      which invokes kostype_tp_call.
    */
    PyTypeObject *kostype = &KosTypeType;
    PyTypeObject *koscallable = KOS_CALLABLE_PYTYPE(KosCallableType);
    TEST_ASSERT(! PyType_IsSubtype(kostype, koscallable),
                "KosType must not be KosCallable; (Foo) uses default PyCall path");
    return 0;
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyLong_Check(ev.payload), "payload is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 6, "result is 6");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(kos_ctx_current() == NULL, "no current context after eval");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: try/except - raise caught, handler result returned === */


static int test_eval_try_except_catch(void) {
    /*
      Bytecode: PUSH_TRY 3, CONST (ValueError), RAISE 1, EXCEPTION, POP 1,
      CONST 42, POP_TRY 1, RETURN. Handler at index 3. Expect KOS_EV_RETURN 42.
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyLong_Check(ev.payload), "payload is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 42, "result is 42");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: try/finally - raise causes finally to run, then re-raise === */


static int test_eval_try_finally_rerase(void) {
    /*
      Bytecode: PUSH_FINALLY 3, CONST (ValueError), RAISE 1, (finally at 3:)
      CONST 99, POP_FINALLY, POP 1, EXCEPTION 0, RAISE 1. Re-raise after finally.
      Expect KOS_EV_RAISE.
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
    PyObject *instr = PyList_New(10);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_FINALLY, 3));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, exc_inst));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RAISE, 1));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_LABEL, label_3));
    PyList_SET_ITEM(instr, 4, make_instr(KOS_OP_CONST, ninety_nine));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_POP_FINALLY, 0));
    PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_POP, 1));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_EXCEPTION, 0));
    PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_RAISE, 1));
    PyList_SET_ITEM(instr, 9, make_instr_int(KOS_OP_NOOP, 0));

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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyExceptionInstance_Check(ev.payload), "payload is exception");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    PyObject *nspec = NULL;
    PyObject *zero = NULL;
    PyObject *one = NULL;
    PyObject *five = NULL;
    PyObject *instr = NULL;
    PyObject *code_obj = NULL;
    PyObject *ns = NULL;
    KosDynFrame *frame = NULL;
    KosContext *ctx = NULL;
    KosEvent ev = {0};
    int res;
    int result = 0;

    nspec = create_test_nspec();
    if (! nspec)
        return -1;

    zero = PyLong_FromLong(0);
    one = PyLong_FromLong(1);
    five = PyLong_FromLong(5);
    instr = PyList_New(9);
    if (! zero || ! one || ! five || ! instr) {
        Py_XDECREF(zero);
        Py_XDECREF(one);
        Py_XDECREF(five);
        Py_XDECREF(instr);
        Py_DECREF(nspec);
        return -1;
    }
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_PUSH_PROMPT, zero));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 2, make_instr(KOS_OP_PUSH_SHIFT, zero));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_CONST, five));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_INVOKE_K, 0));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_POP_SHIFT, 0));
    PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_OBJECT_ADD, 2));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_POP_PROMPT, 0));
    PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_RETURN, 1));

    code_obj = KosCode_New(nspec, instr, NULL, NULL);
    Py_DECREF(zero);
    Py_DECREF(one);
    Py_DECREF(five);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    if (! code_obj)
        return -1;

    ns = PyDict_New();
    if (! ns)
        goto cleanup;

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    if (! frame) {
        Py_DECREF(ns);
        goto cleanup;
    }

    ctx = kos_context_new(frame);
    if (! ctx) {
        kos_dynframe_free(frame);
        goto cleanup;
    }

    res = koskari_eval(ctx, &ev, NULL, NULL, NULL);
    if (res != 0) {
        if (PyErr_Occurred())
            PyErr_Print();
        else
            fprintf(stderr, "  koskari_eval returned %d (no exception set)\n", res);
        fflush(stderr);
        result = -1;
        goto cleanup;
    }
    if (ev.kind != KOS_EV_RETURN) {
        fprintf(stderr, "  ev.kind=%d (expected KOS_EV_RETURN), payload=%p\n",
                (int) ev.kind, (void *) ev.payload);
        fflush(stderr);
        result = -1;
        goto cleanup;
    }
    if (! ev.payload) {
        fprintf(stderr, "  FAIL: payload not NULL\n");
        result = -1;
        goto cleanup;
    }
    if (! PyLong_Check(ev.payload)) {
        fprintf(stderr, "  FAIL: payload is int\n");
        result = -1;
        goto cleanup;
    }
    if (PyLong_AsLong(ev.payload) != 6) {
        fprintf(stderr, "  FAIL: result is 6\n");
        result = -1;
        goto cleanup;
    }

 cleanup:
    Py_XDECREF(ev.payload);
    ev.payload = NULL;
    Py_XDECREF(ev.exc_type);
    ev.exc_type = NULL;
    if (ctx)
        kos_context_free(ctx);
    Py_XDECREF(code_obj);
    return result;
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
    PyObject *resume_str = PyUnicode_FromString("resume");
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_CC, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_CAPTURE_CC, 0));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_POP_CC, 0));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_LABEL, resume_str));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_RETURN, 1));
    Py_XDECREF(resume_str);

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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyObject_TypeCheck(ev.payload, KOS_CALLABLE_PYTYPE(KosPyContinuationType)),
                "result is Continuation");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    PyObject *nspec = NULL;
    PyObject *one = NULL;
    PyObject *instr = NULL;
    PyObject *code_obj = NULL;
    PyObject *ns = NULL;
    KosDynFrame *frame = NULL;
    KosContext *ctx = NULL;
    KosEvent ev = {0};
    int res;
    int result = 0;

    nspec = create_test_nspec();
    if (! nspec)
        return -1;

    one = PyLong_FromLong(1);
    instr = PyList_New(2);
    if (! one || ! instr) {
        Py_XDECREF(one);
        Py_XDECREF(instr);
        Py_DECREF(nspec);
        return -1;
    }
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_CALL, 1));

    code_obj = KosCode_New(nspec, instr, NULL, NULL);
    Py_DECREF(one);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    if (! code_obj)
        return -1;

    ns = PyDict_New();
    if (! ns)
        goto cleanup;

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    if (! frame) {
        Py_DECREF(ns);
        goto cleanup;
    }

    ctx = kos_context_new(frame);
    if (! ctx) {
        kos_dynframe_free(frame);
        goto cleanup;
    }

    res = koskari_eval(ctx, &ev, NULL, NULL, NULL);
    if (res != 0) {
        fprintf(stderr, "  FAIL: koskari_eval succeeded\n");
        result = -1;
        goto cleanup;
    }
    if (ev.kind != KOS_EV_RAISE) {
        fprintf(stderr, "  FAIL: event is RAISE\n");
        result = -1;
        goto cleanup;
    }
    if (! ev.payload) {
        fprintf(stderr, "  FAIL: payload not NULL\n");
        result = -1;
        goto cleanup;
    }
    if (! PyExceptionInstance_Check(ev.payload)) {
        fprintf(stderr, "  FAIL: payload is exception\n");
        result = -1;
        goto cleanup;
    }

 cleanup:
    Py_XDECREF(ev.payload);
    ev.payload = NULL;
    Py_XDECREF(ev.exc_type);
    ev.exc_type = NULL;
    if (ctx)
        kos_context_free(ctx);
    Py_XDECREF(code_obj);
    return result;
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyExceptionInstance_Check(ev.payload), "payload is exception");
    TEST_ASSERT(PyErr_GivenExceptionMatches((PyObject *) Py_TYPE(ev.payload),
                                            PyExc_TypeError),
                "exception is TypeError");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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


/* === Test: PUSH_COLLECT / POP_COLLECT / BUILD_CLASS - empty class C === */


static int test_eval_push_collect_pop_collect_build_class_empty(void) {
    /*
      Bytecode: CONST "C", CONST (), BUILD_ARGS 2, PUSH_COLLECT nspec,
      POP_COLLECT, BUILD_CLASS, RETURN 1.
      Expect KOS_EV_RETURN with a KosType whose __name__ is "C".
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *name_c = PyUnicode_FromString("C");
    PyObject *empty_bases = PyTuple_New(0);
    TEST_ASSERT_NOT_NULL(name_c, "create name C");
    TEST_ASSERT_NOT_NULL(empty_bases, "create empty bases");

    PyObject *instr = PyList_New(7);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, name_c));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, empty_bases));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_BUILD_ARGS, 2));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_PUSH_COLLECT, nspec));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_POP_COLLECT, 0));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_BUILD_CLASS, 0));
    PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_RETURN, 1));

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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyType_Check(ev.payload), "result is a type");
    TEST_ASSERT(KosType_Check(ev.payload), "result is KosType (metaclass used)");

    PyObject *cname = PyObject_GetAttrString(ev.payload, "__name__");
    TEST_ASSERT_NOT_NULL(cname, "__name__ exists");
    TEST_ASSERT(PyUnicode_Check(cname), "__name__ is str");
    TEST_ASSERT(PyUnicode_CompareWithASCIIString(cname, "C") == 0,
                "__name__ is C");
    Py_DECREF(cname);
    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: PUSH_COLLECT with empty stack => RuntimeError === */


static int test_eval_push_collect_empty_stack_raises(void) {
    /*
      PUSH_COLLECT with invalid constant (int 0, not NamespaceSpec).
      Expect KOS_EV_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_COLLECT, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_POP_COLLECT, 0));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);

    PyObject *ns = KosNamespace_FromSpec(nspec, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create ns");
    Py_DECREF(nspec);

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = kos_context_new(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyErr_GivenExceptionMatches(
                    (PyObject *) Py_TYPE(ev.payload), PyExc_RuntimeError),
                "exception is RuntimeError");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: POP_COLLECT without PUSH_COLLECT => rejected at Code construction === */


static int test_eval_pop_collect_without_push_collect(void) {
    /*
      Malformed bytecode (POP_COLLECT without matching PUSH) rejected at
      Code construction by extent validation.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_POP_COLLECT, 0));
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyErr_GivenExceptionMatches((PyObject *) Py_TYPE(ev.payload),
                                            PyExc_TypeError),
                "exception is TypeError");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    PyObject *nspec = NULL;
    PyObject *instr = NULL;
    PyObject *code_obj = NULL;
    PyObject *ns = NULL;
    KosDynFrame *frame = NULL;
    KosContext *ctx = NULL;
    KosEvent ev = {0};
    int res;
    int result = 0;

    nspec = create_test_nspec();
    if (! nspec)
        return -1;

    instr = PyList_New(6);
    if (! instr) {
        Py_DECREF(nspec);
        return -1;
    }
    {
        PyObject *resume_str = PyUnicode_FromString("resume");
        PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_CC, 0));
        PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_CAPTURE_CC, 0));
        PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_CALL, 0));
        PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_POP_CC, 0));
        PyList_SET_ITEM(instr, 4, make_instr(KOS_OP_LABEL, resume_str));
        PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_RETURN, 1));
        Py_XDECREF(resume_str);
    }

    code_obj = KosCode_New(nspec, instr, NULL, NULL);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    if (! code_obj)
        return -1;

    ns = PyDict_New();
    if (! ns)
        goto cleanup;

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
    if (! frame) {
        Py_DECREF(ns);
        goto cleanup;
    }

    ctx = kos_context_new(frame);
    if (! ctx) {
        kos_dynframe_free(frame);
        goto cleanup;
    }

    res = koskari_eval(ctx, &ev, NULL, NULL, NULL);
    if (res != 0) {
        fprintf(stderr, "  FAIL: koskari_eval succeeded\n");
        result = -1;
        goto cleanup;
    }
    if (ev.kind != KOS_EV_RETURN) {
        fprintf(stderr, "  FAIL: event is RETURN\n");
        result = -1;
        goto cleanup;
    }
    if (ev.payload == NULL || ! Py_IsNone(ev.payload)) {
        fprintf(stderr, "  FAIL: result is None\n");
        result = -1;
        goto cleanup;
    }

 cleanup:
    Py_XDECREF(ev.payload);
    ev.payload = NULL;
    Py_XDECREF(ev.exc_type);
    ev.exc_type = NULL;
    if (ctx)
        kos_context_free(ctx);
    Py_XDECREF(code_obj);
    return result;
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
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
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyObject_TypeCheck(ev.payload, KOS_CALLABLE_PYTYPE(KosFunctionType)),
                "result is Function");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Phase-refactor resume-matrix tests === */


/*
  Helper: build a minimal nspec+code+frame+context.
  Returns 0 on success; -1 on failure (PyErr set).
  Caller owns code_obj_out and ctx_out references; ns is owned by the frame.

  For yielding code (KosCode_YIELDS), the frame is created under a KosDynGen
  (Generator node) with owner NULL (standalone). Yield uses the same GEN
  freeze/install path as Python generators; RESUME_GEN is set on freeze so
  the next koskari_eval can revive via revive_yield.
*/
static int make_simple_ctx(
    PyObject *instr,
    PyObject **code_obj_out,
    KosContext **ctx_out)
{
    PyObject *nspec = create_test_nspec();
    if (! nspec) return -1;

    *code_obj_out = KosCode_New(nspec, instr, NULL, NULL);
    Py_DECREF(nspec);
    if (! *code_obj_out) return -1;

    PyObject *ns = PyDict_New();
    if (! ns) { Py_DECREF(*code_obj_out); return -1; }

    KosDynFrame *frame;
    if (KosCode_YIELDS((KosCode *) *code_obj_out)) {
        KosDynGen *gen = kos_dyngen_new(NULL);
        if (! gen) { Py_DECREF(*code_obj_out); return -1; }
        frame = kos_dynframe_new(
            (KosDynNode *) gen, (KosCode *) *code_obj_out, ns, NULL);
        if (! frame) {
            kos_dyngen_free(gen);
            Py_DECREF(*code_obj_out);
            return -1;
        }
        KosDynGen_OWNER(gen) = NULL;
    }
    else {
        frame = kos_dynframe_new(NULL, (KosCode *) *code_obj_out, ns, NULL);
    }
    if (! frame) { Py_DECREF(*code_obj_out); return -1; }

    *ctx_out = kos_context_new(frame);
    if (! *ctx_out) { Py_DECREF(*code_obj_out); return -1; }
    return 0;
}


/* === Test: YIELD empty resume (NULL/NULL) => RETURN with next CONST === */
/*
  These yield tests use the GEN-based path: make_simple_ctx builds a context
  with a KosDynGen (owner NULL). On YIELD, walk_handle_yield freezes the GEN
  and sets SUSPENSION_EVENT; next eval revive_yield installs the gen and
  pushes send value (or sets throw), then execution continues.
*/


static int test_eval_yield_empty_resume(void) {
    /*
      Bytecode: CONST 42, YIELD 1, CONST 99, RETURN 1.
      First eval (NULL, NULL): KOS_EV_YIELD(42); GEN frozen, RESUME_GEN set.
      Second eval (NULL, NULL): revive_yield(Py_None) installs gen, pushes
      Py_None; execution resumes at CONST 99, RETURN 1 => KOS_EV_RETURN(99).
    */
    PyObject *forty_two = PyLong_FromLong(42);
    PyObject *ninety_nine = PyLong_FromLong(99);

    PyObject *instr = PyList_New(4);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, forty_two));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 2, make_instr(KOS_OP_CONST, ninety_nine));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(forty_two);
    Py_DECREF(ninety_nine);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);
    TEST_ASSERT(res == 0, "first koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_YIELD, "first event is YIELD");
    TEST_ASSERT_NOT_NULL(ev.payload, "first payload not NULL");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 42, "yielded value is 42");
    Py_XDECREF(ev.payload);

    ev.kind = KOS_EV_RETURN;
    ev.payload = NULL;
    ev.exc_type = NULL;
    res = koskari_eval(ctx, &ev, NULL, NULL, NULL);
    TEST_ASSERT(res == 0, "second koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "second event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "second payload not NULL");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 99, "resumed result is 99");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: YIELD send resume => RETURN with sent value === */


static int test_eval_yield_send_resume(void) {
    /*
      Bytecode: CONST 42, YIELD 1, RETURN 1.
      First eval (NULL, NULL): KOS_EV_YIELD(42).
      Second eval (send_val=7, NULL): revive_yield pushes 7 onto stack;
      RETURN 1 pops TOS => KOS_EV_RETURN(7).
    */
    PyObject *forty_two = PyLong_FromLong(42);

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, forty_two));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(forty_two);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);
    TEST_ASSERT(res == 0, "first koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_YIELD, "first event is YIELD");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 42, "yielded value is 42");
    Py_XDECREF(ev.payload);

    PyObject *send_val = PyLong_FromLong(7);
    ev.kind = KOS_EV_RETURN;
    ev.payload = NULL;
    ev.exc_type = NULL;
    res = koskari_eval(ctx, &ev, send_val, NULL, NULL);
    Py_DECREF(send_val);
    TEST_ASSERT(res == 0, "second koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "second event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "second payload not NULL");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 7, "returned sent value 7");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: YIELD throw resume => RAISE event (no handler) === */


static int test_eval_yield_throw_resume(void) {
    /*
      Bytecode: CONST 42, YIELD 1, RETURN 1.
      First eval (NULL, NULL): KOS_EV_YIELD(42).
      Second eval (NULL, throw_exc=ValueError): revive_yield sets PyErr =>
      KOS_MODE_EXCEPTION; no TRY handler present => KOS_EV_RAISE.
    */
    PyObject *forty_two = PyLong_FromLong(42);

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, forty_two));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(forty_two);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);
    TEST_ASSERT(res == 0, "first koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_YIELD, "first event is YIELD");
    Py_XDECREF(ev.payload);

    PyObject *throw_exc = PyObject_CallFunction(
        (PyObject *) PyExc_ValueError, "s", "thrown into yield");
    TEST_ASSERT_NOT_NULL(throw_exc, "create throw_exc");

    ev.kind = KOS_EV_RETURN;
    ev.payload = NULL;
    ev.exc_type = NULL;
    res = koskari_eval(ctx, &ev, NULL, (PyObject *) Py_TYPE(throw_exc), throw_exc);
    Py_DECREF(throw_exc);
    TEST_ASSERT(res == 0, "second koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RAISE, "second event is RAISE");
    TEST_ASSERT_NOT_NULL(ev.payload, "raise payload not NULL");
    TEST_ASSERT(PyExceptionInstance_Check(ev.payload), "payload is exception");
    TEST_ASSERT(PyErr_GivenExceptionMatches((PyObject *) Py_TYPE(ev.payload),
                                            PyExc_ValueError),
                "exception is ValueError");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: YIELD throw resume caught by TRY handler => RETURN === */


static int test_eval_yield_throw_caught(void) {
    /*
      Bytecode: PUSH_TRY(6), CONST 42, DUP 1, YIELD 1, POP_TRY, RETURN 1,
                LABEL(6), EXCEPTION, POP 1, CONST 55, POP_TRY, RETURN 1.
      First eval (NULL, NULL): KOS_EV_YIELD(42); TRY region stays live.
      Second eval (NULL, throw_exc): eval_phase_entry calls
      try_local_exception_handler, which finds the TRY region and redirects
      PC to the handler => EXCEPTION/POP/CONST 55/POP_TRY/RETURN =>
      KOS_EV_RETURN(55).
    */
    PyObject *forty_two = PyLong_FromLong(42);
    PyObject *fifty_five = PyLong_FromLong(55);
    PyObject *label_6 = PyLong_FromLong(6);

    /*
      One PUSH_TRY and one POP_TRY (on the normal path).  The handler
      does not POP_TRY — try_local_exception_handler already disabled the
      region; the context is freed by kos_context_free after RETURN.
    */
    PyObject *instr = PyList_New(11);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_TRY, 6));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, forty_two));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_DUP, 1));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_POP_TRY, 0));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_RETURN, 1));
    PyList_SET_ITEM(instr, 6, make_instr(KOS_OP_LABEL, label_6));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_EXCEPTION, 0));
    PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_POP, 1));
    PyList_SET_ITEM(instr, 9, make_instr(KOS_OP_CONST, fifty_five));
    PyList_SET_ITEM(instr, 10, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(forty_two);
    Py_DECREF(fifty_five);
    Py_DECREF(label_6);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);
    TEST_ASSERT(res == 0, "first koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_YIELD, "first event is YIELD");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 42, "yielded value is 42");
    Py_XDECREF(ev.payload);

    PyObject *throw_exc = PyObject_CallFunction(
        (PyObject *) PyExc_ValueError, "s", "thrown into yield");
    TEST_ASSERT_NOT_NULL(throw_exc, "create throw_exc");

    ev.kind = KOS_EV_RETURN;
    ev.payload = NULL;
    ev.exc_type = NULL;
    res = koskari_eval(ctx, &ev, NULL, (PyObject *) Py_TYPE(throw_exc), throw_exc);
    Py_DECREF(throw_exc);
    TEST_ASSERT(res == 0, "second koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "second event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyLong_Check(ev.payload), "payload is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 55, "handler result is 55");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: YIELD_FROM sync iterator (reproducer for test_yield_from_single_value_sync_iterator) === */


// static int test_eval_yield_from_single_value_sync_iterator(void) {
//     /*
//       Reproducer for Python test_yield_from_single_value_sync_iterator.
//       Bytecode: CONST iter([100]), YIELD_FROM 0, RETURN 1.
//       Drives a KosPyAsyncGen via the same path as __anext__ (send Py_None).
//       First eval (Py_None): expect KOS_EV_YIELD(100).
//       Second eval (Py_None): expect KOS_EV_RETURN (exhausted, final None).
//     */
//     PyObject *lst = PyList_New(1);
//     TEST_ASSERT_NOT_NULL(lst, "create list");
//     PyList_SET_ITEM(lst, 0, PyLong_FromLong(100));

//     PyObject *delegate = PyObject_GetIter(lst);
//     Py_DECREF(lst);
//     TEST_ASSERT_NOT_NULL(delegate, "iter([100])");

//     PyObject *instr = PyList_New(3);
//     PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, delegate));
//     PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD_FROM, 0));
//     PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

//     Py_DECREF(delegate);

//     PyObject *nspec = create_test_nspec();
//     TEST_ASSERT_NOT_NULL(nspec, "create nspec");

//     PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL);
//     Py_DECREF(nspec);
//     Py_DECREF(instr);
//     TEST_ASSERT_NOT_NULL(code_obj, "create Code");

//     PyObject *ns = PyDict_New();
//     TEST_ASSERT_NOT_NULL(ns, "create ns");

//     KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns, NULL);
//     TEST_ASSERT_NOT_NULL(frame, "create frame");

//     PyObject *agen_obj = KosPyAsyncGen_NewFromFrame(frame);
//     TEST_ASSERT_NOT_NULL(agen_obj, "create KosPyAsyncGen");
//     KosPyAsyncGen *agen = (KosPyAsyncGen *) agen_obj;

//     if (! agen->ctx) {
//         agen->ctx = kos_context_new(agen->frame);
//         TEST_ASSERT_NOT_NULL(agen->ctx, "create context");
//     }

//     KosEvent ev = {0};
//     int res = koskari_eval(agen->ctx, &ev, Py_None, NULL, NULL);
//     TEST_ASSERT(res == 0, "first koskari_eval (anext) succeeded");
//     TEST_ASSERT(ev.kind == KOS_EV_YIELD, "first event is YIELD");
//     TEST_ASSERT_NOT_NULL(ev.payload, "first payload not NULL");
//     TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 100, "yielded value is 100");
//     Py_XDECREF(ev.payload);

//     ev.kind = KOS_EV_RETURN;
//     ev.payload = NULL;
//     res = koskari_eval(agen->ctx, &ev, Py_None, NULL, NULL);
//     TEST_ASSERT(res == 0, "second koskari_eval (anext) succeeded");
//     TEST_ASSERT(ev.kind == KOS_EV_RETURN, "second event is RETURN (exhausted)");
//     Py_XDECREF(ev.payload);

//     Py_DECREF(agen_obj);
//     Py_DECREF(code_obj);

//     return 0;
// }


/* === Test: exception from opcode (op_res != 0) caught by TRY handler === */
/*
  Uses same GEN-based context as other yield tests (make_simple_ctx with YIELDS).
*/


static int test_eval_opcode_error_caught_by_try(void) {
    /*
      Bytecode: PUSH_TRY(4), YIELD 1, POP_TRY, RETURN 0,
                LABEL(4), EXCEPTION, POP 1, CONST 77, POP_TRY, RETURN 1.
      YIELD 1 with empty stack returns op_res=-1 with PyErr set.
      eval_phase_post_opcode detects PyErr and calls try_local_exception_handler,
      which finds the TRY region and redirects to the handler =>
      EXCEPTION/POP/CONST 77/POP_TRY/RETURN => KOS_EV_RETURN(77).
      This exercises the exception path inside eval_phase_post_opcode directly.
    */
    PyObject *seventy_seven = PyLong_FromLong(77);
    PyObject *label_4 = PyLong_FromLong(4);

    /*
      One PUSH_TRY and one POP_TRY (on the normal path).  The handler
      does not POP_TRY — the region is disabled by try_local_exception_handler;
      the context is freed by kos_context_free after RETURN.
    */
    PyObject *instr = PyList_New(9);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_TRY, 4));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_POP_TRY, 0));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 0));
    PyList_SET_ITEM(instr, 4, make_instr(KOS_OP_LABEL, label_4));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_EXCEPTION, 0));
    PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_POP, 1));
    PyList_SET_ITEM(instr, 7, make_instr(KOS_OP_CONST, seventy_seven));
    PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(seventy_seven);
    Py_DECREF(label_4);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosEvent ev = {0};
    int res = koskari_eval(ctx, &ev, NULL, NULL, NULL);

    TEST_ASSERT(res == 0, "koskari_eval succeeded");
    TEST_ASSERT(ev.kind == KOS_EV_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.payload, "payload not NULL");
    TEST_ASSERT(PyLong_Check(ev.payload), "payload is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.payload), 77, "handler result is 77");

    Py_XDECREF(ev.payload);
    Py_XDECREF(ev.exc_type);
    kos_context_free(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test runner === */

int run_eval_tests(void) {

    BEGIN_TEST_SUITE("Eval Tests");

    RUN_TEST(test_kostype_is_not_koscallable);
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
    RUN_TEST(test_eval_push_collect_pop_collect_build_class_empty);
    RUN_TEST(test_eval_push_collect_empty_stack_raises);
    RUN_TEST(test_eval_pop_collect_without_push_collect);

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

    RUN_TEST(test_eval_yield_empty_resume);
    RUN_TEST(test_eval_yield_send_resume);
    RUN_TEST(test_eval_yield_throw_resume);
    RUN_TEST(test_eval_yield_throw_caught);
    //RUN_TEST(test_eval_yield_from_single_value_sync_iterator);
    RUN_TEST(test_eval_opcode_error_caught_by_try);

    return 0;
}


// The end.
