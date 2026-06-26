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
   tests/native/test_dynchain.c

   Memory management tests for DynNode chain infrastructure.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === DynFrame tests === */


static int test_dynframe_new_free(void) {
    /* Test basic frame allocation and freeing */
    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create namespace dict");

    KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    TEST_ASSERT_EQ(KosDynNode_KIND(frame), KOS_DYN_FRAME, "frame kind is FRAME");
    TEST_ASSERT_NULL(KosDynNode_PARENT(frame), "frame parent is NULL");
    TEST_ASSERT_EQ(KosDynFrame_PC(frame), 0, "frame pc is 0");
    TEST_ASSERT_SAME(KosDynFrame_NS(frame), ns, "frame ns is set");

    kos_dynframe_free(frame);

    return 0;
}


static int test_dynframe_clone(void) {
    /* Test frame cloning with DeepCopy stack */
    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create namespace dict");

    KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosDynFrame_PC(frame) = 42;

    KosDynFrame *clone = kos_dynframe_clone(frame);
    TEST_ASSERT_NOT_NULL(clone, "clone frame");

    TEST_ASSERT_NOT_SAME(frame, clone, "clone is different object");
    TEST_ASSERT_EQ(KosDynFrame_PC(clone), 42, "clone has same pc");
    TEST_ASSERT(KosDynNode_FLAGS(clone) & KOS_DYNF_CLONED, "clone has CLONED flag");

    kos_dynframe_free(clone);
    kos_dynframe_free(frame);

    return 0;
}


/* === DynPrompt tests === */


static int test_dynprompt_new_free(void) {
    /* Test prompt allocation and freeing */
    KosDynPrompt *prompt = kos_dynprompt_new(NULL, 123);
    TEST_ASSERT_NOT_NULL(prompt, "create prompt");

    TEST_ASSERT_EQ(KosDynNode_KIND(prompt), KOS_DYN_PROMPT, "prompt kind is PROMPT");
    TEST_ASSERT_EQ(KosDynPrompt_ID(prompt), 123, "prompt id is set");

    kos_dynprompt_free(prompt);

    return 0;
}


/* === DynWind tests === */


static int test_dynwind_new_free(void) {
    /* Test wind node allocation and freeing */
    KosDynWind *wind = kos_dynwind_new(NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(wind, "create wind");

    TEST_ASSERT_EQ(KosDynNode_KIND(wind), KOS_DYN_WIND, "wind kind is WIND");
    TEST_ASSERT(KosDynWind_BEFORE_THUNK(wind) == NULL, "wind before_thunk is NULL");
    TEST_ASSERT(KosDynWind_AFTER_THUNK(wind) == NULL, "wind after_thunk is NULL");
    TEST_ASSERT_EQ(KosDynWind_ENTERED(wind), 0, "wind entered is 0");

    kos_dynwind_free(wind);

    return 0;
}


static int test_dynwind_clone(void) {
    /* Test wind node cloning */
    KosDynWind *wind = kos_dynwind_new(NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(wind, "create wind");

    KosDynWind_ENTERED(wind) = 1;

    KosDynWind *clone = kos_dynwind_clone(wind);
    TEST_ASSERT_NOT_NULL(clone, "clone wind");

    TEST_ASSERT_NOT_SAME(wind, clone, "clone is different object");
    TEST_ASSERT_EQ(KosDynWind_ENTERED(clone), 0, "clone resets entered for transfer");
    TEST_ASSERT(KosDynNode_FLAGS(clone) & KOS_DYNF_CLONED, "clone has CLONED flag");

    kos_dynwind_free(clone);
    kos_dynwind_free(wind);

    return 0;
}


/* === DynTry / DynFinally / DynLoop / DynForeach tests === */


static int test_dyntry_new_free(void) {
    /* Test TRY node allocation and freeing */
    KosDynTry *node = kos_dyntry_new(NULL, 100);
    TEST_ASSERT_NOT_NULL(node, "create TRY node");

    TEST_ASSERT_EQ(KosDynNode_KIND(node), KOS_DYN_TRY, "node kind is TRY");
    TEST_ASSERT_EQ(KosDynTry_HANDLER_PC(node), 100, "handler_pc is set");
    TEST_ASSERT_EQ(KosDynTry_ENABLED(node), 1, "enabled by default");

    kos_dyntry_free(node);

    return 0;
}


static int test_dyntry_clone(void) {
    /* Test TRY node cloning */
    KosDynTry *node = kos_dyntry_new(NULL, 50);
    TEST_ASSERT_NOT_NULL(node, "create TRY node");

    KosDynTry_ENABLED(node) = 0;

    KosDynTry *clone = kos_dyntry_clone(node);
    TEST_ASSERT_NOT_NULL(clone, "clone TRY");

    TEST_ASSERT_NOT_SAME(node, clone, "clone is different object");
    TEST_ASSERT_EQ(KosDynTry_HANDLER_PC(clone), 50, "clone has same handler_pc");
    TEST_ASSERT_EQ(KosDynTry_ENABLED(clone), 0, "clone has same enabled state");
    TEST_ASSERT(KosDynNode_FLAGS(clone) & KOS_DYNF_CLONED, "clone has CLONED flag");

    kos_dyntry_free(clone);
    kos_dyntry_free(node);

    return 0;
}


static int test_dynfinally_new_free(void) {
    /* Test FINALLY node allocation and freeing */
    KosDynFinally *node = kos_dynfinally_new(NULL, 100);
    TEST_ASSERT_NOT_NULL(node, "create FINALLY node");

    TEST_ASSERT_EQ(KosDynNode_KIND(node), KOS_DYN_FINALLY, "node kind is FINALLY");
    TEST_ASSERT_EQ(KosDynFinally_HANDLER_PC(node), 100, "handler_pc is set");
    TEST_ASSERT_EQ(KosDynFinally_ENABLED(node), 1, "enabled by default");
    TEST_ASSERT_EQ(KosDynFinally_HANDLING_RAISE(node), 0, "handling_raise is 0");
    TEST_ASSERT_EQ(KosDynFinally_HANDLING_RETURN(node), 0, "handling_return is 0");
    TEST_ASSERT_NULL(KosDynFinally_HANDLING_RESULT(node), "handling_result is NULL");

    kos_dynfinally_free(node);

    return 0;
}


static int test_dynloop_new_free(void) {
    /* Test LOOP node allocation and freeing */
    KosDynLoop *node = kos_dynloop_new(NULL, 100, 200);
    TEST_ASSERT_NOT_NULL(node, "create LOOP node");

    TEST_ASSERT_EQ(KosDynNode_KIND(node), KOS_DYN_LOOP, "node kind is LOOP");
    TEST_ASSERT_EQ(KosDynLoop_BREAK_PC(node), 100, "break_pc is set");
    TEST_ASSERT_EQ(KosDynLoop_CONTINUE_PC(node), 200, "continue_pc is set");
    TEST_ASSERT_EQ(KosDynLoop_ENABLED(node), 1, "enabled by default");
    TEST_ASSERT_NULL(KosDynLoop_RESULT(node), "result is NULL");

    kos_dynloop_free(node);

    return 0;
}


static int test_dynforeach_new_free(void) {
    /* Test FOREACH node allocation and freeing */
    KosDynForeach *node = kos_dynforeach_new(NULL, 100, 200);
    TEST_ASSERT_NOT_NULL(node, "create FOREACH node");

    TEST_ASSERT_EQ(KosDynNode_KIND(node), KOS_DYN_FOREACH, "node kind is FOREACH");
    TEST_ASSERT_EQ(KosDynForeach_BREAK_PC(node), 100, "break_pc is set");
    TEST_ASSERT_EQ(KosDynForeach_CONTINUE_PC(node), 200, "continue_pc is set");
    TEST_ASSERT_EQ(KosDynForeach_ENABLED(node), 1, "enabled by default");
    TEST_ASSERT_NULL(KosDynForeach_RESULT(node), "result is NULL");
    TEST_ASSERT_NULL(KosDynForeach_ITER(node), "iter is NULL");
    TEST_ASSERT_EQ(KosDynForeach_EXHAUSTED(node), 0, "exhausted is 0");

    kos_dynforeach_free(node);

    return 0;
}


/* === Chain operations tests === */


static int test_chain_free(void) {
    /* Test freeing a chain of nodes */
    KosDynPrompt *p1 = kos_dynprompt_new(NULL, 1);
    KosDynPrompt *p2 = kos_dynprompt_new((KosDynNode *) p1, 2);
    KosDynPrompt *p3 = kos_dynprompt_new((KosDynNode *) p2, 3);

    TEST_ASSERT_NOT_NULL(p1, "create prompt 1");
    TEST_ASSERT_NOT_NULL(p2, "create prompt 2");
    TEST_ASSERT_NOT_NULL(p3, "create prompt 3");

    /* Free entire chain */
    kos_dynchain_free((KosDynNode *) p3, NULL);

    return 0;
}


static int test_chain_clone_segment(void) {
    /* Test cloning a chain segment */
    KosDynPrompt *p1 = kos_dynprompt_new(NULL, 1);
    KosDynPrompt *p2 = kos_dynprompt_new((KosDynNode *) p1, 2);
    KosDynPrompt *p3 = kos_dynprompt_new((KosDynNode *) p2, 3);

    TEST_ASSERT_NOT_NULL(p1, "create prompt 1");
    TEST_ASSERT_NOT_NULL(p2, "create prompt 2");
    TEST_ASSERT_NOT_NULL(p3, "create prompt 3");

    /* Clone from p3 down to (but not including) p1 */
    KosDynNode *cloned = kos_clone_chain_segment((KosDynNode *) p3,
                                                 (KosDynNode *) p1);
    TEST_ASSERT_NOT_NULL(cloned, "clone segment");

    /* Verify clone structure */
    TEST_ASSERT_EQ(KosDynNode_KIND(cloned), KOS_DYN_PROMPT, "cloned top is prompt");
    TEST_ASSERT_EQ(KosDynPrompt_ID(cloned), 3, "cloned has p3 id");

    KosDynNode *cloned_parent = KosDynNode_PARENT(cloned);
    TEST_ASSERT_NOT_NULL(cloned_parent, "cloned has parent");
    TEST_ASSERT_EQ(KosDynPrompt_ID(cloned_parent), 2,
                   "cloned parent has p2 id");

    /* The bottom of the clone should point to p1 (the stop node) */
    TEST_ASSERT_SAME(KosDynNode_PARENT(cloned_parent), p1, "clone bottom points to stop");

    /* Free cloned segment (but not p1 which we didn't clone) */
    kos_dynchain_free(cloned, (KosDynNode *) p1);

    /* Free original chain */
    kos_dynchain_free((KosDynNode *) p3, NULL);

    return 0;
}


/* === LCA computation tests === */


static int test_compute_lca(void) {
    /* Test LCA computation between two chains */
    KosDynPrompt *root = kos_dynprompt_new(NULL, 0);
    KosDynPrompt *a1 = kos_dynprompt_new((KosDynNode *) root, 1);
    KosDynPrompt *a2 = kos_dynprompt_new((KosDynNode *) a1, 2);
    KosDynPrompt *b1 = kos_dynprompt_new((KosDynNode *) root, 3);
    KosDynPrompt *b2 = kos_dynprompt_new((KosDynNode *) b1, 4);

    TEST_ASSERT_NOT_NULL(root, "create root");
    TEST_ASSERT_NOT_NULL(a2, "create a2");
    TEST_ASSERT_NOT_NULL(b2, "create b2");

    /* LCA of a2 and b2 should be root */
    KosDynNode *lca = kos_compute_lca((KosDynNode *) a2, (KosDynNode *) b2, 0);
    TEST_ASSERT_SAME(lca, root, "LCA is root");

    /* LCA of a2 and a1 should be a1 */
    lca = kos_compute_lca((KosDynNode *) a2, (KosDynNode *) a1, 0);
    TEST_ASSERT_SAME(lca, a1, "LCA of a2 and a1 is a1");

    /* Free all nodes */
    kos_dynprompt_free(a2);
    kos_dynprompt_free(a1);
    kos_dynprompt_free(b2);
    kos_dynprompt_free(b1);
    kos_dynprompt_free(root);

    return 0;
}


/* === Continuation tests === */


static int test_continuation_free_null(void) {
    /* Test that freeing NULL continuation doesn't crash */
    kos_continuation_free(NULL);
    return 0;
}


/* === Transfer plan tests === */


static int test_transfer_plan_free_null(void) {
    /* Test that freeing NULL plan doesn't crash */
    kos_free_transfer_plan(NULL);
    return 0;
}


static int test_transfer_plan_free_with_inject_exc_type(void) {
    /*
       Build a transfer plan with INJECT action where exc_type is set
       (is_throw=1), then free before completion. Verifies exc_type
       is DECREF'd and no leak.
    */
    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create namespace dict");

    KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosContext *ctx = KosContext_New(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvalState es;
    KosEvalState_Init(&es, ctx);

    PyObject *value = PyLong_FromLong(42);
    TEST_ASSERT_NOT_NULL(value, "create value for throw");

    KosTransferPlan *plan = NULL;
    int res = kos_build_transfer_plan(&es, (KosDynNode *) frame, value, 1, 0,
                                      &plan);
    Py_DECREF(value);
    TEST_ASSERT(res == 0, "kos_build_transfer_plan succeeded");
    TEST_ASSERT_NOT_NULL(plan, "plan allocated");

    kos_free_transfer_plan(plan);

    KosContext_Free(ctx);

    return 0;
}


/* === GEN cleanup tests === */


static int test_dyngen_free_disposes_active(void) {

    KosDynGen *gen = kos_dyngen_new(NULL);
    PyObject *ns = PyDict_New();
    KosDynFrame *frame;

    TEST_ASSERT_NOT_NULL(gen, "create gen");
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new((KosDynNode *) gen, NULL, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame under gen");

    KosDynGen_ACTIVE(gen) = (KosDynNode *) frame;
    kos_dyngen_release(gen);

    return 0;
}


static int test_dyngen_bootstrap_no_active(void) {

    KosDynGen *gen = kos_dyngen_new(NULL);
    PyObject *ns = PyDict_New();
    KosDynFrame *frame;
    KosContext *ctx;

    TEST_ASSERT_NOT_NULL(gen, "create gen");
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new((KosDynNode *) gen, NULL, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame under gen");
    TEST_ASSERT_NULL(KosDynGen_ACTIVE(gen), "ACTIVE unset at bootstrap");

    ctx = KosContext_New(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosContext_Free(ctx);

    return 0;
}


/* === YIELD_FROM / WITH / DELIM / region payload tests === */


static int test_dynyieldfrom_pop_closes_delegate(void) {

    KosContext *ctx;
    KosEvalState es;
    PyObject *ns;
    KosDynFrame *frame;
    PyObject *tuple;
    PyObject *delegate;
    Py_ssize_t rc_before;
    KosDynYieldFrom *yf;

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, NULL, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = KosContext_New(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvalState_Init(&es, ctx);
    KosEvalState_ThawFromContext(&es);

    tuple = PyTuple_Pack(1, PyLong_FromLong(100001));
    TEST_ASSERT_NOT_NULL(tuple, "create tuple");
    delegate = PyObject_GetIter(tuple);
    Py_DECREF(tuple);
    TEST_ASSERT_NOT_NULL(delegate, "create iterator delegate");

    rc_before = Py_REFCNT(delegate);

    yf = kos_dyn_push_yield_from(&es, delegate, (uint8_t) 0);
    TEST_ASSERT_NOT_NULL(yf, "push YIELD_FROM");

    TEST_ASSERT(kos_dyn_pop_yield_from(&es, yf) == 0, "pop YIELD_FROM");
    TEST_ASSERT_EQ(Py_REFCNT(delegate), rc_before, "delegate refcount restored");

    KosEvalState_FreezeToContext(&es);
    KosContext_Free(ctx);

    return 0;
}


static int test_dynwith_free_exc_unconditional(void) {

    KosContext *ctx;
    KosEvalState es;
    PyObject *ns;
    KosDynFrame *frame;
    PyObject *mgr;
    PyObject *enter;
    PyObject *exit;
    KosDynWith *with_node;

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, NULL, ns, NULL);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = KosContext_New(frame);
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosEvalState_Init(&es, ctx);
    KosEvalState_ThawFromContext(&es);

    mgr = PyLong_FromLong(200001);
    enter = PyLong_FromLong(200002);
    exit = PyLong_FromLong(200003);
    TEST_ASSERT_NOT_NULL(mgr, "mgr");
    TEST_ASSERT_NOT_NULL(enter, "enter");
    TEST_ASSERT_NOT_NULL(exit, "exit");

    with_node = kos_dyn_push_with_provisional(
        &es, mgr, enter, exit, NULL, 0, KOS_DYN_WITH_SHOT_MULTI);
    TEST_ASSERT_NOT_NULL(with_node, "push WITH provisional");

    KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
    KosDynWith_EXC_TYP(with_node) = PyLong_FromLong(200004);
    KosDynWith_EXC_VAL(with_node) = PyLong_FromLong(200005);
    KosDynWith_EXC_TB(with_node) = PyLong_FromLong(200006);

    TEST_ASSERT(kos_dyn_pop_with(&es, with_node) == 0, "pop WITH");

    KosEvalState_FreezeToContext(&es);
    KosContext_Free(ctx);

    return 0;
}


static int test_dyndelim_clone_no_chain_alias(void) {

    PyObject *ns;
    KosDynFrame *inner;
    KosDynDelim *delim;
    KosDynNode *clone;

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    inner = kos_dynframe_new(NULL, NULL, ns, NULL);
    TEST_ASSERT_NOT_NULL(inner, "create inner frame chain");

    delim = kos_dyndelim_new(
        NULL, KOS_DYN_DELIM_MODE_K_RETURN, (KosDynNode *) inner, 7, NULL, NULL);
    TEST_ASSERT_NOT_NULL(delim, "create DELIM with owned chain");

    clone = kos_dynnode_clone((KosDynNode *) delim);
    TEST_ASSERT_NOT_NULL(clone, "clone DELIM");
    TEST_ASSERT_NULL(KosDynDelim_RETURN_CHAIN((KosDynDelim *) clone),
                     "clone does not alias RETURN_CHAIN");

    kos_dyndelim_free(delim);
    kos_dyndelim_free((KosDynDelim *) clone);

    return 0;
}


static int test_dynforeach_free_with_iter(void) {

    KosDynForeach *node;
    PyObject *result;
    PyObject *iter;

    node = kos_dynforeach_new(NULL, 10, 20);
    TEST_ASSERT_NOT_NULL(node, "create FOREACH");

    result = PyLong_FromLong(300001);
    iter = PyLong_FromLong(300002);
    TEST_ASSERT_NOT_NULL(result, "result");
    TEST_ASSERT_NOT_NULL(iter, "iter");

    KosDynForeach_RESULT(node) = result;
    KosDynForeach_ITER(node) = iter;

    kos_dynforeach_free(node);

    return 0;
}


static int test_dynfinally_free_with_handling_result(void) {

    KosDynFinally *node;
    PyObject *handling_result;

    node = kos_dynfinally_new(NULL, 50);
    TEST_ASSERT_NOT_NULL(node, "create FINALLY");

    handling_result = PyLong_FromLong(400001);
    TEST_ASSERT_NOT_NULL(handling_result, "handling_result");

    KosDynFinally_HANDLING_RESULT(node) = handling_result;

    kos_dynfinally_free(node);

    return 0;
}


/* === Test runner === */


int run_dynchain_tests(void) {
    BEGIN_TEST_SUITE("DynChain Memory Tests");

    /* Frame tests */
    RUN_TEST(test_dynframe_new_free);
    RUN_TEST(test_dynframe_clone);

    /* Prompt tests */
    RUN_TEST(test_dynprompt_new_free);

    /* Wind tests */
    RUN_TEST(test_dynwind_new_free);
    RUN_TEST(test_dynwind_clone);

    /* TRY / FINALLY / LOOP / FOREACH tests */
    RUN_TEST(test_dyntry_new_free);
    RUN_TEST(test_dyntry_clone);
    RUN_TEST(test_dynfinally_new_free);
    RUN_TEST(test_dynloop_new_free);
    RUN_TEST(test_dynforeach_new_free);

    /* Chain operation tests */
    RUN_TEST(test_chain_free);
    RUN_TEST(test_chain_clone_segment);

    /* LCA tests */
    RUN_TEST(test_compute_lca);

    /* Continuation tests */
    RUN_TEST(test_continuation_free_null);

    /* Transfer plan tests */
    RUN_TEST(test_transfer_plan_free_null);
    RUN_TEST(test_transfer_plan_free_with_inject_exc_type);

    /* GEN and payload cleanup tests */
    RUN_TEST(test_dyngen_free_disposes_active);
    RUN_TEST(test_dyngen_bootstrap_no_active);
    RUN_TEST(test_dynyieldfrom_pop_closes_delegate);
    RUN_TEST(test_dynwith_free_exc_unconditional);
    RUN_TEST(test_dyndelim_clone_no_chain_alias);
    RUN_TEST(test_dynforeach_free_with_iter);
    RUN_TEST(test_dynfinally_free_with_handling_result);

    return 0;
}


// The end.
