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
    TEST_ASSERT(KosDynWind_CODE(wind) == NULL, "wind code is NULL");
    TEST_ASSERT(KosDynWind_ROW(wind) == NULL, "wind row is NULL");
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

    /* Clone from p3 down to (but not including) p1, keeping ids as a
       call/cc capture does */
    KosCloneShape shape;
    KosDynNode *cloned = kos_dyn_clone_segment((KosDynNode *) p3,
                                                 (KosDynNode *) p1, 0,
                                                 0, 0, &shape);
    TEST_ASSERT_NOT_NULL(cloned, "clone segment");
    TEST_ASSERT_EQ(kos_dynnode_id(cloned), kos_dynnode_id((KosDynNode *) p3),
                   "a clone that keeps ids has its source's");

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


static int test_chain_clone_segment_fresh_ids(void) {
    /*
      A delimited continuation's template, and each copy attached from
      it, clone with fresh ids: every clone is a new instance, told
      from its source and from every other copy.
    */
    KosDynPrompt *p1 = kos_dynprompt_new(NULL, NULL);
    KosDynPrompt *p2 = kos_dynprompt_new((KosDynNode *) p1, Py_True);
    KosCloneShape shape;
    KosDynNode *first;
    KosDynNode *second;
    uint32_t source;

    TEST_ASSERT_NOT_NULL(p1, "create prompt 1");
    TEST_ASSERT_NOT_NULL(p2, "create prompt 2");
    source = kos_dynnode_id((KosDynNode *) p2);

    first = kos_dyn_clone_segment((KosDynNode *) p2, (KosDynNode *) p1, 1,
                                  0, 0, &shape);
    second = kos_dyn_clone_segment((KosDynNode *) p2, (KosDynNode *) p1, 1,
                                   0, 0, &shape);
    TEST_ASSERT_NOT_NULL(first, "clone once");
    TEST_ASSERT_NOT_NULL(second, "clone twice");

    TEST_ASSERT(kos_dynnode_id(first) != source,
                "a fresh clone is not its source's instance");
    TEST_ASSERT(kos_dynnode_id(second) != source,
                "nor is the second");
    TEST_ASSERT(kos_dynnode_id(first) != kos_dynnode_id(second),
                "two fresh clones are two instances");

    kos_dynchain_free(first, (KosDynNode *) p1);
    kos_dynchain_free(second, (KosDynNode *) p1);
    kos_dynchain_free((KosDynNode *) p2, NULL);

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


/* === WITH / region payload tests === */


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
        ctx, mgr, enter, exit, 0, NULL, 0, KOS_DYN_CAP_WINDS);
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


/* === Capability and skip link tests === */


/* A code-less frame on parent: a node with no skip bit. */
static KosDynNode *plain_node(KosDynNode *parent) {
    PyObject *ns = PyDict_New();

    if (! ns)
        return NULL;
    return (KosDynNode *) kos_dynframe_new(parent, NULL, ns);
}


/* A TRY on parent: a node with a skip bit. */
static KosDynNode *skip_node(KosDynNode *parent) {
    return (KosDynNode *) kos_dyntry_new(parent, FAKE_INSN(1));
}


static int test_node_header_size(void) {

    TEST_ASSERT_EQ(offsetof(KosDynNode, u), 40,
                   "the node header is 40 bytes");
    return 0;
}


static int test_caps_by_kind(void) {
    /* every node of a kind carries the kind's caps from its init */
    KosDynNode *frame = plain_node(NULL);
    KosDynNode *prompt = (KosDynNode *) kos_dynprompt_new(NULL, NULL);
    KosDynNode *try_ = skip_node(NULL);
    KosDynNode *wind = (KosDynNode *) kos_dynwind_new(NULL, NULL, NULL);
    PyObject *owner = PyList_New(0);
    KosGenState state;
    KosDynNode *gen;

    TEST_ASSERT_NOT_NULL(frame, "create frame");
    TEST_ASSERT_NOT_NULL(prompt, "create prompt");
    TEST_ASSERT_NOT_NULL(try_, "create try");
    TEST_ASSERT_NOT_NULL(wind, "create wind");
    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    kos_gen_state_init(&state, NULL);
    gen = (KosDynNode *) kos_dyngen_new(owner, &state);
    TEST_ASSERT_NOT_NULL(gen, "create the GEN node");

    TEST_ASSERT_EQ(KosDynNode_CAPS(frame),
                   KOS_DYN_CAP_FRAME | KOS_DYN_CAP_MAJOR,
                   "a code-less frame is a major frame and no more");
    TEST_ASSERT(KosDynNode_IS_FRAME(frame), "a frame is found by its cap");
    TEST_ASSERT(! (KosDynNode_CAPS(frame) & KOS_DYN_CAP_SKIP),
                "a frame carries no skip bit");

    TEST_ASSERT_EQ(KosDynNode_CAPS(prompt),
                   KOS_DYN_CAP_INTERCEPT | KOS_DYN_CAP_BARRIER,
                   "a prompt intercepts and is a barrier, and is minor");
    TEST_ASSERT_EQ(KosDynNode_CAPS(try_),
                   KOS_DYN_CAP_INTERCEPT | KOS_DYN_CAP_EFFECT
                   | KOS_DYN_CAP_MAJOR,
                   "a try intercepts and holds an effect");
    TEST_ASSERT_EQ(KosDynNode_CAPS(wind),
                   KOS_DYN_CAP_LEAVES | KOS_DYN_CAP_WINDS
                   | KOS_DYN_CAP_MAJOR,
                   "a wind leaves and winds");
    TEST_ASSERT_EQ(KosDynNode_CAPS(gen),
                   KOS_DYN_CAP_INTERCEPT | KOS_DYN_CAP_GEN,
                   "a GEN intercepts and is minor");
    TEST_ASSERT(KosDynNode_IS_GEN(gen), "a GEN is found by its cap");
    TEST_ASSERT(! KosDynNode_IS_GEN(prompt), "a prompt is not a GEN");

    kos_dyngen_rest((KosDynGen *) gen);
    kos_gen_state_clear(&state);
    Py_DECREF(owner);
    kos_dynchain_free(wind, NULL);
    kos_dynchain_free(try_, NULL);
    kos_dynchain_free(prompt, NULL);
    kos_dynchain_free(frame, NULL);
    return 0;
}


static int test_skip_links_at_init(void) {
    /* a node's skip link is its nearest ancestor with a skip bit */
    KosDynNode *f1 = plain_node(NULL);
    KosDynNode *t1 = skip_node(f1);
    KosDynNode *f2 = plain_node(t1);
    KosDynNode *f3 = plain_node(f2);
    KosDynNode *p = (KosDynNode *) kos_dynprompt_new(f3, NULL);
    KosDynNode *f4 = plain_node(p);

    TEST_ASSERT_NOT_NULL(f4, "build the chain");
    TEST_ASSERT_NULL(KosDynNode_SKIP(f1), "the root names nothing");
    TEST_ASSERT_NULL(KosDynNode_SKIP(t1), "nothing below the try has a bit");
    TEST_ASSERT_SAME(KosDynNode_SKIP(f2), t1, "a node on the try names it");
    TEST_ASSERT_SAME(KosDynNode_SKIP(f3), t1,
                     "a node above a plain node names what that names");
    TEST_ASSERT_SAME(KosDynNode_SKIP(p), t1, "the prompt names the try");
    TEST_ASSERT_SAME(KosDynNode_SKIP(f4), p, "a node on the prompt names it");
    TEST_ASSERT(kos_dyn_skips_hold(f4, NULL), "the chain's links hold");

    KosDynNode_SKIP(f3) = p;
    TEST_ASSERT(! kos_dyn_skips_hold(f4, NULL), "a wrong link is seen");
    kos_dyn_relink_skips(f4, NULL);
    TEST_ASSERT_SAME(KosDynNode_SKIP(f3), t1, "the relink sets it right");
    TEST_ASSERT(kos_dyn_skips_hold(f4, NULL), "the relinked chain holds");

    kos_dynchain_free(f4, NULL);
    return 0;
}


/* Clone a three-node segment resting on a prompt, with the skip-bit
   node at where (0 top, 1 middle, 2 bottom, 3 none), and attach the
   clone onto another prompt. */
static int clone_segment_skip_links(int where) {
    KosDynNode *stop = (KosDynNode *) kos_dynprompt_new(NULL, NULL);
    KosDynNode *onto = (KosDynNode *) kos_dynprompt_new(NULL, Py_True);
    KosDynNode *a = where == 2 ? skip_node(stop) : plain_node(stop);
    KosDynNode *b = where == 1 ? skip_node(a) : plain_node(a);
    KosDynNode *c = where == 0 ? skip_node(b) : plain_node(b);
    KosCloneShape shape;
    KosDynNode *top;
    KosDynNode *mid;
    KosDynNode *bottom;

    TEST_ASSERT_NOT_NULL(c, "build the segment");
    TEST_ASSERT_NOT_NULL(onto, "build the node to attach onto");

    top = kos_dyn_clone_segment(c, stop, 1, 0, 0, &shape);
    TEST_ASSERT_NOT_NULL(top, "clone the segment");
    mid = KosDynNode_PARENT(top);
    bottom = KosDynNode_PARENT(mid);
    TEST_ASSERT_SAME(shape.bottom, bottom, "the shape names the bottom");

    /* inside the clone the links are set; those reaching below it wait */
    switch (where) {
      case 0:
        TEST_ASSERT_SAME(shape.tail, top, "the tail is the skip-bit clone");
        TEST_ASSERT_NULL(KosDynNode_SKIP(top), "top reaches below");
        TEST_ASSERT_NULL(KosDynNode_SKIP(mid), "mid reaches below");
        break;
      case 1:
        TEST_ASSERT_SAME(shape.tail, mid, "the tail is the skip-bit clone");
        TEST_ASSERT_SAME(KosDynNode_SKIP(top), mid, "top names mid");
        TEST_ASSERT_NULL(KosDynNode_SKIP(mid), "mid reaches below");
        break;
      case 2:
        TEST_ASSERT_SAME(shape.tail, bottom, "the tail is the skip-bit clone");
        TEST_ASSERT_SAME(KosDynNode_SKIP(top), bottom, "top names bottom");
        TEST_ASSERT_SAME(KosDynNode_SKIP(mid), bottom, "mid names bottom");
        break;
      default:
        TEST_ASSERT_SAME(shape.tail, top, "with no bit the tail is the top");
        TEST_ASSERT_NULL(KosDynNode_SKIP(top), "top reaches below");
        TEST_ASSERT_NULL(KosDynNode_SKIP(mid), "mid reaches below");
        break;
    }
    TEST_ASSERT_NULL(KosDynNode_SKIP(bottom), "bottom reaches below");

    /* the attach: the bottom is linked, and the tail takes its link */
    kos_dynnode_link(bottom, onto);
    kos_dyn_repair_tail(shape.tail, bottom);
    TEST_ASSERT_SAME(KosDynNode_SKIP(bottom), onto,
                     "the bottom names the node it is on");
    TEST_ASSERT(kos_dyn_skips_hold(top, NULL), "the attached clone holds");

    kos_dynchain_free(top, NULL);
    kos_dynchain_free(c, NULL);
    return 0;
}


static int test_clone_skip_links_bit_at_top(void) {
    return clone_segment_skip_links(0);
}


static int test_clone_skip_links_bit_in_middle(void) {
    return clone_segment_skip_links(1);
}


static int test_clone_skip_links_bit_at_bottom(void) {
    return clone_segment_skip_links(2);
}


static int test_clone_skip_links_no_bit(void) {
    return clone_segment_skip_links(3);
}


static int test_frame_caps_survive_cache_reset(void) {
    /* a frame stripped for a holder's cache stays a major frame */
    KosDynFrame *frame = (KosDynFrame *) plain_node(NULL);
    KosDynFrame *cache = NULL;

    TEST_ASSERT_NOT_NULL(frame, "create frame");
    KosDynNode_CAPS(frame) |= KOS_DYN_CAP_OWNS_NS;
    TEST_ASSERT(kos_dynframe_cache_inert(frame, &cache), "the cache takes it");
    TEST_ASSERT_SAME(cache, frame, "the frame is in the cache");
    TEST_ASSERT_EQ(KosDynNode_CAPS(frame),
                   KOS_DYN_CAP_FRAME | KOS_DYN_CAP_MAJOR,
                   "the reset clears the scope cap and keeps the frame's");

    KosDynFrame_NS(frame) = PyDict_New();
    kos_dynframe_free(frame);
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
    RUN_TEST(test_chain_clone_segment_fresh_ids);

    /* Continuation tests */
    RUN_TEST(test_continuation_free_null);

    /* GEN and payload cleanup tests */
    RUN_TEST(test_dyngen_borrows_owner);
    RUN_TEST(test_dyngen_chain_free_leaves_the_owner_alone);
    RUN_TEST(test_dynwith_free_exc_unconditional);
    RUN_TEST(test_dynforeach_free_with_iter);

    /* Capability and skip link tests */
    RUN_TEST(test_node_header_size);
    RUN_TEST(test_caps_by_kind);
    RUN_TEST(test_skip_links_at_init);
    RUN_TEST(test_clone_skip_links_bit_at_top);
    RUN_TEST(test_clone_skip_links_bit_in_middle);
    RUN_TEST(test_clone_skip_links_bit_at_bottom);
    RUN_TEST(test_clone_skip_links_no_bit);
    RUN_TEST(test_frame_caps_survive_cache_reset);

    return 0;
}


// The end.
