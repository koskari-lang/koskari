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
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === Helpers === */


static PyObject *create_test_nspec(void) {

    PyObject *name = PyUnicode_FromString("test");
    PyObject *locals = PyTuple_New(0);
    PyObject *closures = PyTuple_New(0);

    PyObject *nspec = KosNamespaceSpec_New(name, locals, closures, NULL);

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


static int install_frozen_gen(KosContext *ctx, KosDynGen *gen) {

    int r;

    KosContext_EvalEnter(ctx, NULL);
    if (! gen || ! kos_dyn_gen_frozen_needs_install(ctx, gen)) {
        KosContext_EvalExit(ctx);
        return 0;
    }
    r = kos_dyn_gen_install(ctx, gen);
    KosContext_EvalExit(ctx);
    return r;
}


/* === Test: type is not KosCallable (uses default PyObject_Call path) === */


static int test_class_is_not_koscallable(void) {
    /*
      Koskari-built classes use plain type as their metaclass, which is
      not a KosCallable. (Foo) uses the default PyObject_Call path.
    */
    PyObject *pytype = (PyObject *) &PyType_Type;
    TEST_ASSERT(! KosCallable_Check(pytype),
                "type must not be KosCallable; (Foo) uses default PyCall path");
    return 0;
}


/* === Test: OBJECT_ADD and RETURN === */


static int test_eval_object_add_return(void) {
    /*
      Run bytecode: CONST 1, CONST 5, OBJECT_ADD 2, RETURN.
      Expect KOS_RES_RETURN with value 6.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *five = PyLong_FromLong(5);

    PyObject *instr = PyList_New(4);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, five));
    PyList_SET_ITEM(instr, 2, make_instr_int(
        KOS_OP_OBJECT_OPERAND, KosOpCount_MAKE(KOS_OPERAND_ADD, 2)));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(one);
    Py_DECREF(five);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "val not NULL");
    TEST_ASSERT(PyLong_Check(ev.val), "val is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 6, "result is 6");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
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

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");
    /* every context has a domain, the thread default when none is
       ambient; parameter cells start empty (lazy) */
    TEST_ASSERT_NOT_NULL(KosContext_DOMAIN(ctx), "domain is non-null");
    TEST_ASSERT(KosContext_PARAM_REFS(ctx) == NULL,
                "param cells start unallocated");

    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: tp_clear then dealloc releases each inject field once === */


static int test_context_clear_then_dealloc_releases_once(void) {
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    /* fresh, non-cached objects; the context takes one reference to
       each and the test keeps two more so a double release is
       observable as a refcount rather than a freed object */
    PyObject *resume = PyList_New(0);
    PyObject *inject = PyList_New(0);
    TEST_ASSERT_NOT_NULL(resume, "create resume value");
    TEST_ASSERT_NOT_NULL(inject, "create inject value");
    Py_INCREF(resume);
    Py_INCREF(resume);
    Py_INCREF(inject);
    Py_INCREF(inject);
    KosContext_PY_RESUME_INJECT(ctx) = resume;
    KosContext_INJECT_VALUE(ctx) = inject;
    TEST_ASSERT_REFCNT(resume, 3, "resume held by test twice and ctx");
    TEST_ASSERT_REFCNT(inject, 3, "inject held by test twice and ctx");

    /* what the collector does to a context in a garbage cycle, then
       what happens when the last reference goes */
    int res = Py_TYPE(ctx)->tp_clear((PyObject *) ctx);
    TEST_ASSERT_EQ(res, 0, "tp_clear succeeds");
    Py_DECREF(ctx);

    TEST_ASSERT_REFCNT(resume, 2, "resume released exactly once");
    TEST_ASSERT_REFCNT(inject, 2, "inject released exactly once");

    Py_DECREF(resume);
    Py_DECREF(resume);
    Py_DECREF(inject);
    Py_DECREF(inject);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: tp_clear leaves no pointer into the freed chain === */


static int test_context_clear_drops_chain_caches(void) {
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");
    TEST_ASSERT_NOT_NULL(KosContext_FRAME(ctx), "frame cached");
    TEST_ASSERT_NOT_NULL(KosContext_NAMESPACE(ctx), "namespace cached");

    /* what the collector does to a context in a garbage cycle: the
       chain is freed, and the cached frame, block and namespace that
       pointed into it must go with it, since a finalizer in the same
       cycle can still reach the context */
    int res = Py_TYPE(ctx)->tp_clear((PyObject *) ctx);
    TEST_ASSERT_EQ(res, 0, "tp_clear succeeds");
    TEST_ASSERT_NULL(KosContext_DYN_TOP(ctx), "chain dropped");
    TEST_ASSERT_NULL(KosContext_FRAME(ctx), "frame cache dropped");
    TEST_ASSERT_NULL(KosContext_BLOCK(ctx), "block cache dropped");
    TEST_ASSERT_NULL(KosContext_NAMESPACE(ctx), "namespace cache dropped");

    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: tp_clear releases a staged delivery === */


static int test_context_clear_releases_staged_delivery(void) {

  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  /* a fresh, non-cached object; the context takes the staged
     reference, so the count returns to its value before staging only
     when tp_clear releases it */
  PyObject *val = PyList_New(0);
  TEST_ASSERT_NOT_NULL(val, "create staged value");
  Py_ssize_t before = Py_REFCNT(val);

  Py_INCREF(val);
  KosContext_StageDelivery(ctx, val);
  TEST_ASSERT_REFCNT(val, before + 1, "staged value held by ctx");

  /* what the collector does to a context in a garbage cycle collected
     mid-drive while a delivery is staged */
  int res = Py_TYPE(ctx)->tp_clear((PyObject *) ctx);
  TEST_ASSERT_EQ(res, 0, "tp_clear succeeds");
  TEST_ASSERT_NULL(KosContext_PENDING_VALUE(ctx), "staged slot emptied");
  TEST_ASSERT_REFCNT(val, before, "staged value released");

  Py_DECREF(ctx);
  Py_DECREF(val);

  return 0;
}


/* === Test: recycle releases a staged delivery === */


static int test_context_recycle_releases_staged_delivery(void) {

  KosContext *ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");

  PyObject *val = PyList_New(0);
  TEST_ASSERT_NOT_NULL(val, "create staged value");
  Py_ssize_t before = Py_REFCNT(val);

  Py_INCREF(val);
  KosContext_StageDelivery(ctx, val);
  TEST_ASSERT_REFCNT(val, before + 1, "staged value held by ctx");

  /* the next run on a recycled context must not receive the staged
     value of the finished one */
  int res = KosContext_Recycle(ctx);
  TEST_ASSERT_EQ(res, 0, "recycle succeeds");
  TEST_ASSERT_NULL(KosContext_PENDING_VALUE(ctx), "staged slot emptied");
  TEST_ASSERT_REFCNT(val, before, "staged value released");

  Py_DECREF(ctx);
  Py_DECREF(val);

  return 0;
}


/* === Test: contexts made and dropped past the freelist cap === */


/* more than the context freelist keeps, so the drop both caches and
   frees; under KOS_NOFREELIST every drop frees */
#define CONTEXT_CHURN_COUNT 300


static int test_context_churn_past_freelist_cap(void) {
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    KosContext *held[CONTEXT_CHURN_COUNT];
    for (int i = 0; i < CONTEXT_CHURN_COUNT; i++) {
        PyObject *ns = PyDict_New();
        TEST_ASSERT_NOT_NULL(ns, "create ns");
        KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
        TEST_ASSERT_NOT_NULL(frame, "create frame");
        held[i] = KosContext_New(frame, kos_tls_current());
        TEST_ASSERT_NOT_NULL(held[i], "create context");
    }

    /* half the drops run tp_clear first, as the collector does to a
       context in a garbage cycle */
    for (int i = 0; i < CONTEXT_CHURN_COUNT; i++) {
        if (i % 2)
            TEST_ASSERT_EQ(Py_TYPE(held[i])->tp_clear((PyObject *) held[i]),
                           0, "tp_clear succeeds");
        Py_DECREF(held[i]);
    }

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");
    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context after the churn");
    TEST_ASSERT_REFCNT(ctx, 1, "one reference to the new context");
    TEST_ASSERT(KosContext_DYN_TOP(ctx) == (KosDynNode *) frame,
                "chain is the new frame");
    TEST_ASSERT(KosContext_FRAME(ctx) == frame, "live frame is the new one");
    TEST_ASSERT_NOT_NULL(KosContext_DOMAIN(ctx), "domain is non-null");
    TEST_ASSERT(KosContext_PARAM_REFS(ctx) == NULL,
                "param cells start unallocated");
    TEST_ASSERT(KosContext_TOKEN(ctx) == NULL, "no evaluator token");
    TEST_ASSERT_EQ(KosContext_FLAGS(ctx), 0, "flags start cleared");
    TEST_ASSERT(KosContext_PY_AWAIT_ITER(ctx) == NULL, "no await iterator");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "val not NULL");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 1, "result is 1");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: contexts with no ambient domain share one === */


static int test_domain_thread_default_when_no_tls(void) {
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns1 = PyDict_New();
    PyObject *ns2 = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns1, "create ns1");
    TEST_ASSERT_NOT_NULL(ns2, "create ns2");

    KosDynFrame *frame1 = kos_dynframe_new(NULL, (KosCode *) code_obj, ns1);
    KosDynFrame *frame2 = kos_dynframe_new(NULL, (KosCode *) code_obj, ns2);
    TEST_ASSERT_NOT_NULL(frame1, "create frame1");
    TEST_ASSERT_NOT_NULL(frame2, "create frame2");

    KosContext *ctx1 = KosContext_New(frame1, kos_tls_current());
    KosContext *ctx2 = KosContext_New(frame2, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx1, "create ctx1");
    TEST_ASSERT_NOT_NULL(ctx2, "create ctx2");
    TEST_ASSERT_NOT_NULL(KosContext_DOMAIN(ctx1), "ctx1 domain non-null");
    TEST_ASSERT_NOT_NULL(KosContext_DOMAIN(ctx2), "ctx2 domain non-null");
    TEST_ASSERT_SAME(KosContext_DOMAIN(ctx1), KosContext_DOMAIN(ctx2),
                     "contexts share the thread default domain");

    /* each context also owns its own cell table */
    PyObject *cells1 = kos_context_param_refs(ctx1);
    PyObject *cells2 = kos_context_param_refs(ctx2);
    TEST_ASSERT_NOT_NULL(cells1, "ctx1 cell table");
    TEST_ASSERT_NOT_NULL(cells2, "ctx2 cell table");
    TEST_ASSERT_NOT_SAME(cells1, cells2,
                         "contexts without TLS have isolated cell tables");

    Py_DECREF(ctx1);
    Py_DECREF(ctx2);
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

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "eval returned RETURN");
    TEST_ASSERT(kos_ctx_current() == NULL, "no current context after eval");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: try/except - raise caught, handler result returned === */


static int test_eval_try_except_catch(void) {
    /*
      Bytecode: PUSH_TRY 3, CONST (ValueError), RAISE 1, EXCEPTION, POP 1,
      CONST 42, POP_TRY 1, RETURN. Handler at index 3. Expect KOS_RES_RETURN 42.
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

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    /* the instructions took their own references */
    Py_DECREF(exc_inst);
    Py_DECREF(forty_two);
    Py_DECREF(label_3);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "val not NULL");
    TEST_ASSERT(PyLong_Check(ev.val), "val is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 42, "result is 42");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: try/finally - raise causes finally to run, then re-raise === */


static int test_eval_try_finally_rerase(void) {
    /*
      Bytecode: PUSH_FINALLY 3, CONST (ValueError), RAISE 1, (finally at 3:)
      CONST 99, POP_FINALLY, POP 1, EXCEPTION 0, RAISE 1. Re-raise after finally.
      Expect KOS_RES_RAISE.
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

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    /* the instructions took their own references */
    Py_DECREF(exc_inst);
    Py_DECREF(label_3);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RAISE, "event is RAISE");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: reset (+ 1 (shift k (k 5))) => 6 === */


static int test_eval_reset_shift_invoke(void) {
    /*
      Bytecode: PUSH_PROMPT 0, CONST 1, PUSH_SHIFT 0, CONST 5, CALL 1,
      POP_SHIFT 0, OBJECT_ADD 2, POP_PROMPT 0, RETURN 1.
      (+ 1 (shift k (k 5))) => 6. Shift body: k on stack, CONST 5, CALL 1.
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
    KosOpResult ev = {0};
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
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_CALL, 1));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_POP_SHIFT, 0));
    PyList_SET_ITEM(instr, 6, make_instr_int(
        KOS_OP_OBJECT_OPERAND, KosOpCount_MAKE(KOS_OPERAND_ADD, 2)));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_POP_PROMPT, 0));
    PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_RETURN, 1));

    code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
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

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    if (! frame) {
        Py_DECREF(ns);
        goto cleanup;
    }

    ctx = KosContext_New(frame, kos_tls_current());
    if (! ctx) {
        kos_dynframe_free(frame);
        goto cleanup;
    }

    ev = koskari_eval(ctx, NULL, NULL, NULL);
    if (ev.kind == KOS_RES_FATAL) {
        if (PyErr_Occurred())
            PyErr_Print();
        else
            fprintf(stderr, "  koskari_eval returned FATAL (no exception set)\n");
        fflush(stderr);
        result = -1;
        goto cleanup;
    }
    if (ev.kind != KOS_RES_RETURN) {
        fprintf(stderr, "  ev.kind=%d (expected KOS_RES_RETURN), val=%p\n",
                (int) ev.kind, (void *) ev.val);
        fflush(stderr);
        result = -1;
        goto cleanup;
    }
    if (! ev.val) {
        fprintf(stderr, "  FAIL: val not NULL\n");
        result = -1;
        goto cleanup;
    }
    if (! PyLong_Check(ev.val)) {
        fprintf(stderr, "  FAIL: val is int\n");
        result = -1;
        goto cleanup;
    }
    if (PyLong_AsLong(ev.val) != 6) {
        fprintf(stderr, "  FAIL: result is 6\n");
        result = -1;
        goto cleanup;
    }

 cleanup:
    Py_XDECREF(ev.val);
    ev.val = NULL;
    PyErr_Clear();
    if (ctx)
        Py_DECREF(ctx);
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

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "val not NULL");
    TEST_ASSERT(PyObject_TypeCheck(ev.val, KOS_EVALUATED_PYTYPE(KosPyContinuationType)),
                "result is Continuation");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
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
    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
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
    KosOpResult ev = {0};
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

    code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    Py_DECREF(one);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    if (! code_obj)
        return -1;

    ns = PyDict_New();
    if (! ns)
        goto cleanup;

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    if (! frame) {
        Py_DECREF(ns);
        goto cleanup;
    }

    ctx = KosContext_New(frame, kos_tls_current());
    if (! ctx) {
        kos_dynframe_free(frame);
        goto cleanup;
    }

    ev = koskari_eval(ctx, NULL, NULL, NULL);
    if (ev.kind == KOS_RES_FATAL) {
        fprintf(stderr, "  FAIL: koskari_eval returned FATAL\n");
        result = -1;
        goto cleanup;
    }
    if (ev.kind != KOS_RES_RAISE) {
        fprintf(stderr, "  FAIL: event is RAISE (got %d)\n", (int) ev.kind);
        result = -1;
        goto cleanup;
    }
    if (! PyErr_Occurred()) {
        fprintf(stderr, "  FAIL: exception is set\n");
        result = -1;
        goto cleanup;
    }

 cleanup:
    Py_XDECREF(ev.val);
    ev.val = NULL;
    PyErr_Clear();
    if (ctx)
        Py_DECREF(ctx);
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

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(five);
    Py_DECREF(one_twenty_three);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RAISE, "event is RAISE");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_TypeError), "exception is TypeError");
    PyErr_Clear();

    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: OBJECT_ADD stack underrun === */


static int test_eval_object_add_underrun(void) {
    /*
      OBJECT_ADD 2 needs 2 values; we have only 1. Expect KOS_RES_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(
        KOS_OP_OBJECT_OPERAND, KosOpCount_MAKE(KOS_OPERAND_ADD, 2)));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(one);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RAISE, "event is RAISE");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: RETURN stack underrun === */


static int test_eval_return_underrun(void) {
    /*
      RETURN 2 needs 2 values; we have only 1. Expect KOS_RES_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *one = PyLong_FromLong(1);
    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, one));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 2));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_NOOP, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(one);
    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RAISE, "event is RAISE");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: DUP stack underrun === */


static int test_eval_dup_underrun(void) {
    /*
      DUP 1 needs 1 value; stack is empty. Expect KOS_RES_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_DUP, 1));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RAISE, "event is RAISE");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: CALL 0 with empty stack === */


static int test_eval_call_empty_stack(void) {
    /*
      CALL 0 needs callable on stack; stack is empty. Expect KOS_RES_RAISE.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_CALL, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RAISE, "event is RAISE");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    Py_DECREF(ctx);
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
    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
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
    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
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
    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
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
      Expect KOS_RES_RETURN with a plain type whose __name__ is "C".
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

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);

    PyObject *ns = KosNamespace_FromSpec(nspec, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");
    Py_DECREF(nspec);

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "val not NULL");
    TEST_ASSERT(PyType_Check(ev.val), "result is a type");
    TEST_ASSERT(Py_TYPE(ev.val) == &PyType_Type,
                "result metaclass is plain type");

    PyObject *cname = PyObject_GetAttrString(ev.val, "__name__");
    TEST_ASSERT_NOT_NULL(cname, "__name__ exists");
    TEST_ASSERT(PyUnicode_Check(cname), "__name__ is str");
    TEST_ASSERT(PyUnicode_CompareWithASCIIString(cname, "C") == 0,
                "__name__ is C");
    Py_DECREF(cname);
    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: PUSH_COLLECT with empty stack => RuntimeError === */


static int test_eval_push_collect_empty_stack_raises(void) {
    /*
      PUSH_COLLECT with invalid constant (int 0, not NamespaceSpec).
      Expect KOS_RES_RAISE with RuntimeError.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_COLLECT, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_POP_COLLECT, 0));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);

    PyObject *ns = KosNamespace_FromSpec(nspec, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create ns");
    Py_DECREF(nspec);

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RAISE, "event is RAISE");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
                "exception is RuntimeError");
    PyErr_Clear();

    Py_DECREF(ctx);
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
    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NULL(code_obj, "Code creation fails for malformed bytecode");
    TEST_ASSERT(PyErr_Occurred(), "error is set");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
                "exception is RuntimeError");

    PyErr_Clear();
    Py_DECREF(instr);
    Py_DECREF(nspec);

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
    KosOpResult ev = {0};
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

    code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    if (! code_obj)
        return -1;

    ns = PyDict_New();
    if (! ns)
        goto cleanup;

    frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    if (! frame) {
        Py_DECREF(ns);
        goto cleanup;
    }

    ctx = KosContext_New(frame, kos_tls_current());
    if (! ctx) {
        kos_dynframe_free(frame);
        goto cleanup;
    }

    ev = koskari_eval(ctx, NULL, NULL, NULL);
    if (ev.kind == KOS_RES_FATAL) {
        fprintf(stderr, "  FAIL: koskari_eval returned FATAL\n");
        result = -1;
        goto cleanup;
    }
    if (ev.kind != KOS_RES_RETURN) {
        fprintf(stderr, "  FAIL: event is RETURN (got %d)\n", (int) ev.kind);
        result = -1;
        goto cleanup;
    }
    if (ev.val == NULL || ! Py_IsNone(ev.val)) {
        fprintf(stderr, "  FAIL: result is None\n");
        result = -1;
        goto cleanup;
    }

 cleanup:
    Py_XDECREF(ev.val);
    ev.val = NULL;
    PyErr_Clear();
    if (ctx)
        Py_DECREF(ctx);
    Py_XDECREF(code_obj);
    return result;
}


/* === Test: FAST-slot code on non-fast namespace => RuntimeError at entry === */


static int test_eval_fast_on_dict_ns(void) {
    /*
      Code with FAST opcodes sets KOS_CODE_USES_FAST; the frame install
      (kos_dynframe_new, through kos_code_fits_namespace) refuses a
      namespace that is not a fast-capable KosNamespace (e.g. a plain
      dict) with RuntimeError, before a context exists and any opcode
      runs.
    */

    PyObject *name = PyUnicode_FromString("test");
    PyObject *locals = PyTuple_New(1);
    PyObject *closures = PyTuple_New(0);
    TEST_ASSERT_NOT_NULL(name, "create nspec name");
    TEST_ASSERT_NOT_NULL(locals, "create nspec locals");
    TEST_ASSERT_NOT_NULL(closures, "create nspec closures");

    PyObject *local0 = PyUnicode_FromString("x");
    TEST_ASSERT_NOT_NULL(local0, "create local symbol");
    PyTuple_SET_ITEM(locals, 0, local0);
    PyObject *nspec = KosNamespaceSpec_New(name, locals, closures, NULL);
    Py_DECREF(name);
    Py_DECREF(locals);
    Py_DECREF(closures);
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_FAST, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    Py_DECREF(instr);
    Py_DECREF(nspec);

    /* Verify uses_fast flag was set by validate_fast_slots */
    TEST_ASSERT(KosCode_USES_FAST(code_obj), "code has uses_fast flag");

    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT(frame == NULL, "frame install refused");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
                "exception is RuntimeError");
    PyErr_Clear();

    Py_DECREF(code_obj);

    return 0;
}


/* === Test: BUILD_FUNCTION (no defaults) => Function on stack === */


static int test_eval_build_function_no_defaults(void) {
    /*
      Bytecode: CONST (FunctionSpec), BUILD_FUNCTION ((), (), 0, 0),
      RETURN 1. FunctionSpec = (name=NULL, formals_spec=empty,
      code=inner). Inner code is just RETURN 0. Frame ns must be
      KosNamespace so KosFunction_New can use it as parent_ns. Expect
      KOS_RES_RETURN with a KosFunction.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *inner_instr = PyList_New(1);
    TEST_ASSERT_NOT_NULL(inner_instr, "inner instr list");
    PyList_SET_ITEM(inner_instr, 0, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *inner_code = KosCode_New(nspec, inner_instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(inner_code, "inner Code");
    Py_DECREF(inner_instr);

    PyObject *formals_spec = KosFormalsSpec_New(
        NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(formals_spec, "formals_spec");

    PyObject *fspec = KosFunctionSpec_New(NULL, formals_spec, inner_code);
    TEST_ASSERT_NOT_NULL(fspec, "FunctionSpec");
    Py_DECREF(formals_spec);
    Py_DECREF(inner_code);

    PyObject *bf_arg = Py_BuildValue("(()()ii)", 0, 0);
    TEST_ASSERT_NOT_NULL(bf_arg, "BUILD_FUNCTION extra tuple");

    PyObject *instr = PyList_New(3);
    TEST_ASSERT_NOT_NULL(instr, "instr list");
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, fspec));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_BUILD_FUNCTION, bf_arg));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));
    Py_DECREF(bf_arg);
    Py_DECREF(fspec);

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");
    Py_DECREF(instr);

    PyObject *ns = KosNamespace_FromSpec(nspec, NULL);
    TEST_ASSERT_NOT_NULL(ns, "create namespace");
    Py_DECREF(nspec);

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "val not NULL");
    TEST_ASSERT(PyObject_TypeCheck(ev.val, KOS_EVALUATED_PYTYPE(KosFunctionType)),
                "result is Function");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Phase-refactor resume-matrix tests === */


/*
  Helper: build a minimal nspec+code+frame+context.
  Returns 0 on success; -1 on failure (PyErr set).
  Caller owns code_obj_out and ctx_out references; ns is owned by the frame.

  For yielding code (KosCode_YIELDS), the frame is created under a KosDynGen
  (Generator node) with owner NULL (standalone). After YIELD freeze, the
  caller must install the frozen generator before next koskari_eval;
  if gen_out_opt is non-NULL, *gen_out_opt receives the KosDynGen * for that.
*/
static int make_simple_ctx(
    PyObject *instr,
    PyObject **code_obj_out,
    KosContext **ctx_out,
    KosDynGen **gen_out_opt)
{
    PyObject *nspec = create_test_nspec();
    if (! nspec) return -1;

    *code_obj_out = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    Py_DECREF(nspec);
    if (! *code_obj_out) return -1;

    PyObject *ns = PyDict_New();
    if (! ns) { Py_DECREF(*code_obj_out); return -1; }

    KosDynFrame *frame;
    if (KosCode_YIELDS((KosCode *) *code_obj_out)) {
        KosDynGen *gen = kos_dyngen_new(NULL);
        if (! gen) { Py_DECREF(*code_obj_out); return -1; }
        frame = kos_dynframe_new(
            (KosDynNode *) gen, (KosCode *) *code_obj_out, ns);
        if (! frame) {
            kos_dyngen_free(gen);
            Py_DECREF(*code_obj_out);
            return -1;
        }
        KosDynGen_OWNER(gen) = NULL;
        if (gen_out_opt)
            *gen_out_opt = gen;
    }
    else {
        frame = kos_dynframe_new(NULL, (KosCode *) *code_obj_out, ns);
        if (gen_out_opt)
            *gen_out_opt = NULL;
    }
    if (! frame) { Py_DECREF(*code_obj_out); return -1; }

    *ctx_out = KosContext_New(frame, kos_tls_current());
    if (! *ctx_out) { Py_DECREF(*code_obj_out); return -1; }
    return 0;
}


/* === Test: YIELD empty resume (NULL/NULL) => RETURN with next CONST === */
/*
  These yield tests use the GEN-based path: make_simple_ctx builds a context
  with a KosDynGen (owner NULL). On YIELD, walk_handle_yield freezes the GEN
  and sets SUSPENSION_EVENT; before the next koskari_eval, kos_dyn_gen_install
  restores the chain; revive_yield pops the install placeholder and pushes
  the resume value (Py_None here), then execution continues.
*/


static int test_eval_yield_empty_resume(void) {
    /*
      Bytecode: CONST 42, YIELD 1, CONST 99, RETURN 1.
      First eval (NULL, NULL): KOS_RES_YIELD(42); GEN frozen.
      Second eval: kos_dyn_gen_install, then revive_yield(Py_None) replaces
      placeholder; execution resumes at CONST 99, RETURN 1 => KOS_RES_RETURN(99).
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
    KosDynGen *dyn_gen = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx, &dyn_gen);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
    TEST_ASSERT(ev.kind == KOS_RES_YIELD, "first event is YIELD");
    TEST_ASSERT_NOT_NULL(ev.val, "first val not NULL");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 42, "yielded value is 42");
    Py_XDECREF(ev.val);

    TEST_ASSERT(install_frozen_gen(ctx, dyn_gen) == 0, "install frozen gen");

    ev = koskari_eval(ctx, NULL, NULL, NULL);
    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "second event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "second val not NULL");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 99, "resumed result is 99");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    kos_dyngen_release(dyn_gen);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: YIELD send resume => RETURN with sent value === */


static int test_eval_yield_send_resume(void) {
    /*
      Bytecode: CONST 42, YIELD 1, RETURN 1.
      First eval (NULL, NULL): KOS_RES_YIELD(42).
      Second eval: install frozen gen; revive_yield pushes 7 onto stack;
      RETURN 1 pops TOS => KOS_RES_RETURN(7).
    */
    PyObject *forty_two = PyLong_FromLong(42);

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, forty_two));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(forty_two);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    KosDynGen *dyn_gen = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx, &dyn_gen);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
    TEST_ASSERT(ev.kind == KOS_RES_YIELD, "first event is YIELD");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 42, "yielded value is 42");
    Py_XDECREF(ev.val);

    TEST_ASSERT(install_frozen_gen(ctx, dyn_gen) == 0, "install frozen gen");

    PyObject *send_val = PyLong_FromLong(7);
    ev = koskari_eval(ctx, send_val, NULL, NULL);
    Py_DECREF(send_val);
    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "second event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "second val not NULL");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 7, "returned sent value 7");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    kos_dyngen_release(dyn_gen);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: YIELD throw resume => RAISE event (no handler) === */


static int test_eval_await_throw_completes_iterator(void) {
    /*
      __await__ iterator catches ValueError after first yield and returns 99
      (StopIteration). Second koskari_eval(throw) must call iterator.throw
      first so the await completes without VM-only PyErr.
    */
    PyObject *pre = PyDict_New();
    TEST_ASSERT_NOT_NULL(pre, "prelude dict");
    if (PyRun_String(
            "def _it():\n"
            "    try:\n"
            "        yield 1\n"
            "    except ValueError:\n"
            "        return 99\n"
            "class AW:\n"
            "    def __await__(self):\n"
            "        return _it()\n",
            Py_file_input, pre, pre) < 0) {
        Py_DECREF(pre);
        return -1;
    }

    PyObject *aw_class = PyDict_GetItemString(pre, "AW");
    TEST_ASSERT_NOT_NULL(aw_class, "AW class");
    PyObject *aw = PyObject_CallNoArgs(aw_class);
    TEST_ASSERT_NOT_NULL(aw, "AW instance");

    PyObject *name_test = PyUnicode_FromString("test");
    PyObject *sym_a = PyUnicode_FromString("a");
    PyObject *locals = PyTuple_Pack(1, sym_a);
    PyObject *closures = PyTuple_New(0);
    PyObject *nspec = KosNamespaceSpec_New(name_test, locals, closures, NULL);
    Py_DECREF(name_test);
    Py_DECREF(locals);
    Py_DECREF(closures);

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_VAR, sym_a));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_AWAIT, 1));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    Py_DECREF(instr);
    TEST_ASSERT_NOT_NULL(code_obj, "code");

    PyObject *ns = KosNamespace_FromSpec(nspec, NULL);
    TEST_ASSERT_NOT_NULL(ns, "namespace");
    TEST_ASSERT(KosNamespace_Store(ns, sym_a, aw) == 0, "store a");
    Py_DECREF(nspec);
    Py_DECREF(aw);

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "frame");
    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
    TEST_ASSERT(ev.kind == KOS_RES_AWAIT, "first event is AWAIT");
    Py_XDECREF(ev.val);

    PyObject *throw_exc = PyObject_CallFunction(
        (PyObject *) PyExc_ValueError, "s", "into await iter");
    TEST_ASSERT_NOT_NULL(throw_exc, "throw_exc");
    ev = koskari_eval(ctx, NULL, (PyObject *) PyExc_ValueError, throw_exc);
    Py_DECREF(throw_exc);
    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "second event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "return val");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 99, "await result 99");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    Py_DECREF(code_obj);
    Py_DECREF(pre);

    return 0;
}


static int test_eval_await_throw_reyield(void) {
    /*
      Iterator catches ValueError and yields 99; throw must run first so the
      second suspend exposes val 99 (KOS_MODE_AWAIT / KOS_RES_AWAIT).
    */
    PyObject *pre = PyDict_New();
    TEST_ASSERT_NOT_NULL(pre, "prelude dict");
    if (PyRun_String(
            "def _it():\n"
            "    try:\n"
            "        yield 1\n"
            "    except ValueError:\n"
            "        yield 99\n"
            "class AW:\n"
            "    def __await__(self):\n"
            "        return _it()\n",
            Py_file_input, pre, pre) < 0) {
        Py_DECREF(pre);
        return -1;
    }

    PyObject *aw_class = PyDict_GetItemString(pre, "AW");
    TEST_ASSERT_NOT_NULL(aw_class, "AW class");
    PyObject *aw = PyObject_CallNoArgs(aw_class);
    TEST_ASSERT_NOT_NULL(aw, "AW instance");

    PyObject *name_test = PyUnicode_FromString("test");
    PyObject *sym_a = PyUnicode_FromString("a");
    PyObject *locals = PyTuple_Pack(1, sym_a);
    PyObject *closures = PyTuple_New(0);
    PyObject *nspec = KosNamespaceSpec_New(name_test, locals, closures, NULL);
    Py_DECREF(name_test);
    Py_DECREF(locals);
    Py_DECREF(closures);

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_VAR, sym_a));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_AWAIT, 1));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    Py_DECREF(instr);
    TEST_ASSERT_NOT_NULL(code_obj, "code");

    PyObject *ns = KosNamespace_FromSpec(nspec, NULL);
    TEST_ASSERT_NOT_NULL(ns, "namespace");
    TEST_ASSERT(KosNamespace_Store(ns, sym_a, aw) == 0, "store a");
    Py_DECREF(nspec);
    Py_DECREF(aw);

    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    TEST_ASSERT_NOT_NULL(frame, "frame");
    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
    TEST_ASSERT(ev.kind == KOS_RES_AWAIT, "first event is AWAIT");
    Py_XDECREF(ev.val);

    PyObject *throw_exc = PyObject_CallFunction(
        (PyObject *) PyExc_ValueError, "s", "first throw");
    TEST_ASSERT_NOT_NULL(throw_exc, "throw_exc");
    ev = koskari_eval(ctx, NULL, (PyObject *) PyExc_ValueError, throw_exc);
    Py_DECREF(throw_exc);
    TEST_ASSERT(ev.kind == KOS_RES_AWAIT, "second event is AWAIT (re-yield)");
    TEST_ASSERT_NOT_NULL(ev.val, "second val");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 99, "re-yielded 99");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    Py_DECREF(code_obj);
    Py_DECREF(pre);

    return 0;
}


static int test_eval_yield_throw_resume(void) {
    /*
      Bytecode: CONST 42, YIELD 1, RETURN 1.
      First eval (NULL, NULL): KOS_RES_YIELD(42).
      Second eval: install frozen gen; revive_yield sets PyErr =>
      KOS_MODE_EXCEPTION; no TRY handler present => KOS_RES_RAISE.
    */
    PyObject *forty_two = PyLong_FromLong(42);

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, forty_two));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(forty_two);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    KosDynGen *dyn_gen = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx, &dyn_gen);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
    TEST_ASSERT(ev.kind == KOS_RES_YIELD, "first event is YIELD");
    Py_XDECREF(ev.val);

    TEST_ASSERT(install_frozen_gen(ctx, dyn_gen) == 0, "install frozen gen");

    PyObject *throw_exc = PyObject_CallFunction(
        (PyObject *) PyExc_ValueError, "s", "thrown into yield");
    TEST_ASSERT_NOT_NULL(throw_exc, "create throw_exc");

    ev = koskari_eval(ctx, NULL, (PyObject *) Py_TYPE(throw_exc), throw_exc);
    Py_DECREF(throw_exc);
    TEST_ASSERT(ev.kind == KOS_RES_RAISE, "second event is RAISE");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError), "exception is ValueError");
    PyErr_Clear();

    Py_DECREF(ctx);
    kos_dyngen_release(dyn_gen);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: YIELD throw resume caught by TRY handler => RETURN === */


static int test_eval_yield_throw_caught(void) {
    /*
      Bytecode: PUSH_TRY(5), CONST 42, DUP 1, YIELD 1, JUMP 9,
                LABEL(5), EXCEPTION, POP 1, CONST 55,
                LABEL(9), POP_TRY, RETURN 1.
      First eval (NULL, NULL): KOS_RES_YIELD(42); TRY region stays live.
      Second eval (NULL, throw_exc): eval_phase_entry calls
      try_local_exception_handler, which finds the TRY region and redirects
      PC to the handler => EXCEPTION/POP/CONST 55/POP_TRY/RETURN =>
      KOS_RES_RETURN(55).
    */
    PyObject *forty_two = PyLong_FromLong(42);
    PyObject *fifty_five = PyLong_FromLong(55);
    PyObject *label_5 = PyLong_FromLong(5);
    PyObject *label_9 = PyLong_FromLong(9);

    /*
      The handler sits inside the TRY extent, as the compiler places it,
      and both paths leave through the one POP_TRY.
    */
    PyObject *instr = PyList_New(12);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_TRY, 5));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, forty_two));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_DUP, 1));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_JUMP, 9));
    PyList_SET_ITEM(instr, 5, make_instr(KOS_OP_LABEL, label_5));
    PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_EXCEPTION, 0));
    PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_POP, 1));
    PyList_SET_ITEM(instr, 8, make_instr(KOS_OP_CONST, fifty_five));
    PyList_SET_ITEM(instr, 9, make_instr(KOS_OP_LABEL, label_9));
    PyList_SET_ITEM(instr, 10, make_instr_int(KOS_OP_POP_TRY, 0));
    PyList_SET_ITEM(instr, 11, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(forty_two);
    Py_DECREF(fifty_five);
    Py_DECREF(label_5);
    Py_DECREF(label_9);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    KosDynGen *dyn_gen = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx, &dyn_gen);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
    TEST_ASSERT(ev.kind == KOS_RES_YIELD, "first event is YIELD");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 42, "yielded value is 42");
    Py_XDECREF(ev.val);

    TEST_ASSERT(install_frozen_gen(ctx, dyn_gen) == 0, "install frozen gen");

    PyObject *throw_exc = PyObject_CallFunction(
        (PyObject *) PyExc_ValueError, "s", "thrown into yield");
    TEST_ASSERT_NOT_NULL(throw_exc, "create throw_exc");

    ev = koskari_eval(ctx, NULL, (PyObject *) Py_TYPE(throw_exc), throw_exc);
    Py_DECREF(throw_exc);
    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "second event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "val not NULL");
    TEST_ASSERT(PyLong_Check(ev.val), "val is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 55, "handler result is 55");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    kos_dyngen_release(dyn_gen);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: exception from opcode (op_res != 0) caught by TRY handler === */
/*
  Uses same GEN-based context as other yield tests (make_simple_ctx with YIELDS).
*/


static int test_eval_opcode_error_caught_by_try(void) {
    /*
      Bytecode: PUSH_TRY(3), YIELD 1, JUMP 7,
                LABEL(3), EXCEPTION, POP 1, CONST 77,
                LABEL(7), POP_TRY, RETURN 1.
      YIELD 1 with empty stack returns op_res=-1 with PyErr set.
      eval_phase_post_opcode detects PyErr and calls try_local_exception_handler,
      which finds the TRY region and redirects to the handler =>
      EXCEPTION/POP/CONST 77/POP_TRY/RETURN => KOS_RES_RETURN(77).
      This exercises the exception path inside eval_phase_post_opcode directly.
    */
    PyObject *seventy_seven = PyLong_FromLong(77);
    PyObject *label_3 = PyLong_FromLong(3);
    PyObject *label_7 = PyLong_FromLong(7);

    /*
      The handler sits inside the TRY extent, as the compiler places it,
      and both paths leave through the one POP_TRY.
    */
    PyObject *instr = PyList_New(10);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_PUSH_TRY, 3));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_JUMP, 7));
    PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_LABEL, label_3));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_EXCEPTION, 0));
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_POP, 1));
    PyList_SET_ITEM(instr, 6, make_instr(KOS_OP_CONST, seventy_seven));
    PyList_SET_ITEM(instr, 7, make_instr(KOS_OP_LABEL, label_7));
    PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_POP_TRY, 0));
    PyList_SET_ITEM(instr, 9, make_instr_int(KOS_OP_RETURN, 1));

    Py_DECREF(seventy_seven);
    Py_DECREF(label_3);
    Py_DECREF(label_7);

    PyObject *code_obj = NULL;
    KosContext *ctx = NULL;
    int rc = make_simple_ctx(instr, &code_obj, &ctx, NULL);
    Py_DECREF(instr);
    TEST_ASSERT(rc == 0, "create context");

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);

    TEST_ASSERT(ev.kind == KOS_RES_RETURN, "event is RETURN");
    TEST_ASSERT_NOT_NULL(ev.val, "val not NULL");
    TEST_ASSERT(PyLong_Check(ev.val), "val is int");
    TEST_ASSERT_EQ(PyLong_AsLong(ev.val), 77, "handler result is 77");

    Py_XDECREF(ev.val);
    Py_DECREF(ctx);
    Py_DECREF(code_obj);

    return 0;
}


/* === Test: frozen async gen teardown without resume install === */


static int test_eval_frozen_gen_teardown_without_install(void) {

    /*
      Yield once to freeze GEN.active; KosPyAsyncGen_Teardown must dispose
      the frozen chain without a second kos_dyn_gen_install / koskari_eval.
    */
    PyObject *nspec = create_test_nspec();
    PyObject *val = PyLong_FromLong(42);
    PyObject *instr = PyList_New(2);
    PyObject *code_obj;
    PyObject *ns;
    PyObject *agen_obj;
    KosPyAsyncGen *agen;
    KosDynGen *gen;
    PyObject *step;
    PyObject *got;
    PyObject *typ, *exc, *tb;
    PyObject *yielded;

    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, val));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
    Py_DECREF(val);

    code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    Py_DECREF(nspec);
    Py_DECREF(instr);
    TEST_ASSERT_NOT_NULL(code_obj, "create code");

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    agen_obj = KosAsyncGen_NewFromCodeAndNs((KosCode *) code_obj, ns);
    Py_DECREF(code_obj);
    TEST_ASSERT_NOT_NULL(agen_obj, "create async gen");

    agen = (KosPyAsyncGen *) agen_obj;
    gen = (KosDynGen *) agen->gen;
    TEST_ASSERT_NOT_NULL(gen, "agen owns gen");

    /* the generator has no context until a drive from Python; step it
       as a Python caller would, through __anext__'s awaitable */
    TEST_ASSERT_NULL(agen->ctx, "no context before the first drive");

    step = Py_TYPE(agen_obj)->tp_as_async->am_anext(agen_obj);
    TEST_ASSERT_NOT_NULL(step, "__anext__ awaitable");
    got = Py_TYPE(step)->tp_iternext(step);
    Py_DECREF(step);
    TEST_ASSERT_NULL(got, "the yield completes the awaitable");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_StopIteration),
                "the yield surfaces as StopIteration");

    PyErr_Fetch(&typ, &exc, &tb);
    PyErr_NormalizeException(&typ, &exc, &tb);
    yielded = PyObject_GetAttrString(exc, "value");
    Py_XDECREF(typ);
    Py_XDECREF(exc);
    Py_XDECREF(tb);
    TEST_ASSERT_NOT_NULL(yielded, "yield value");
    TEST_ASSERT_EQ(PyLong_AsLong(yielded), 42, "yielded 42");
    Py_DECREF(yielded);

    TEST_ASSERT_NOT_NULL(KosDynGen_ACTIVE(gen), "gen frozen with active chain");
    TEST_ASSERT_NOT_NULL(agen->ctx, "the drive created the context");
    TEST_ASSERT_NULL(KosContext_DYN_TOP(agen->ctx), "ctx dyn_top cleared on freeze");

    KosPyAsyncGen_Teardown(agen);
    TEST_ASSERT_NULL(agen->gen, "teardown cleared agen->gen");
    TEST_ASSERT_NULL(agen->ctx, "teardown cleared agen->ctx");

    Py_DECREF(agen_obj);

    return 0;
}


/* === Test: flatten onto a consumer clears the wrapper's registers === */


static int test_agen_flatten_clears_wrapper_registers(void) {

  /*
    A generator with a context of its own, stepped on a separate
    consumer context: the flatten moves the chain onto the consumer,
    and the wrapper context must keep no register pointing into it.
    The block and namespace registers are given stand-in values before
    the step, so the test observes whether the flatten clears them.
  */
  PyObject *nspec = create_test_nspec();
  PyObject *val = PyLong_FromLong(42);
  PyObject *instr = PyList_New(2);
  KosSegment *sentinel_seg = NULL;
  KosIterSlots *islots = &KosPyAsyncGenType.kos_iter;
  PyObject *code_obj;
  PyObject *ns;
  PyObject *agen_obj;
  KosPyAsyncGen *agen;
  KosContext *consumer;
  KosIterStepResult r;
  PyObject *out = NULL;
  KosOpResult res;

  PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, val));
  PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_YIELD, 1));
  Py_DECREF(val);

  code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
  Py_DECREF(nspec);
  Py_DECREF(instr);
  TEST_ASSERT_NOT_NULL(code_obj, "create code");

  ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");

  agen_obj = KosAsyncGen_NewFromCodeAndNs((KosCode *) code_obj, ns);
  Py_DECREF(code_obj);
  TEST_ASSERT_NOT_NULL(agen_obj, "create async gen");

  agen = (KosPyAsyncGen *) agen_obj;
  TEST_ASSERT_NOT_NULL(agen->gen, "agen owns gen");
  TEST_ASSERT_NULL(agen->ctx, "no context before the first drive");

  /* the wrapper's context, made as the Python-drive path makes it */
  agen->ctx = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(agen->ctx, "create wrapper context");

  consumer = KosContext_New(NULL, kos_tls_current());
  TEST_ASSERT_NOT_NULL(consumer, "create consumer context");

  KosContext_BLOCK(agen->ctx) = &sentinel_seg;
  KosContext_NAMESPACE(agen->ctx) = Py_None;

  KosContext_EvalEnter(consumer, NULL);
  r = islots->ki_step(consumer, agen_obj, NULL, &out);
  KosContext_EvalExit(consumer);
  TEST_ASSERT(! PyErr_Occurred(), "step raised no exception");
  TEST_ASSERT(r == KOS_ITER_INSTALLED, "step installed onto consumer");

  TEST_ASSERT_NULL(KosContext_BLOCK(agen->ctx),
                   "flatten cleared the wrapper's block register");
  TEST_ASSERT_NULL(KosContext_NAMESPACE(agen->ctx),
                   "flatten cleared the wrapper's namespace register");

  /* run the flattened body to its yield, which freezes the chain back
     into the generator; the teardown then closes it on the wrapper's
     context as the neighbouring test does */
  res = koskari_eval(consumer, NULL, NULL, NULL);
  TEST_ASSERT(res.kind == KOS_RES_YIELD, "body yielded on consumer");
  TEST_ASSERT_EQ(PyLong_AsLong(res.val), 42, "yielded 42");
  Py_XDECREF(res.val);
  islots->ki_finish(agen_obj, 0);

  TEST_ASSERT_NOT_NULL(KosDynGen_ACTIVE((KosDynGen *) agen->gen),
                       "gen frozen with active chain");

  KosPyAsyncGen_Teardown(agen);
  TEST_ASSERT_NULL(agen->gen, "teardown cleared agen->gen");
  TEST_ASSERT_NULL(agen->ctx, "teardown cleared agen->ctx");

  Py_DECREF(agen_obj);
  Py_DECREF(consumer);

  return 0;
}


/* === Test runner === */

/* === Elector demotion counts guard misses alone === */


/* Run a one-row item site once: a fresh frame over a namespace whose
   fast slots hold obj, key and, when given, val. Returns the eval's
   result kind; a raise is cleared. */
static int run_item_site(
    PyObject *code_obj, PyObject *obj, PyObject *key, PyObject *val) {
    PyObject *ns = KosNamespace_New(NULL, KosCode_NSPEC(code_obj));
    if (! ns)
        return -1;
    if (kos_ns_fast_store(ns, 0, obj) < 0 || kos_ns_fast_store(ns, 1, key) < 0
        || (val && kos_ns_fast_store(ns, 2, val) < 0)) {
        Py_DECREF(ns);
        return -1;
    }

    /* the frame takes the namespace reference */
    KosDynFrame *frame = kos_dynframe_new(NULL, (KosCode *) code_obj, ns);
    if (! frame) {
        Py_DECREF(ns);
        return -1;
    }

    KosContext *ctx = KosContext_New(frame, kos_tls_current());
    if (! ctx)
        return -1;

    KosOpResult ev = koskari_eval(ctx, NULL, NULL, NULL);
    int kind = (int) ev.kind;
    Py_XDECREF(ev.val);
    if (ev.kind == KOS_RES_RAISE)
        PyErr_Clear();
    Py_DECREF(ctx);
    return kind;
}


/* A one-row site over the fast locals obj, key, val: the row, then
   RETURN 1 for a load or FAST 0, RETURN 1 for a store. */
static PyObject *make_item_site(int op, PyObject *row, int store) {
    PyObject *name = PyUnicode_FromString("site");
    PyObject *locals = Py_BuildValue("(sss)", "obj", "key", "val");
    PyObject *closures = PyTuple_New(0);
    PyObject *nspec = KosNamespaceSpec_New(name, locals, closures, NULL);
    Py_DECREF(name);
    Py_DECREF(locals);
    Py_DECREF(closures);
    if (! nspec)
        return NULL;

    PyObject *instr = PyList_New(store ? 3 : 2);
    PyList_SET_ITEM(instr, 0, make_instr(op, row));
    if (store) {
        PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_FAST, 0));
        PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));
    } else {
        PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));
    }

    PyObject *code_obj = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return code_obj;
}


/* A decline from an elected body (an index past the list's end) runs
   the protocol once and must not count toward demotion: seven
   declines and then one guard miss leave the site elected. */
static int test_elector_declines_do_not_count_toward_demotion(void) {
    PyObject *row = Py_BuildValue("(iiii)", 0, 1, 0, 0);
    PyObject *code_obj = make_item_site(KOS_OP_FAST_ITEM_FAST, row, 0);
    Py_DECREF(row);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    KosCacheSlot *slot = KosOp_SLOT(&KosCode_CODE(code_obj)[0]);
    TEST_ASSERT_NOT_NULL(slot, "the site has a cache slot");

    PyObject *lst = Py_BuildValue("[ii]", 10, 20);
    PyObject *d = PyDict_New();
    PyObject *zero = PyLong_FromLong(0);
    PyObject *far = PyLong_FromLong(5);

    TEST_ASSERT_EQ(run_item_site(code_obj, lst, zero, NULL), (int) KOS_RES_RETURN,
                   "the first load elects the list body");
    TEST_ASSERT_EQ(slot->kind, (int) KOS_CACHE_ITEM, "the slot is an item slot");
    TEST_ASSERT_EQ(slot->u.item.election.mode, (int) KOS_ELECTOR_ELECTED,
                   "the site is elected");
    TEST_ASSERT_NOT_NULL(slot->u.item.fn.load, "the site holds a body");

    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_EQ(run_item_site(code_obj, lst, far, NULL), (int) KOS_RES_RAISE,
                       "an index past the end declines and raises");
    }
    TEST_ASSERT_EQ(slot->u.item.election.mode, (int) KOS_ELECTOR_ELECTED,
                   "declines leave the site elected");

    TEST_ASSERT_EQ(run_item_site(code_obj, d, zero, NULL), (int) KOS_RES_RAISE,
                   "a dict misses the list guard and raises KeyError generically");
    TEST_ASSERT_EQ(slot->u.item.election.mode, (int) KOS_ELECTOR_ELECTED,
                   "one guard miss after seven declines does not demote");

    Py_DECREF(far);
    Py_DECREF(zero);
    Py_DECREF(d);
    Py_DECREF(lst);
    Py_DECREF(code_obj);
    return 0;
}


/* Eight guard misses demote a store site: the body is dropped and the
   protocol runs, still storing correctly; the tick wrap then empties
   the slot and the next execution elects again. */
static int test_item_store_site_demotes_and_re_elects(void) {
    PyObject *row = Py_BuildValue("(iiii)", 0, 1, 2, 0);
    PyObject *code_obj = make_item_site(KOS_OP_FAST_ITEM_FAST_STORE_FAST, row, 1);
    Py_DECREF(row);
    TEST_ASSERT_NOT_NULL(code_obj, "create Code");

    KosCacheSlot *slot = KosOp_SLOT(&KosCode_CODE(code_obj)[0]);
    TEST_ASSERT_NOT_NULL(slot, "the site has a cache slot");

    PyObject *lst = Py_BuildValue("[ii]", 10, 20);
    PyObject *d = PyDict_New();
    PyObject *zero = PyLong_FromLong(0);
    PyObject *val = PyLong_FromLong(7);

    TEST_ASSERT_EQ(run_item_site(code_obj, lst, zero, val), (int) KOS_RES_RETURN,
                   "the first store elects the list body");
    TEST_ASSERT_EQ(slot->kind, (int) KOS_CACHE_ITEM, "the slot is an item slot");
    TEST_ASSERT_EQ(PyList_GET_ITEM(lst, 0), val, "the list cell took the value");

    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQ(run_item_site(code_obj, d, zero, val), (int) KOS_RES_RETURN,
                       "a dict misses the list guard and stores generically");
    }
    TEST_ASSERT_EQ(slot->kind, (int) KOS_CACHE_ITEM, "the slot stays an item slot");
    TEST_ASSERT_EQ(slot->u.item.election.mode, (int) KOS_ELECTOR_MEGAMORPHIC,
                   "eight misses demote the site");
    TEST_ASSERT_NULL(slot->u.item.fn.store, "a demoted site holds no body");
    TEST_ASSERT_EQ(PyDict_GetItem(d, zero), val, "the dict took the value");

    /* the demotion reset the tick count and the demoting execution
       then ran the protocol once, so the wrap at 256 protocol
       executions since the demotion comes 255 executions later */
    for (int i = 0; i < 254; i++) {
        TEST_ASSERT_EQ(run_item_site(code_obj, d, zero, val), (int) KOS_RES_RETURN,
                       "a demoted site stores through the protocol");
    }
    TEST_ASSERT_EQ(slot->kind, (int) KOS_CACHE_ITEM,
                   "one execution short of the wrap the slot is still demoted");
    TEST_ASSERT_EQ(run_item_site(code_obj, d, zero, val), (int) KOS_RES_RETURN,
                   "the 256th protocol execution since the demotion");
    TEST_ASSERT_EQ(slot->kind, (int) KOS_CACHE_EMPTY,
                   "the tick wrap empties the slot");
    TEST_ASSERT_EQ(run_item_site(code_obj, d, zero, val), (int) KOS_RES_RETURN,
                   "the next execution elects again");
    TEST_ASSERT_EQ(slot->kind, (int) KOS_CACHE_ITEM, "the slot is an item slot");
    TEST_ASSERT_EQ(slot->u.item.election.mode, (int) KOS_ELECTOR_ELECTED,
                   "the dict body is elected");

    Py_DECREF(val);
    Py_DECREF(zero);
    Py_DECREF(d);
    Py_DECREF(lst);
    Py_DECREF(code_obj);
    return 0;
}


/* === frame cache: a frame of any shape fills it === */


/* A function over code that returns 0, with the given formals; the
   parent namespace is the test spec's. Returns a new reference. */
static PyObject *make_function_with_formals(PyObject *formals) {

  PyObject *nspec = create_test_nspec();
  if (! nspec)
    return NULL;

  PyObject *instr = PyList_New(1);
  if (! instr) {
    Py_DECREF(nspec);
    return NULL;
  }
  PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_RETURN, 0));

  PyObject *code = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
  Py_DECREF(instr);
  PyObject *parent = code ? KosNamespace_FromSpec(nspec, NULL) : NULL;
  Py_DECREF(nspec);
  if (! code || ! parent) {
    Py_XDECREF(code);
    Py_XDECREF(parent);
    return NULL;
  }

  PyObject *fn = KosFunction_NewFromFormals(code, formals, parent);
  Py_DECREF(code);
  Py_DECREF(parent);
  return fn;
}


/* The frame a return offers: the function's prepared call namespace
   for one positional argument, in a fresh node. */
static KosDynFrame *make_offered_frame(PyObject *fn) {

  PyObject *arg = PyLong_FromLong(1);
  if (! arg)
    return NULL;
  PyObject *argv[1] = { arg };
  PyObject *ns = KosFunction_PrepareCallNamespaceFromVector(fn, argv, 1, NULL);
  Py_DECREF(arg);
  if (! ns)
    return NULL;
  return kos_dynframe_new(NULL, (KosCode *) KosFunction_CODE(fn), ns);
}


static int test_cache_frame_takes_every_shape(void) {
    /*
      KosFunction_CacheFrame takes a frame of any shape, because the
      general arm of KosFunction_Install serves every shape: a plain
      positional function's frame is cached, and so is the frame of a
      function with star formals, whose simple arity is 0.
    */
    PyObject *pos = Py_BuildValue("(s)", "a");
    TEST_ASSERT_NOT_NULL(pos, "pos tuple");
    PyObject *star = PyUnicode_FromString("rest");
    TEST_ASSERT_NOT_NULL(star, "star name");

    PyObject *plain_formals = KosFormals_NewFull(pos, NULL, NULL, NULL,
                                                 NULL, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(plain_formals, "plain formals");
    PyObject *star_formals = KosFormals_NewFull(pos, NULL, NULL, NULL,
                                                star, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(star_formals, "star formals");
    Py_DECREF(pos);
    Py_DECREF(star);

    PyObject *plain = make_function_with_formals(plain_formals);
    TEST_ASSERT_NOT_NULL(plain, "plain function");
    PyObject *starred = make_function_with_formals(star_formals);
    TEST_ASSERT_NOT_NULL(starred, "star function");
    Py_DECREF(plain_formals);
    Py_DECREF(star_formals);

    TEST_ASSERT_EQ(KosFunction_SIMPLE_ARITY1(plain), 2,
                   "one positional formal is simple arity 1, stored as 2");
    TEST_ASSERT_EQ(KosFunction_SIMPLE_ARITY1(starred), 0,
                   "star formals are not simple: stored as 0");

    KosDynFrame *frame = make_offered_frame(plain);
    TEST_ASSERT_NOT_NULL(frame, "plain frame");
    TEST_ASSERT_EQ(KosFunction_CacheFrame(plain, frame), 1,
                   "a simple function's frame is cached");
    TEST_ASSERT(KosFunction_CACHED_FRAME(plain) == frame,
                "the cached frame is the one offered");

    frame = make_offered_frame(starred);
    TEST_ASSERT_NOT_NULL(frame, "star frame");
    int cached = KosFunction_CacheFrame(starred, frame);
    if (! cached)
      kos_dynframe_free(frame);
    TEST_ASSERT_EQ(cached, 1, "a star-formals function's frame is cached");
    TEST_ASSERT(KosFunction_CACHED_FRAME(starred) == frame,
                "the cached frame is the one offered");

    Py_DECREF(plain);
    Py_DECREF(starred);
    return 0;
}


/* A namespace spec with the given two locals, the second the name a
   named lambda declares for itself after its formals. */
static PyObject *create_nspec_with_self(const char *formal, const char *self) {

  PyObject *name = PyUnicode_FromString("test");
  PyObject *locals = Py_BuildValue("(ss)", formal, self);
  PyObject *closures = PyTuple_New(0);
  PyObject *nspec = (name && locals && closures)
      ? KosNamespaceSpec_New(name, locals, closures, NULL) : NULL;

  Py_XDECREF(name);
  Py_XDECREF(locals);
  Py_XDECREF(closures);
  return nspec;
}


static int test_cache_frame_takes_self_binding_function(void) {
    /*
      A function whose body names it, the named-lambda shape, resolves
      bind_self_state to 1 with self_slot the name's slot, and its
      frame is cached like any simple function's: the self-bind is a
      store the call arms make, not a shape the cache refuses.
    */
    PyObject *nspec = create_nspec_with_self("a", "f");
    TEST_ASSERT_NOT_NULL(nspec, "nspec with a self local");

    PyObject *instr = PyList_New(1);
    TEST_ASSERT_NOT_NULL(instr, "instr list");
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_RETURN, 0));
    PyObject *code = KosCode_New(nspec, instr, NULL, NULL, 1, 1);
    Py_DECREF(instr);
    TEST_ASSERT_NOT_NULL(code, "code");
    PyObject *parent = KosNamespace_FromSpec(nspec, NULL);
    Py_DECREF(nspec);
    TEST_ASSERT_NOT_NULL(parent, "parent namespace");

    PyObject *pos = Py_BuildValue("(s)", "a");
    TEST_ASSERT_NOT_NULL(pos, "pos tuple");
    PyObject *formals = KosFormals_NewFull(pos, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL);
    Py_DECREF(pos);
    TEST_ASSERT_NOT_NULL(formals, "formals");

    PyObject *fn = KosFunction_NewFromFormals(code, formals, parent);
    Py_DECREF(code);
    Py_DECREF(parent);
    Py_DECREF(formals);
    TEST_ASSERT_NOT_NULL(fn, "function");
    KosFunction_NAME(fn) = PyUnicode_FromString("f");
    TEST_ASSERT_NOT_NULL(KosFunction_NAME(fn), "function name");

    TEST_ASSERT_EQ(KosFunction_SIMPLE_ARITY1(fn), 2,
                   "one positional formal is simple arity 1, stored as 2");

    KosDynFrame *frame = make_offered_frame(fn);
    TEST_ASSERT_NOT_NULL(frame, "prepared frame");
    TEST_ASSERT_EQ(KosFunction_BIND_SELF_STATE(fn), 1,
                   "a body naming the function resolves to bind (1)");
    TEST_ASSERT_EQ(((KosFunction *) fn)->self_slot, 1,
                   "the name's slot follows the formal's");
    TEST_ASSERT(kos_ns_fast_peek(KosDynFrame_NS(frame), 1) == fn,
                "the prepared frame holds the function in its slot");

    int cached = KosFunction_CacheFrame(fn, frame);
    if (! cached)
      kos_dynframe_free(frame);
    TEST_ASSERT_EQ(cached, 1, "a self-binding simple function's frame is cached");
    TEST_ASSERT(KosFunction_CACHED_FRAME(fn) == frame,
                "the cached frame is the one offered");
    TEST_ASSERT_NULL(kos_ns_fast_peek(KosDynFrame_NS(frame), 1),
                     "the reset cleared the self slot with the locals");

    Py_DECREF(fn);
    return 0;
}


/* === frame cache: a general-shape call binds into a cached frame === */


/* Code over a spec of the given locals. Returns a new reference. */
static PyObject *make_code_with_locals(PyObject *locals, PyObject *instr) {

  PyObject *name = PyUnicode_FromString("test");
  PyObject *closures = PyTuple_New(0);
  PyObject *nspec = (name && closures)
      ? KosNamespaceSpec_New(name, locals, closures, NULL) : NULL;
  PyObject *code = nspec ? KosCode_New(nspec, instr, NULL, NULL, 1, 1)
      : NULL;

  Py_XDECREF(name);
  Py_XDECREF(closures);
  Py_XDECREF(nspec);
  return code;
}


/* Drive code over a fresh namespace of its spec holding fn in slot 0.
   The caller owns the outcome's value. */
static KosOpResult run_with_function(PyObject *code, PyObject *fn) {

  KosOpResult ev = { 0 };
  KosDynFrame *frame;
  KosContext *ctx;
  PyObject *ns = KosNamespace_New(NULL, KosCode_NSPEC(code));

  ev.kind = KOS_RES_FATAL;
  if (! ns)
    return ev;

  if (kos_ns_fast_store(ns, 0, fn) < 0) {
    Py_DECREF(ns);
    return ev;
  }

  /* the frame takes the namespace reference */
  frame = kos_dynframe_new(NULL, (KosCode *) code, ns);
  if (! frame) {
    Py_DECREF(ns);
    return ev;
  }

  ctx = KosContext_New(frame, kos_tls_current());
  if (! ctx)
    return ev;

  ev = koskari_eval(ctx, NULL, NULL, NULL);
  Py_DECREF(ctx);
  return ev;
}


static int test_general_call_binds_into_cached_frame(void) {
  /*
    f [a *: rest] answers [a, rest]. Its calls take the general arm of
    KosFunction_Install, and once a return has cached its frame the
    next call binds into that frame: (f 1 2 3) then (f 4) in one drive
    answer [1, (2, 3)] and [4, ()]. A call failing in the binder after
    its positional stores ((f big 6 nope: 7), an unexpected keyword)
    raises TypeError, leaves the frame cached and keeps no reference to
    its arguments; the next call (f 8) answers [8, ()] from the same
    frame.
  */
  PyObject *f_locals = Py_BuildValue("(ss)", "a", "rest");
  TEST_ASSERT_NOT_NULL(f_locals, "f locals");
  PyObject *f_instr = PyList_New(4);
  TEST_ASSERT_NOT_NULL(f_instr, "f instr list");
  PyList_SET_ITEM(f_instr, 0, make_instr_int(KOS_OP_FAST, 0));
  PyList_SET_ITEM(f_instr, 1, make_instr_int(KOS_OP_FAST, 1));
  PyList_SET_ITEM(f_instr, 2, make_instr_int(KOS_OP_BUILD_LIST, 2));
  PyList_SET_ITEM(f_instr, 3, make_instr_int(KOS_OP_RETURN, 1));
  PyObject *f_code = make_code_with_locals(f_locals, f_instr);
  Py_DECREF(f_locals);
  Py_DECREF(f_instr);
  TEST_ASSERT_NOT_NULL(f_code, "f code");

  PyObject *parent = KosNamespace_FromSpec(KosCode_NSPEC(f_code), NULL);
  TEST_ASSERT_NOT_NULL(parent, "parent namespace");
  PyObject *pos = Py_BuildValue("(s)", "a");
  TEST_ASSERT_NOT_NULL(pos, "pos tuple");
  PyObject *star = PyUnicode_FromString("rest");
  TEST_ASSERT_NOT_NULL(star, "star name");
  PyObject *formals = KosFormals_NewFull(pos, NULL, NULL, NULL,
                                         star, NULL, NULL, NULL);
  Py_DECREF(pos);
  Py_DECREF(star);
  TEST_ASSERT_NOT_NULL(formals, "star formals");
  PyObject *fn = KosFunction_NewFromFormals(f_code, formals, parent);
  Py_DECREF(f_code);
  Py_DECREF(parent);
  Py_DECREF(formals);
  TEST_ASSERT_NOT_NULL(fn, "star function");
  TEST_ASSERT_EQ(KosFunction_SIMPLE_ARITY1(fn), 0,
                 "star formals are not simple: stored as 0");

  PyObject *locals = Py_BuildValue("(s)", "f");
  TEST_ASSERT_NOT_NULL(locals, "caller locals");
  PyObject *one = PyLong_FromLong(1);
  PyObject *two = PyLong_FromLong(2);
  PyObject *three = PyLong_FromLong(3);
  PyObject *four = PyLong_FromLong(4);
  PyObject *six = PyLong_FromLong(6);
  PyObject *seven = PyLong_FromLong(7);
  PyObject *eight = PyLong_FromLong(8);
  PyObject *big = PyLong_FromLong(1234567891L);
  PyObject *names = Py_BuildValue("(s)", "nope");
  TEST_ASSERT(one && two && three && four && six && seven && eight
              && big && names, "constants");

  /* (list (f 1 2 3) (f 4)) */
  PyObject *instr = PyList_New(10);
  TEST_ASSERT_NOT_NULL(instr, "twice instr list");
  PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_FAST, 0));
  PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, one));
  PyList_SET_ITEM(instr, 2, make_instr(KOS_OP_CONST, two));
  PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_CONST, three));
  PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_CALL, 3));
  PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_FAST, 0));
  PyList_SET_ITEM(instr, 6, make_instr(KOS_OP_CONST, four));
  PyList_SET_ITEM(instr, 7, make_instr_int(KOS_OP_CALL, 1));
  PyList_SET_ITEM(instr, 8, make_instr_int(KOS_OP_BUILD_LIST, 2));
  PyList_SET_ITEM(instr, 9, make_instr_int(KOS_OP_RETURN, 1));
  PyObject *twice = make_code_with_locals(locals, instr);
  Py_DECREF(instr);
  TEST_ASSERT_NOT_NULL(twice, "twice code");

  /* (f big 6 nope: 7) */
  instr = PyList_New(7);
  TEST_ASSERT_NOT_NULL(instr, "bad instr list");
  PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_FAST, 0));
  PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, big));
  PyList_SET_ITEM(instr, 2, make_instr(KOS_OP_CONST, six));
  PyList_SET_ITEM(instr, 3, make_instr(KOS_OP_CONST, seven));
  PyList_SET_ITEM(instr, 4, make_instr(KOS_OP_CONST, names));
  PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_CALL_NAMES, 2));
  PyList_SET_ITEM(instr, 6, make_instr_int(KOS_OP_RETURN, 1));
  PyObject *bad = make_code_with_locals(locals, instr);
  Py_DECREF(instr);
  TEST_ASSERT_NOT_NULL(bad, "bad code");

  /* (f 8) */
  instr = PyList_New(4);
  TEST_ASSERT_NOT_NULL(instr, "good instr list");
  PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_FAST, 0));
  PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, eight));
  PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_CALL, 1));
  PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 1));
  PyObject *good = make_code_with_locals(locals, instr);
  Py_DECREF(instr);
  Py_DECREF(locals);
  TEST_ASSERT_NOT_NULL(good, "good code");

  PyObject *expect = Py_BuildValue("[[i(ii)][i()]]", 1, 2, 3, 4);
  TEST_ASSERT_NOT_NULL(expect, "expected twice result");
  KosOpResult ev = run_with_function(twice, fn);
  TEST_ASSERT(ev.kind == KOS_RES_RETURN, "the two calls return");
  TEST_ASSERT_EQ(PyObject_RichCompareBool(ev.val, expect, Py_EQ), 1,
                 "[[1, (2, 3)], [4, ()]]: no star value left over");
  Py_DECREF(ev.val);
  Py_DECREF(expect);

  KosDynFrame *cached = KosFunction_CACHED_FRAME(fn);
  TEST_ASSERT_NOT_NULL(cached, "the return cached the frame");

  Py_ssize_t big_refs = Py_REFCNT(big);
  ev = run_with_function(bad, fn);
  TEST_ASSERT(ev.kind == KOS_RES_RAISE, "the bad keyword raises");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_TypeError), "as TypeError");
  PyErr_Clear();
  TEST_ASSERT_EQ(Py_REFCNT(big), big_refs,
                 "the failed call keeps no reference to its argument");
  TEST_ASSERT(KosFunction_CACHED_FRAME(fn) == cached,
              "the frame stays cached after the failed bind");

  expect = Py_BuildValue("[i()]", 8);
  TEST_ASSERT_NOT_NULL(expect, "expected good result");
  ev = run_with_function(good, fn);
  TEST_ASSERT(ev.kind == KOS_RES_RETURN, "the next call returns");
  TEST_ASSERT_EQ(PyObject_RichCompareBool(ev.val, expect, Py_EQ), 1,
                 "[8, ()]");
  Py_DECREF(ev.val);
  Py_DECREF(expect);
  TEST_ASSERT(KosFunction_CACHED_FRAME(fn) == cached,
              "the same frame served it and was cached again");

  Py_DECREF(twice);
  Py_DECREF(bad);
  Py_DECREF(good);
  Py_DECREF(one);
  Py_DECREF(two);
  Py_DECREF(three);
  Py_DECREF(four);
  Py_DECREF(six);
  Py_DECREF(seven);
  Py_DECREF(eight);
  Py_DECREF(big);
  Py_DECREF(names);
  Py_DECREF(fn);
  return 0;
}


int run_eval_tests(void) {

    BEGIN_TEST_SUITE("Eval Tests");

    RUN_TEST(test_class_is_not_koscallable);
    RUN_TEST(test_cache_frame_takes_every_shape);
    RUN_TEST(test_cache_frame_takes_self_binding_function);
    RUN_TEST(test_general_call_binds_into_cached_frame);
    RUN_TEST(test_eval_object_add_return);
    RUN_TEST(test_domain_non_null);
    RUN_TEST(test_context_clear_then_dealloc_releases_once);
    RUN_TEST(test_context_clear_drops_chain_caches);
    RUN_TEST(test_context_clear_releases_staged_delivery);
    RUN_TEST(test_context_recycle_releases_staged_delivery);
    RUN_TEST(test_context_churn_past_freelist_cap);
    RUN_TEST(test_domain_thread_default_when_no_tls);
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
    RUN_TEST(test_eval_call_zero_args);
    RUN_TEST(test_eval_fast_on_dict_ns);

    RUN_TEST(test_eval_await_throw_completes_iterator);
    RUN_TEST(test_eval_await_throw_reyield);
    RUN_TEST(test_eval_yield_empty_resume);
    RUN_TEST(test_eval_yield_send_resume);
    RUN_TEST(test_eval_yield_throw_resume);
    RUN_TEST(test_eval_yield_throw_caught);
    RUN_TEST(test_eval_opcode_error_caught_by_try);
    RUN_TEST(test_eval_frozen_gen_teardown_without_install);
    RUN_TEST(test_agen_flatten_clears_wrapper_registers);
    RUN_TEST(test_elector_declines_do_not_count_toward_demotion);
    RUN_TEST(test_item_store_site_demotes_and_re_elects);

    return 0;
}


// The end.
