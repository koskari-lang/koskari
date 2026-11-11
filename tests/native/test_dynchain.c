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
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* Records for the jump targets a node stores: a node holds the
   pointer and never reads through it, so any addresses serve. */
static KosOp fake_ops[4];
#define FAKE_INSN(i) (&fake_ops[(i)])


/* === DynFrame tests === */


static int test_dynframe_new_free(void) {
    /* Test basic frame allocation and freeing */
    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create namespace dict");

    KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    TEST_ASSERT_EQ(KosDynNode_KIND(frame), KOS_DYN_FRAME, "frame kind is FRAME");
    TEST_ASSERT_NULL(KosDynNode_PARENT(frame), "frame parent is NULL");
    TEST_ASSERT_NULL(KosDynFrame_PC(frame), "code-less frame pc is NULL");
    TEST_ASSERT_SAME(KosDynFrame_NS(frame), ns, "frame ns is set");

    kos_dynframe_free(frame);

    return 0;
}


static int test_dynframe_clone(void) {
    /* Test frame cloning with DeepCopy stack */
    PyObject *ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create namespace dict");

    KosDynFrame *frame = kos_dynframe_new(NULL, NULL, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    KosDynFrame_PC(frame) = FAKE_INSN(2);

    KosDynFrame *clone = kos_dynframe_clone(frame);
    TEST_ASSERT_NOT_NULL(clone, "clone frame");

    TEST_ASSERT_NOT_SAME(frame, clone, "clone is different object");
    TEST_ASSERT_SAME(KosDynFrame_PC(clone), FAKE_INSN(2),
                     "clone has same pc");
    TEST_ASSERT(KosDynNode_FLAGS(clone) & KOS_DYNF_CLONED, "clone has CLONED flag");

    kos_dynframe_free(clone);
    kos_dynframe_free(frame);

    return 0;
}


/* === DynPrompt tests === */


static int test_dynprompt_new_free(void) {
    /* Test prompt allocation and freeing */
    PyObject *tag = PyLong_FromLong(300123);
    TEST_ASSERT_NOT_NULL(tag, "create tag");

    KosDynPrompt *prompt = kos_dynprompt_new(NULL, tag);
    TEST_ASSERT_NOT_NULL(prompt, "create prompt");

    TEST_ASSERT_EQ(KosDynNode_KIND(prompt), KOS_DYN_PROMPT, "prompt kind is PROMPT");
    TEST_ASSERT_SAME(KosDynPrompt_TAG(prompt), tag, "prompt tag is set");
    TEST_ASSERT_EQ(Py_REFCNT(tag), 2, "the prompt owns a tag reference");

    kos_dynprompt_free(prompt);
    TEST_ASSERT_EQ(Py_REFCNT(tag), 1, "the free releases the tag");
    Py_DECREF(tag);

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
    KosDynTry *node = kos_dyntry_new(NULL, FAKE_INSN(1));
    TEST_ASSERT_NOT_NULL(node, "create TRY node");

    TEST_ASSERT_EQ(KosDynNode_KIND(node), KOS_DYN_TRY, "node kind is TRY");
    TEST_ASSERT_SAME(KosDynTry_HANDLER_PC(node), FAKE_INSN(1),
                     "handler_pc is set");
    TEST_ASSERT_EQ(KosDynTry_ENABLED(node), 1, "enabled by default");

    kos_dyntry_free(node);

    return 0;
}


static int test_dyntry_clone(void) {
    /* Test TRY node cloning */
    KosDynTry *node = kos_dyntry_new(NULL, FAKE_INSN(3));
    TEST_ASSERT_NOT_NULL(node, "create TRY node");

    KosDynTry_ENABLED(node) = 0;

    KosDynTry *clone = kos_dyntry_clone(node);
    TEST_ASSERT_NOT_NULL(clone, "clone TRY");

    TEST_ASSERT_NOT_SAME(node, clone, "clone is different object");
    TEST_ASSERT_SAME(KosDynTry_HANDLER_PC(clone), FAKE_INSN(3),
                     "clone has same handler_pc");
    TEST_ASSERT_EQ(KosDynTry_ENABLED(clone), 0, "clone has same enabled state");
    TEST_ASSERT(KosDynNode_FLAGS(clone) & KOS_DYNF_CLONED, "clone has CLONED flag");

    kos_dyntry_free(clone);
    kos_dyntry_free(node);

    return 0;
}


static int test_dynfinally_new_free(void) {
    /* Test FINALLY node allocation and freeing */
    KosDynFinally *node = kos_dynfinally_new(NULL, FAKE_INSN(1));
    TEST_ASSERT_NOT_NULL(node, "create FINALLY node");

    TEST_ASSERT_EQ(KosDynNode_KIND(node), KOS_DYN_FINALLY, "node kind is FINALLY");
    TEST_ASSERT_SAME(KosDynFinally_HANDLER_PC(node), FAKE_INSN(1),
                     "handler_pc is set");
    TEST_ASSERT_EQ(KosDynFinally_ENABLED(node), 1, "enabled by default");
    TEST_ASSERT_EQ(KosDynFinally_HANDLING_RAISE(node), 0, "handling_raise is 0");
    TEST_ASSERT_EQ(KosDynFinally_HANDLING_CONTROL(node), 0, "handling_control is 0");

    kos_dynfinally_free(node);

    return 0;
}


static int test_dynloop_new_free(void) {
    /* Test LOOP node allocation and freeing */
    KosDynLoop *node = kos_dynloop_new(NULL, FAKE_INSN(1), FAKE_INSN(2));
    TEST_ASSERT_NOT_NULL(node, "create LOOP node");

    TEST_ASSERT_EQ(KosDynNode_KIND(node), KOS_DYN_LOOP, "node kind is LOOP");
    TEST_ASSERT_SAME(KosDynLoop_BREAK_PC(node), FAKE_INSN(1),
                     "break_pc is set");
    TEST_ASSERT_SAME(KosDynLoop_CONTINUE_PC(node), FAKE_INSN(2),
                     "continue_pc is set");
    TEST_ASSERT_EQ(KosDynLoop_ENABLED(node), 1, "enabled by default");
    TEST_ASSERT_NULL(KosDynLoop_RESULT(node), "result is NULL");

    kos_dynloop_free(node);

    return 0;
}


static int test_dynforeach_new_free(void) {
    /* Test FOREACH node allocation and freeing */
    KosDynForeach *node = kos_dynforeach_new(NULL, FAKE_INSN(1),
                                             FAKE_INSN(2));
    TEST_ASSERT_NOT_NULL(node, "create FOREACH node");

    TEST_ASSERT_EQ(KosDynNode_KIND(node), KOS_DYN_FOREACH, "node kind is FOREACH");
    TEST_ASSERT_SAME(KosDynForeach_BREAK_PC(node), FAKE_INSN(1),
                     "break_pc is set");
    TEST_ASSERT_SAME(KosDynForeach_CONTINUE_PC(node), FAKE_INSN(2),
                     "continue_pc is set");
    TEST_ASSERT_EQ(KosDynForeach_ENABLED(node), 1, "enabled by default");
    TEST_ASSERT_NULL(KosDynForeach_RESULT(node), "result is NULL");
    TEST_ASSERT_NULL(KosDynForeach_ITER(node), "iter is NULL");

    kos_dynforeach_free(node);

    return 0;
}


/* === Chain operations tests === */


static int test_chain_free(void) {
    /* Test freeing a chain of nodes */
    KosDynPrompt *p1 = kos_dynprompt_new(NULL, NULL);
    KosDynPrompt *p2 = kos_dynprompt_new((KosDynNode *) p1, Py_True);
    KosDynPrompt *p3 = kos_dynprompt_new((KosDynNode *) p2, Py_False);

    TEST_ASSERT_NOT_NULL(p1, "create prompt 1");
    TEST_ASSERT_NOT_NULL(p2, "create prompt 2");
    TEST_ASSERT_NOT_NULL(p3, "create prompt 3");

    /* Free entire chain */
    kos_dynchain_free((KosDynNode *) p3, NULL);

    return 0;
}


static int test_chain_clone_segment(void) {
    /* Test cloning a chain segment */
    KosDynPrompt *p1 = kos_dynprompt_new(NULL, NULL);
    KosDynPrompt *p2 = kos_dynprompt_new((KosDynNode *) p1, Py_True);
    KosDynPrompt *p3 = kos_dynprompt_new((KosDynNode *) p2, Py_False);

    TEST_ASSERT_NOT_NULL(p1, "create prompt 1");
    TEST_ASSERT_NOT_NULL(p2, "create prompt 2");
    TEST_ASSERT_NOT_NULL(p3, "create prompt 3");

    /* Clone from p3 down to (but not including) p1 */
    KosDynNode *cloned = kos_dyn_clone_segment((KosDynNode *) p3,
                                                 (KosDynNode *) p1);
    TEST_ASSERT_NOT_NULL(cloned, "clone segment");

    /* Verify clone structure */
    TEST_ASSERT_EQ(KosDynNode_KIND(cloned), KOS_DYN_PROMPT, "cloned top is prompt");
    TEST_ASSERT_SAME(KosDynPrompt_TAG(cloned), Py_False,
                     "cloned has p3 tag");

    KosDynNode *cloned_parent = KosDynNode_PARENT(cloned);
    TEST_ASSERT_NOT_NULL(cloned_parent, "cloned has parent");
    TEST_ASSERT_SAME(KosDynPrompt_TAG(cloned_parent), Py_True,
                     "cloned parent has p2 tag");

    /* The bottom of the clone should point to p1 (the stop node) */
    TEST_ASSERT_SAME(KosDynNode_PARENT(cloned_parent), p1, "clone bottom points to stop");

    /* Free cloned segment (but not p1 which we didn't clone) */
    kos_dynchain_free(cloned, (KosDynNode *) p1);

    /* Free original chain */
    kos_dynchain_free((KosDynNode *) p3, NULL);

    return 0;
}


/* === Continuation tests === */


static int test_continuation_free_null(void) {
    /* Test that freeing NULL continuation doesn't crash */
    kos_continuation_free(NULL);
    return 0;
}


/* === GEN node tests === */


static int test_dyngen_borrows_owner(void) {

    PyObject *owner = PyList_New(0);
    KosGenState state;
    KosDynGen *gen;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    kos_gen_state_init(&state, NULL);

    gen = kos_dyngen_new(owner, &state);
    TEST_ASSERT_NOT_NULL(gen, "create the GEN node");
    TEST_ASSERT_SAME(KosDynGen_OWNER(gen), owner, "the node names the owner");
    TEST_ASSERT_EQ(Py_REFCNT(owner), 1, "the node borrows the owner");
    TEST_ASSERT_SAME(KosDynGen_STATE(gen), &state, "the node names the state");

    kos_dyngen_rest(gen);
    TEST_ASSERT_SAME(state.node, (KosDynNode *) gen,
                     "the node rests in the state");
    TEST_ASSERT_NULL(KosDynGen_OWNER(gen), "a resting node names no owner");

    /* the state's clear takes the resting node out and frees it */
    kos_gen_state_clear(&state);
    TEST_ASSERT_NULL(state.node, "the clear leaves the state");
    TEST_ASSERT_EQ(Py_REFCNT(owner), 1, "the free touches no reference");

    Py_DECREF(owner);
    return 0;
}


/* Build the chain a consumer's step leaves: a frame under an installed
   GEN node whose state is the record at state. */
static KosContext *dyngen_installed_chain(
    PyObject *owner,
    KosGenState *state) {

    PyObject *ns = PyDict_New();
    KosDynGen *gen;
    KosDynFrame *frame;

    if (! ns)
        return NULL;

    kos_gen_state_init(state, NULL);
    state->busy = KOS_GEN_BUSY_CONSUMER;
    gen = kos_dyngen_new(owner, state);
    if (! gen) {
        Py_DECREF(ns);
        return NULL;
    }

    /* the frame takes the namespace */
    frame = kos_dynframe_new((KosDynNode *) gen, NULL, ns);
    if (! frame)
        return NULL;
    return KosContext_New(frame, kos_tls_current());
}


/* The chain outlives the generator: freeing it reads and writes
   nothing through the GEN node's owner or state. The plain build
   checks a poisoned state for any write; under ASan and Valgrind the
   freed state and owner catch any access. */
static int test_dyngen_chain_free_leaves_the_owner_alone(void) {

    unsigned char poison[sizeof(KosGenState)];
    KosGenState *state = malloc(sizeof(KosGenState));
    PyObject *owner = PyList_New(0);
    KosContext *ctx;

    TEST_ASSERT_NOT_NULL(state, "allocate the state record");
    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");

    /* a poisoned state stays as it was */
    ctx = dyngen_installed_chain(owner, state);
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");
    memset(state, 0xA5, sizeof(KosGenState));
    memcpy(poison, state, sizeof(KosGenState));
    Py_DECREF(ctx);
    TEST_ASSERT(memcmp(poison, state, sizeof(KosGenState)) == 0,
                "the chain free writes nothing into the state");
    TEST_ASSERT_EQ(Py_REFCNT(owner), 1, "the chain free touches no reference");

    /* a state and owner already gone are not reached */
    ctx = dyngen_installed_chain(owner, state);
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context again");
    free(state);
    Py_DECREF(owner);
    Py_DECREF(ctx);

    return 0;
}


/* === WITH / DELIM / region payload tests === */


static int test_dynwith_free_exc_unconditional(void) {

    KosContext *ctx;
    PyObject *ns;
    KosDynFrame *frame;
    PyObject *mgr;
    PyObject *enter;
    PyObject *exit;
    KosDynWith *with_node;

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, NULL, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosContext_EvalEnter(ctx, NULL);

    mgr = PyLong_FromLong(200001);
    enter = PyLong_FromLong(200002);
    exit = PyLong_FromLong(200003);
    TEST_ASSERT_NOT_NULL(mgr, "mgr");
    TEST_ASSERT_NOT_NULL(enter, "enter");
    TEST_ASSERT_NOT_NULL(exit, "exit");

    with_node = kos_dyn_push_with_provisional(
        ctx, mgr, enter, exit, 0, NULL, 0, KOS_DYN_WITH_SHOT_MULTI);
    TEST_ASSERT_NOT_NULL(with_node, "push WITH provisional");

    KosDynWith_PHASE(with_node) = KOS_WITH_PHASE_IDLE;
    /* an exception stored on the node resumes propagating when the node
       is unlinked normally: the pop answers -1 with it pending, and the
       three references end up owned by the error indicator */
    TEST_ASSERT(kos_dyn_ob_store_exception(
        (KosDynNode *) with_node,
        Py_NewRef(PyExc_ValueError),
        PyUnicode_FromString("suspended"),
        NULL) == 0,
        "store exception on WITH");

    TEST_ASSERT(kos_dyn_pop_with(ctx, with_node) == -1,
        "pop WITH re-raises the stored exception");
    TEST_ASSERT(PyErr_Occurred() != NULL, "stored exception pending");
    PyErr_Clear();

    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);

    return 0;
}


static int test_dyndelim_clone_owns_chain_copy(void) {
    /*
      A DELIM clone carries a copy of the owned RETURN_CHAIN, never the
      original: a captured in-place invoke resumes over its own copy of
      the handler span, and freeing either DELIM leaves the other whole.
    */

    PyObject *ns;
    KosDynFrame *inner;
    KosDynDelim *delim;
    KosDynNode *clone;
    KosDynNode *chain_copy;

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    inner = kos_dynframe_new(NULL, NULL, ns);
    TEST_ASSERT_NOT_NULL(inner, "create inner frame chain");

    delim = kos_dyndelim_new(
        NULL, KOS_DYN_DELIM_MODE_INPLACE, (KosDynNode *) inner, FAKE_INSN(3),
        NULL, NULL);
    TEST_ASSERT_NOT_NULL(delim, "create DELIM with owned chain");

    clone = kos_dyn_clone((KosDynNode *) delim);
    TEST_ASSERT_NOT_NULL(clone, "clone DELIM");
    chain_copy = KosDynDelim_RETURN_CHAIN((KosDynDelim *) clone);
    TEST_ASSERT_NOT_NULL(chain_copy, "clone carries a RETURN_CHAIN");
    TEST_ASSERT(chain_copy != (KosDynNode *) inner,
                "clone's RETURN_CHAIN is a copy, not the original");
    TEST_ASSERT(KosDynNode_IS_FRAME(chain_copy),
                "the copy is the same kind of node");

    kos_dyndelim_free(delim);
    kos_dyndelim_free((KosDynDelim *) clone);

    return 0;
}


static int test_shift_capture_at_invocation_prompt_frames_inside(void) {
  /*
    A shift bound at an invocation prompt takes a clone of the frame
    below the prompt as the template's bottom. A delim of the segment
    returning into that live frame, in place or as a graft's return,
    names the clone instead: every delim of the template has a return
    frame of the template, never a node of the capturing chain.
  */

  PyObject *ns;
  KosDynFrame *frame;
  KosContext *ctx;
  KosDynPrompt *invocation;
  KosDynPrompt *bound = NULL;
  KosContinuation *k = NULL;
  KosDynNode *node;
  int delims = 0;

  ns = PyDict_New();
  TEST_ASSERT_NOT_NULL(ns, "create ns");
  frame = kos_dynframe_new(NULL, NULL, ns);
  TEST_ASSERT_NOT_NULL(frame, "create frame");
  ctx = KosContext_New(frame, kos_tls_current());
  TEST_ASSERT_NOT_NULL(ctx, "create context");
  KosContext_EvalEnter(ctx, NULL);

  /* frame <- invoke's delim <- invocation prompt (tag True) <- prompt
     (default tag) <- in-place delim <- graft return delim; the last two
     return into frame */
  TEST_ASSERT_NOT_NULL(
      kos_dyn_push_delim(ctx, KOS_DYN_DELIM_MODE_POP_PROMPT, NULL,
                         FAKE_INSN(1), frame, NULL),
      "push the invoke's delim");
  invocation = kos_dyn_push_prompt(ctx, Py_True, NULL);
  TEST_ASSERT_NOT_NULL(invocation, "push the invocation prompt");
  TEST_ASSERT_NOT_NULL(kos_dyn_push_prompt(ctx, NULL, FAKE_INSN(2)),
                       "push a default-tag prompt");
  TEST_ASSERT_NOT_NULL(
      kos_dyn_push_delim(ctx, KOS_DYN_DELIM_MODE_INPLACE, NULL,
                         FAKE_INSN(1), frame, NULL),
      "push an in-place delim returning into frame");
  TEST_ASSERT_NOT_NULL(
      kos_dyn_push_delim(ctx, KOS_DYN_DELIM_MODE_POP_PROMPT, NULL,
                         FAKE_INSN(1), frame, NULL),
      "push a graft return delim returning into frame");

  TEST_ASSERT_EQ(
      kos_shift_capture(ctx, Py_True, FAKE_INSN(0), &k, &bound), 0,
      "the shift binds at the invocation prompt");
  TEST_ASSERT_SAME(bound, invocation,
                   "the capture reports the prompt it stopped at");
  TEST_ASSERT_NOT_NULL(k, "a continuation is captured");

  for (node = KosContinuation_CAPTURED_CHAIN(k); node;
       node = KosDynNode_PARENT(node)) {
    KosDynNode *want;
    KosDynNode *cand;

    if (! KosDynNode_IS_DELIM(node))
      continue;
    delims++;
    want = KosDynDelim_RETURN_FRAME_NODE((KosDynDelim *) node);
    if (! want)
      continue;
    for (cand = KosContinuation_CAPTURED_CHAIN(k); cand && cand != want;
         cand = KosDynNode_PARENT(cand))
      ;
    TEST_ASSERT(cand && KosDynNode_IS_FRAME(cand),
                "a template delim returns into a frame of the template");
  }
  TEST_ASSERT_EQ(delims, 2, "the template holds both delims");

  kos_continuation_free(k);
  KosContext_EvalExit(ctx);
  Py_DECREF(ctx);

  return 0;
}


static int test_dynforeach_free_with_iter(void) {

    KosDynForeach *node;
    PyObject *result;
    PyObject *iter;

    node = kos_dynforeach_new(NULL, FAKE_INSN(1), FAKE_INSN(2));
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

    /* Continuation tests */
    RUN_TEST(test_continuation_free_null);

    /* GEN and payload cleanup tests */
    RUN_TEST(test_dyngen_borrows_owner);
    RUN_TEST(test_dyngen_chain_free_leaves_the_owner_alone);
    RUN_TEST(test_dynwith_free_exc_unconditional);
    RUN_TEST(test_dyndelim_clone_owns_chain_copy);
    RUN_TEST(test_shift_capture_at_invocation_prompt_frames_inside);
    RUN_TEST(test_dynforeach_free_with_iter);

    return 0;
}


// The end.
