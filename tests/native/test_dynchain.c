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
#include "../../koskari/lib/xfer_goal.h"


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

    KosDynWith_PHASE(with_node) = KOS_EXTENT_IDLE;
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


/* === Traverse return propagation === */


/* The object stop_visit answers on, whether it has been seen, and how
   many visits came after it */
static PyObject *stop_target = NULL;
static int stop_seen = 0;
static int stop_visits_after = 0;


#define STOP_VISIT_RESULT 7


/* A visitor that answers a positive value on stop_target, the way
   gc.get_referrers' visitor answers a match */
static int stop_visit(PyObject *obj, void *arg) {

    (void) arg;
    if (stop_seen)
        stop_visits_after++;
    if (obj == stop_target) {
        stop_seen = 1;
        return STOP_VISIT_RESULT;
    }
    return 0;
}


static void stop_visit_reset(PyObject *target) {
    stop_target = target;
    stop_seen = 0;
    stop_visits_after = 0;
}


/* A match in a WITH node's displaced exception ends the traverse with
   the visitor's value; the obligation behind it is not visited. */
static int test_traverse_with_saved_returns_visit_result(void) {

    KosContext *ctx;
    PyObject *ns;
    KosDynFrame *frame;
    PyObject *mgr;
    PyObject *enter;
    PyObject *exit;
    PyObject *target;
    KosDynWith *with_node;
    int res;

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, NULL, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    KosContext_EvalEnter(ctx, NULL);

    mgr = PyLong_FromLong(200011);
    enter = PyLong_FromLong(200012);
    exit = PyLong_FromLong(200013);
    target = PyList_New(0);
    TEST_ASSERT_NOT_NULL(mgr, "mgr");
    TEST_ASSERT_NOT_NULL(enter, "enter");
    TEST_ASSERT_NOT_NULL(exit, "exit");
    TEST_ASSERT_NOT_NULL(target, "target");

    with_node = kos_dyn_push_with_provisional(
        ctx, mgr, enter, exit, 0, NULL, 0, KOS_DYN_CAP_WINDS);
    TEST_ASSERT_NOT_NULL(with_node, "push WITH provisional");

    KosDynWith_PHASE(with_node) = KOS_EXTENT_IDLE;
    KosDynWith_EXC(with_node)->saved.value = Py_NewRef(target);
    TEST_ASSERT(kos_dyn_ob_store_exception(
        (KosDynNode *) with_node,
        Py_NewRef(PyExc_ValueError),
        PyUnicode_FromString("pending"),
        NULL) == 0,
        "store exception on WITH");

    stop_visit_reset(target);
    res = kos_dynnode_traverse((KosDynNode *) with_node, stop_visit, NULL);
    TEST_ASSERT_EQ(res, STOP_VISIT_RESULT,
                   "traverse answers the visitor's value");
    TEST_ASSERT_EQ(stop_visits_after, 0, "no visit after the match");
    stop_visit_reset(NULL);

    /* the pop re-raises the stored exception and frees the node, which
       releases the displaced one */
    TEST_ASSERT(kos_dyn_pop_with(ctx, with_node) == -1,
        "pop WITH re-raises the stored exception");
    PyErr_Clear();

    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    TEST_ASSERT_EQ(Py_REFCNT(target), 1, "the node released the target");
    Py_DECREF(target);

    return 0;
}


/* A match in a FINALLY node's displaced exception ends the traverse
   with the visitor's value. */
static int test_traverse_finally_saved_returns_visit_result(void) {

    KosDynFinally *node;
    PyObject *target;
    int res;

    node = kos_dynfinally_new(NULL, FAKE_INSN(1));
    TEST_ASSERT_NOT_NULL(node, "create FINALLY node");

    target = PyList_New(0);
    TEST_ASSERT_NOT_NULL(target, "target");
    KosDynFinally_EXC(node)->saved.value = Py_NewRef(target);

    stop_visit_reset(target);
    res = kos_dynnode_traverse((KosDynNode *) node, stop_visit, NULL);
    TEST_ASSERT_EQ(res, STOP_VISIT_RESULT,
                   "traverse answers the visitor's value");
    TEST_ASSERT_EQ(stop_visits_after, 0, "no visit after the match");
    stop_visit_reset(NULL);

    kos_dynfinally_free(node);
    TEST_ASSERT_EQ(Py_REFCNT(target), 1, "the node released the target");
    Py_DECREF(target);

    return 0;
}


/* A match in a context's handled exception ends the context's traverse
   with the visitor's value. */
static int test_traverse_context_exc_returns_visit_result(void) {

    KosContext *ctx;
    PyObject *ns;
    KosDynFrame *frame;
    PyObject *target;
    int res;

    ns = PyDict_New();
    TEST_ASSERT_NOT_NULL(ns, "create ns");

    frame = kos_dynframe_new(NULL, NULL, ns);
    TEST_ASSERT_NOT_NULL(frame, "create frame");

    ctx = KosContext_New(frame, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create context");

    target = PyList_New(0);
    TEST_ASSERT_NOT_NULL(target, "target");
    KosContext_EXC_INFO(ctx)->value = Py_NewRef(target);

    stop_visit_reset(target);
    res = Py_TYPE(ctx)->tp_traverse((PyObject *) ctx, stop_visit, NULL);
    TEST_ASSERT_EQ(res, STOP_VISIT_RESULT,
                   "traverse answers the visitor's value");
    TEST_ASSERT_EQ(stop_visits_after, 0, "no visit after the match");
    stop_visit_reset(NULL);

    Py_DECREF(ctx);
    TEST_ASSERT_EQ(Py_REFCNT(target), 1, "the context released the target");
    Py_DECREF(target);

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
    /* the slot's state and the bytes it lends fit the padding the
       exception triple leaves, so a payload embedding one grows by a
       word over the triple and no more */
    TEST_ASSERT_EQ(sizeof(KosExcSlot), sizeof(KosExcState) + sizeof(void *),
                   "the exception slot is the triple and one word");
    /* an extent's phase and entered flag live in its obligation's
       lent bytes */
    TEST_ASSERT_EQ(sizeof(KosExtent), sizeof(KosObligation),
                   "an extent is no larger than its obligation");
    TEST_ASSERT_EQ(sizeof(((KosDynNode *) 0)->u.wind), 6 * sizeof(void *),
                   "the WIND payload is six words");
    TEST_ASSERT_EQ(sizeof(((KosDynNode *) 0)->u.with), 13 * sizeof(void *),
                   "the WITH payload is thirteen words");
    TEST_ASSERT_EQ(sizeof(((KosDynNode *) 0)->u), 13 * sizeof(void *),
                   "the WITH payload is the largest");
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


static int test_node_sizes_by_kind(void) {
    /* a PROMPT, CC or GEN is a minor node; every other kind is major */
    size_t minor = offsetof(KosDynNode, u) + 2 * sizeof(void *);
    KosDynKind kind;

    for (kind = (KosDynKind) 0; kind < KOS_DYN_UNKNOWN; kind++) {
        int is_minor = kind == KOS_DYN_PROMPT || kind == KOS_DYN_CC
            || kind == KOS_DYN_GEN;

        TEST_ASSERT_EQ(kos_dyn_kind_size(kind),
                       is_minor ? minor : sizeof(KosDynNode),
                       "the kind's size follows its MAJOR cap");
    }
    TEST_ASSERT_EQ(minor, 56, "a minor node is 56 bytes");
    return 0;
}


/* Minor and major nodes come and go through their own freelists: a
   node of one size is never handed out as the other. With the minor
   freelist emptied first, a freed prompt is the only block on it, so
   neither major node made next may be that block and the next prompt
   must be. Each kind's constructor writes its whole payload, and a
   major one its embedded segment, so a wrong-sized block is also an
   overrun the ASan run sees. */
static int test_node_sizes_do_not_mix(void) {
#ifndef KOS_NOFREELIST
    KosDynNode *drain[DYN_MINOR_MAX_FREE];

    /* take what earlier tests left on the minor freelist, so the
       prompts freed below find room there */
    for (int i = 0; i < DYN_MINOR_MAX_FREE; i++) {
        drain[i] = (KosDynNode *) kos_dynprompt_new(NULL, NULL);
        TEST_ASSERT_NOT_NULL(drain[i], "create a draining prompt");
    }
#endif

    for (int round = 0; round < 4; round++) {
        KosDynNode *prompt = (KosDynNode *) kos_dynprompt_new(NULL, NULL);
        KosDynNode *try_;
        KosDynNode *frame;
#ifndef KOS_NOFREELIST
        KosDynNode *was = prompt;
#endif

        TEST_ASSERT_NOT_NULL(prompt, "create a prompt");
        kos_dynchain_free(prompt, NULL);

        /* the freed minor block must not serve the major nodes */
        try_ = skip_node(NULL);
        frame = plain_node(try_);
        TEST_ASSERT_NOT_NULL(frame, "create major nodes");
#ifndef KOS_NOFREELIST
        TEST_ASSERT_NOT_SAME(try_, was, "the try is not the freed prompt");
        TEST_ASSERT_NOT_SAME(frame, was, "the frame is not the freed prompt");
#endif
        TEST_ASSERT(KosDynNode_STACK(try_) != NULL, "the try has its segment");
        kos_dynchain_free(frame, NULL);

        /* nor do the freed major blocks serve a minor node: the freed
           prompt's block is the one the next prompt takes */
        prompt = (KosDynNode *) kos_dynprompt_new(NULL, Py_True);
        TEST_ASSERT_NOT_NULL(prompt, "create a prompt again");
#ifndef KOS_NOFREELIST
        TEST_ASSERT_SAME(prompt, was, "the prompt takes the minor block back");
#endif
        TEST_ASSERT_NULL(KosDynNode_STACK(prompt), "a prompt owns no segment");
        kos_dynchain_free(prompt, NULL);
    }

#ifndef KOS_NOFREELIST
    for (int i = 0; i < DYN_MINOR_MAX_FREE; i++)
        kos_dynchain_free(drain[i], NULL);
#endif
    return 0;
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


/* === Effect list tests === */


/* A consumer's context with a generator's span installed on it: a
   consumer frame, the GEN, the generator's frame, then count TRY nodes
   each handling an exception, and a frame on top where the yield
   runs. The trys are returned outermost first. */
static KosContext *span_with_effects(
    PyObject *owner,
    KosGenState *state,
    int count,
    KosDynNode **trys) {

    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosContext *ctx;

    if (! ns || ! gen_ns)
        return NULL;
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    if (! consumer || ! root)
        return NULL;
    ctx = KosContext_New(consumer, kos_tls_current());
    if (! ctx)
        return NULL;

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(state, root);
    if (kos_dyn_gen_install_bootstrap(
            ctx, owner, state, (KosDynNode *) root) < 0)
        return NULL;
    state->active = NULL;

    for (int i = 0; i < count; i++) {
        trys[i] = (KosDynNode *) kos_dyn_push_try(ctx, FAKE_INSN(1));
        if (! trys[i])
            return NULL;
        KosDynTry_EXC(trys[i])->state = KOS_EXC_HANDLING;
    }
    if (! kos_dyn_push_frame_empty(ctx))
        return NULL;
    return ctx;
}


static KosDynNode **effects_items(KosDynEffects *fx) {
    return fx->cap ? fx->items.heap : fx->items.pair;
}


/* Freeze and install a span holding count effects, twice over. */
static int freeze_install_effects(int count) {

    PyObject *owner = PyList_New(0);
    KosGenState state;
    KosDynNode *trys[4] = { NULL, NULL, NULL, NULL };
    KosContext *ctx;
    KosDynNode *top;
    KosDynNode **heap = NULL;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    ctx = span_with_effects(owner, &state, count, trys);
    TEST_ASSERT_NOT_NULL(ctx, "build the installed span");
    top = KosContext_DYN_TOP(ctx);

    for (int round = 0; round < 2; round++) {
        KosDynGen *gen = kos_dyn_find_gen(ctx);

        TEST_ASSERT_NOT_NULL(gen, "the running frame is a generator's");
        TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, gen), 0, "freeze the span");
        TEST_ASSERT_SAME(state.active, top, "the span rests in the state");
        TEST_ASSERT_EQ(state.effects.count, count, "every effect is listed");
        for (int i = 0; i < count; i++) {
            TEST_ASSERT_SAME(effects_items(&state.effects)[i],
                             trys[count - 1 - i],
                             "the list is innermost first");
            TEST_ASSERT(KosDynTry_EXC(trys[i])->state == KOS_EXC_FROZEN, "the effect is out");
        }
        if (count > 2) {
            TEST_ASSERT_EQ(state.effects.cap, count,
                           "past two the list is on the heap");
            if (round)
                TEST_ASSERT_SAME(state.effects.items.heap, heap,
                                 "the array is kept for the next yield");
            heap = state.effects.items.heap;
        } else {
            TEST_ASSERT_EQ(state.effects.cap, 0, "two entries stay inline");
        }

        /* the count the install computes from depths is the walk's */
        TEST_ASSERT_EQ(
            (unsigned) KosDynFrame_DEPTH(state.frozen.frame)
                - KosDynFrame_DEPTH(state.bottom) + 1u,
            kos_dyn_frame_count(state.active, NULL),
            "the span's depths give its frame count");

        TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 0,
                       "install the span");
        TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), top, "the span is back");
        TEST_ASSERT_EQ(state.effects.count, 0, "the list is used up");
        for (int i = 0; i < count; i++)
            TEST_ASSERT(KosDynTry_EXC(trys[i])->state == KOS_EXC_HANDLING, "the effect is in");
        TEST_ASSERT(kos_dyn_skips_hold(top, NULL), "the skip links hold");
    }

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, kos_dyn_find_gen(ctx)), 0,
                   "freeze to tear down");
    kos_gen_state_clear(&state);
    TEST_ASSERT_EQ(state.effects.cap, 0, "the clear frees the array");
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    return 0;
}


static int test_freeze_install_no_effects(void) {
    return freeze_install_effects(0);
}


static int test_freeze_install_one_effect(void) {
    return freeze_install_effects(1);
}


static int test_freeze_install_two_effects(void) {
    return freeze_install_effects(2);
}


static int test_freeze_install_three_effects(void) {
    return freeze_install_effects(3);
}


/* A freeze whose list cannot be sized refuses with MemoryError and
   leaves the chain, the span and its effects as they were. */
static int test_freeze_refused_changes_nothing(void) {

    PyObject *owner = PyList_New(0);
    KosGenState state;
    KosDynNode *trys[4] = { NULL, NULL, NULL, NULL };
    KosContext *ctx;
    KosDynNode *top;
    KosDynGen *gen;
    int r;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    ctx = span_with_effects(owner, &state, 3, trys);
    TEST_ASSERT_NOT_NULL(ctx, "build the installed span");
    top = KosContext_DYN_TOP(ctx);
    gen = kos_dyn_find_gen(ctx);
    TEST_ASSERT_NOT_NULL(gen, "the running frame is a generator's");

    test_refuse_realloc_begin(0);
    r = kos_dyn_gen_freeze(ctx, gen);
    test_refuse_realloc_end();

    TEST_ASSERT_EQ(r, -2, "the freeze refuses");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_MemoryError),
                "with MemoryError");
    PyErr_Clear();
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), top, "the chain is whole");
    TEST_ASSERT_SAME(KosDynNode_PARENT(state.bottom), (KosDynNode *) gen,
                     "the span is still on its GEN");
    TEST_ASSERT_NULL(state.active, "the state holds no span");
    TEST_ASSERT_EQ(state.effects.count, 0, "nothing is listed");
    for (int i = 0; i < 3; i++)
        TEST_ASSERT(KosDynTry_EXC(trys[i])->state == KOS_EXC_HANDLING, "no effect went out");

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, gen), 0, "a later freeze works");
    kos_gen_state_clear(&state);
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    return 0;
}


/* Push count TRY nodes onto the chain's top, each handling an
   exception, and a frame on them. The trys are stored outermost
   first. Returns 0, or -1. */
static int push_trys_and_frame(
    KosContext *ctx,
    int count,
    KosDynNode **trys) {

    for (int i = 0; i < count; i++) {
        trys[i] = (KosDynNode *) kos_dyn_push_try(ctx, FAKE_INSN(1));
        TEST_ASSERT_NOT_NULL(trys[i], "push a try");
        KosDynTry_EXC(trys[i])->state = KOS_EXC_HANDLING;
    }
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx),
                         "push a frame on them");
    return 0;
}


/* A span frozen under three effects, then under five: the heap array
   grows to five, the list innermost first. A later freeze under one
   effect keeps the array of five where it is. */
static int test_freeze_effects_grow_between_yields(void) {

    PyObject *owner = PyList_New(0);
    KosGenState state;
    KosDynNode *trys[5] = { NULL, NULL, NULL, NULL, NULL };
    KosDynNode **items;
    KosDynNode **heap;
    KosDynNode *top;
    KosContext *ctx;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    ctx = span_with_effects(owner, &state, 3, trys);
    TEST_ASSERT_NOT_NULL(ctx, "build the installed span");

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, kos_dyn_find_gen(ctx)), 0,
                   "freeze under three");
    TEST_ASSERT_EQ(state.effects.cap, 3, "the array holds three");
    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 0,
                   "install the span");
    TEST_ASSERT_EQ(push_trys_and_frame(ctx, 2, &trys[3]), 0,
                   "two more effects on the span");
    top = KosContext_DYN_TOP(ctx);

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, kos_dyn_find_gen(ctx)), 0,
                   "freeze under five");
    TEST_ASSERT_SAME(state.active, top, "the span rests in the state");
    TEST_ASSERT_EQ(state.effects.count, 5, "every effect is listed");
    TEST_ASSERT_EQ(state.effects.cap, 5, "the array grew to five");
    items = effects_items(&state.effects);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_SAME(items[i], trys[4 - i],
                         "the list is innermost first");
        TEST_ASSERT(KosDynTry_EXC(trys[i])->state == KOS_EXC_FROZEN,
                    "the effect is out");
    }
    heap = state.effects.items.heap;

    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 0,
                   "install the span");
    for (int i = 0; i < 5; i++)
        TEST_ASSERT(KosDynTry_EXC(trys[i])->state == KOS_EXC_HANDLING,
                    "the effect is in");

    /* the outer four are done handling */
    for (int i = 0; i < 4; i++)
        KosDynTry_EXC(trys[i])->state = KOS_EXC_NONE;
    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, kos_dyn_find_gen(ctx)), 0,
                   "freeze under one");
    TEST_ASSERT_EQ(state.effects.count, 1, "one effect is listed");
    TEST_ASSERT_SAME(effects_items(&state.effects)[0], trys[4],
                     "the innermost");
    TEST_ASSERT(KosDynTry_EXC(trys[4])->state == KOS_EXC_FROZEN,
                "its effect is out");
    TEST_ASSERT_EQ(state.effects.cap, 5, "the array of five is kept");
    TEST_ASSERT_SAME(state.effects.items.heap, heap, "where it was");

    kos_gen_state_clear(&state);
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    return 0;
}


/* A freeze whose list must grow the array it already has, refused:
   MemoryError, and the array, its size, the chain and every effect as
   they were. A later freeze grows the array. */
static int test_freeze_regrow_refused_changes_nothing(void) {

    PyObject *owner = PyList_New(0);
    KosGenState state;
    KosDynNode *trys[5] = { NULL, NULL, NULL, NULL, NULL };
    KosDynNode **heap;
    KosDynNode *top;
    KosDynGen *gen;
    KosContext *ctx;
    int refused;
    int r;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    ctx = span_with_effects(owner, &state, 3, trys);
    TEST_ASSERT_NOT_NULL(ctx, "build the installed span");

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, kos_dyn_find_gen(ctx)), 0,
                   "freeze under three");
    TEST_ASSERT_EQ(state.effects.cap, 3, "the array holds three");
    heap = state.effects.items.heap;
    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 0,
                   "install the span");
    TEST_ASSERT_EQ(push_trys_and_frame(ctx, 2, &trys[3]), 0,
                   "two more effects on the span");
    top = KosContext_DYN_TOP(ctx);
    gen = kos_dyn_find_gen(ctx);
    TEST_ASSERT_NOT_NULL(gen, "the running frame is a generator's");

    test_refuse_realloc_begin(0);
    r = kos_dyn_gen_freeze(ctx, gen);
    refused = test_refuse_realloc_end();

    TEST_ASSERT_EQ(r, -2, "the freeze refuses");
    TEST_ASSERT_EQ(refused, 1, "the regrow was refused");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_MemoryError),
                "with MemoryError");
    PyErr_Clear();
    TEST_ASSERT_SAME(state.effects.items.heap, heap, "the array stands");
    TEST_ASSERT_EQ(state.effects.cap, 3, "at three");
    TEST_ASSERT_EQ(state.effects.count, 0, "nothing is listed");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), top, "the chain is whole");
    TEST_ASSERT_SAME(KosDynNode_PARENT(state.bottom), (KosDynNode *) gen,
                     "the span is still on its GEN");
    TEST_ASSERT_NULL(state.active, "the state holds no span");
    for (int i = 0; i < 5; i++)
        TEST_ASSERT(KosDynTry_EXC(trys[i])->state == KOS_EXC_HANDLING,
                    "no effect went out");

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, gen), 0, "a later freeze works");
    TEST_ASSERT_EQ(state.effects.count, 5, "and lists every effect");
    TEST_ASSERT_EQ(state.effects.cap, 5, "in a grown array");

    kos_gen_state_clear(&state);
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    return 0;
}


/* === Winding across a yield === */


/* a dyn-wind's row with neither segment: its enter and its leave
   complete at once */
static const KosExtra bare_wind_row;


/* Leave every winder of the span at dyn_top, as a yield does, and
   freeze it. Returns 0 on success. */
static int wind_out_and_freeze(KosContext *ctx, PyObject *val) {

    KosDynGen *gen = kos_dyn_find_gen(ctx);
    KosXferGoal *goal;
    KosOpResult r;

    TEST_ASSERT_NOT_NULL(gen, "the running frame is a generator's");
    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, gen), 1,
                   "a span with winders is not frozen");
    goal = kos_dyn_gen_wind_out_begin(ctx, gen, val);
    TEST_ASSERT_NOT_NULL(goal, "record the winders");
    r = kos_xfer_goal_run_kept(ctx, goal);
    TEST_ASSERT_EQ(r.kind, KOS_RES_YIELD, "the leaves answer the yield");
    TEST_ASSERT_SAME(r.val, val, "with the value yielded");
    Py_DECREF(r.val);
    TEST_ASSERT_NULL(KosContext_GOAL(ctx), "the goal is retired");
    TEST_ASSERT_EQ(kos_dyn_gen_freeze_wound(ctx, gen), 0, "rest the span");
    return 0;
}


/* A span of a root frame, a handling TRY, a dyn-wind, a second
   handling TRY, a second dyn-wind and a frame on top. A yield leaves
   the winds innermost first with the span cut above each, and takes
   every effect out; the install puts the span back piece by piece. */
static int test_wind_out_and_in(void) {

    PyObject *owner = PyList_New(0);
    PyObject *val = PyLong_FromLong(7);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *trys[2];
    KosDynNode *winds[2];
    KosDynNode *top;
    KosXferPiece *pieces;
    KosContext *ctx;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(val, "create a value to yield");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;

    for (int i = 0; i < 2; i++) {
        trys[i] = (KosDynNode *) kos_dyn_push_try(ctx, FAKE_INSN(1));
        TEST_ASSERT_NOT_NULL(trys[i], "push a try");
        KosDynTry_EXC(trys[i])->state = KOS_EXC_HANDLING;
        winds[i] = (KosDynNode *) kos_dyn_push_wind(
            ctx, NULL, &bare_wind_row);
        TEST_ASSERT_NOT_NULL(winds[i], "push a wind");
        KosDynWind_ENTERED(winds[i]) = 1;
    }
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx), "push the top frame");
    top = KosContext_DYN_TOP(ctx);

    for (int round = 0; round < 2; round++) {
        TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield");

        TEST_ASSERT_SAME(state.active, top, "the span rests in the state");
        TEST_ASSERT_EQ(state.wound_count, 4,
                       "the winders are listed with the effects");
        TEST_ASSERT_EQ(state.piece_count, 2, "both winders are recorded");
        if (round)
            TEST_ASSERT_SAME(state.pieces, pieces,
                             "the records are kept for the next yield");
        pieces = state.pieces;
        TEST_ASSERT_SAME(pieces[0].regs.top, winds[0], "outermost first");
        TEST_ASSERT_SAME(pieces[0].rest, trys[1],
                         "with the node resting on it");
        TEST_ASSERT_SAME(pieces[1].regs.top, winds[1], "then the inner");
        TEST_ASSERT_SAME(pieces[1].rest, top, "under the top frame");
        TEST_ASSERT_SAME(KosDynNode_PARENT(top), winds[1],
                         "the span rests whole");
        TEST_ASSERT_SAME(KosDynNode_PARENT(trys[1]), winds[0],
                         "at both winders");
        for (int i = 0; i < 2; i++) {
            TEST_ASSERT(KosDynTry_EXC(trys[i])->state == KOS_EXC_FROZEN,
                        "the effect is out");
            TEST_ASSERT_EQ(KosDynWind_ENTERED(winds[i]), 0,
                           "the wind is left");
            TEST_ASSERT_EQ(KosDynWind_PHASE(winds[i]), KOS_EXTENT_IDLE,
                           "and can be entered again");
        }

        TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 1,
                       "the install stops at the outermost winder");
        TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), winds[0],
                         "which is the chain's top");
        TEST_ASSERT_NULL(KosDynNode_PARENT(trys[1]),
                         "what rests on it is cut off");
        TEST_ASSERT_NOT_NULL(KosContext_GOAL(ctx), "and a goal is pushed");
        TEST_ASSERT(KosDynTry_EXC(trys[0])->state == KOS_EXC_HANDLING,
                    "the effect outside it is in");
        TEST_ASSERT(KosDynTry_EXC(trys[1])->state == KOS_EXC_FROZEN,
                    "the effect inside it is not");

        TEST_ASSERT_EQ(kos_xfer_goal_drive(ctx).kind,
                       KOS_RES_NORMAL, "the goal enters and attaches");
        TEST_ASSERT_NULL(KosContext_GOAL(ctx), "and retires");
        TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), top, "the span is back");
        TEST_ASSERT_SAME(KosDynNode_PARENT(top), winds[1], "whole");
        TEST_ASSERT_SAME(KosDynNode_PARENT(trys[1]), winds[0],
                         "at both winders");
        TEST_ASSERT_EQ(state.piece_count, 0, "the records are used up");
        TEST_ASSERT_EQ(state.effects.count, 0, "and the list");
        for (int i = 0; i < 2; i++) {
            TEST_ASSERT(KosDynTry_EXC(trys[i])->state == KOS_EXC_HANDLING,
                        "the effect is in");
            TEST_ASSERT_EQ(KosDynWind_ENTERED(winds[i]), 1,
                           "the wind is entered");
        }
        TEST_ASSERT(kos_dyn_skips_hold(top, NULL), "the skip links hold");
    }

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield to tear down");
    kos_gen_state_clear(&state);
    TEST_ASSERT_NULL(state.pieces, "the clear frees the records");
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    Py_DECREF(val);
    return 0;
}


/* A generator's state cleared while the goal entering its winders is
   still on the consumer's context, as the collector's clear of the
   owner can come before the clear of that context: the state keeps
   the storage the goal lives in, the context's clear gives the goal up
   and lets go of the owner, and the next clear of the state frees the
   storage. */
static int test_wind_goal_outlives_the_state_clear(void) {

    PyObject *owner = PyList_New(0);
    PyObject *val = PyLong_FromLong(7);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *wind;
    KosXferGoal *goal;
    KosContext *ctx;
    Py_ssize_t owner_refs;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(val, "create a value to yield");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");
    owner_refs = Py_REFCNT(owner);

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;

    wind = (KosDynNode *) kos_dyn_push_wind(ctx, NULL, &bare_wind_row);
    TEST_ASSERT_NOT_NULL(wind, "push a wind");
    KosDynWind_ENTERED(wind) = 1;
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx), "push the top frame");

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield");
    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 1,
                   "the install stops at the winder");
    goal = KosContext_GOAL(ctx);
    TEST_ASSERT_NOT_NULL(goal, "a goal is pushed");
    TEST_ASSERT_SAME((KosXferPiece *) (goal + 1), state.pieces,
                     "in the state's storage");
    TEST_ASSERT_EQ(Py_REFCNT(owner), owner_refs + 1,
                   "the goal holds the owner");
    KosContext_EvalExit(ctx);

    kos_gen_state_clear(&state);
    TEST_ASSERT_SAME(state.pieces, (KosXferPiece *) (goal + 1),
                     "the live goal keeps its storage");
    TEST_ASSERT_EQ(state.piece_count, 0, "with no records");
    TEST_ASSERT_EQ(state.wound_count, 0, "and no list");

    Py_DECREF(ctx);
    TEST_ASSERT_EQ(Py_REFCNT(owner), owner_refs,
                   "the context's clear gave the goal up");

    kos_gen_state_clear(&state);
    TEST_ASSERT_NULL(state.pieces, "the next clear frees the storage");
    TEST_ASSERT_EQ(state.piece_cap, 0, "and forgets it");

    Py_DECREF(owner);
    Py_DECREF(val);
    return 0;
}


/* Push a dyn-wind, entered, and a frame on it. Returns 0, or -1. */
static int push_entered_wind(
    KosContext *ctx,
    KosDynNode **wind,
    KosDynNode **top) {

    *wind = (KosDynNode *) kos_dyn_push_wind(ctx, NULL, &bare_wind_row);
    TEST_ASSERT_NOT_NULL(*wind, "push a wind");
    KosDynWind_ENTERED(*wind) = 1;
    *top = (KosDynNode *) kos_dyn_push_frame_empty(ctx);
    TEST_ASSERT_NOT_NULL(*top, "push a frame on it");
    return 0;
}


/* On an installed span of a root frame, a dyn-wind and a frame: yield
   under the one winder, enter it again, then push a second dyn-wind
   and a frame on the first frame. Returns 0, or -1. */
static int grow_one_winder_span(
    KosContext *ctx,
    PyObject *owner,
    PyObject *val,
    KosGenState *state,
    KosDynNode **winds,
    KosDynNode **tops) {

    TEST_ASSERT_EQ(push_entered_wind(ctx, &winds[0], &tops[0]), 0,
                   "the outer wind");
    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield under one");
    TEST_ASSERT_EQ(state->piece_count, 1, "one winder is recorded");
    TEST_ASSERT_EQ(state->piece_cap, 1, "the allocation holds one");
    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, state), 1,
                   "the install stops at the winder");
    TEST_ASSERT_EQ(kos_xfer_goal_drive(ctx).kind, KOS_RES_NORMAL,
                   "the goal enters it and attaches");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), tops[0], "the span is back");
    TEST_ASSERT_EQ(push_entered_wind(ctx, &winds[1], &tops[1]), 0,
                   "the inner wind");
    return 0;
}


/* The second yield of a span has two winders, one more than the
   pieces allocation holds after the first: the allocation grows, the
   records are made in it, and the goal that enters them is at its
   head. A third yield with as many winders reallocates nothing. */
static int test_pieces_grow_between_yields(void) {

    PyObject *owner = PyList_New(0);
    PyObject *val = PyLong_FromLong(7);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *winds[2];
    KosDynNode *tops[2];
    KosXferPiece *pieces;
    KosXferGoal *goal;
    KosContext *ctx;
    Py_ssize_t owner_refs;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(val, "create a value to yield");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");
    owner_refs = Py_REFCNT(owner);

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;
    TEST_ASSERT_EQ(
        grow_one_winder_span(ctx, owner, val, &state, winds, tops), 0,
        "a second wind on a span yielded under one");

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield under two");
    TEST_ASSERT_EQ(state.piece_cap, 2, "the allocation grew to two");
    TEST_ASSERT_EQ(state.piece_count, 2, "both winders are recorded");
    TEST_ASSERT_SAME(state.pieces[0].regs.top, winds[0], "outermost first");
    TEST_ASSERT_SAME(state.pieces[0].rest, tops[0],
                     "with the frame resting on it");
    TEST_ASSERT_SAME(state.pieces[1].regs.top, winds[1], "then the inner");
    TEST_ASSERT_SAME(state.pieces[1].rest, tops[1], "under the top frame");
    TEST_ASSERT_EQ(Py_REFCNT(owner), owner_refs, "no goal holds the owner");

    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 1,
                   "the install stops at the outermost winder");
    goal = KosContext_GOAL(ctx);
    TEST_ASSERT_NOT_NULL(goal, "a goal is pushed");
    TEST_ASSERT_SAME((KosXferPiece *) (goal + 1), state.pieces,
                     "at the head of the grown allocation");
    TEST_ASSERT_EQ(kos_xfer_goal_drive(ctx).kind, KOS_RES_NORMAL,
                   "the goal enters both and attaches");
    TEST_ASSERT_NULL(KosContext_GOAL(ctx), "and retires");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), tops[1], "the span is back");
    TEST_ASSERT_SAME(KosDynNode_PARENT(tops[0]), winds[0], "whole");
    for (int i = 0; i < 2; i++)
        TEST_ASSERT_EQ(KosDynWind_ENTERED(winds[i]), 1,
                       "the wind is entered");
    TEST_ASSERT(kos_dyn_skips_hold(tops[1], NULL), "the skip links hold");

    pieces = state.pieces;
    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0,
                   "yield under two again");
    TEST_ASSERT_SAME(state.pieces, pieces, "the grown allocation is kept");
    TEST_ASSERT_EQ(state.piece_cap, 2, "at two");

    kos_gen_state_clear(&state);
    TEST_ASSERT_NULL(state.pieces, "the clear frees the records");
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    Py_DECREF(val);
    return 0;
}


/* The same second yield with the grow refused: MemoryError, the list
   emptied, and the chain, the winders and the allocation as they were.
   A later yield grows the allocation. */
static int test_pieces_grow_refused_changes_nothing(void) {

    PyObject *owner = PyList_New(0);
    PyObject *val = PyLong_FromLong(7);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *winds[2];
    KosDynNode *tops[2];
    KosXferPiece *pieces;
    KosXferGoal *goal;
    KosDynGen *gen;
    KosContext *ctx;
    Py_ssize_t owner_refs;
    int refused;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(val, "create a value to yield");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");
    owner_refs = Py_REFCNT(owner);

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;
    TEST_ASSERT_EQ(
        grow_one_winder_span(ctx, owner, val, &state, winds, tops), 0,
        "a second wind on a span yielded under one");

    gen = kos_dyn_find_gen(ctx);
    TEST_ASSERT_NOT_NULL(gen, "the running frame is a generator's");
    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, gen), 1,
                   "a span with winders is not frozen");
    pieces = state.pieces;

    test_refuse_realloc_begin(0);
    goal = kos_dyn_gen_wind_out_begin(ctx, gen, val);
    refused = test_refuse_realloc_end();

    TEST_ASSERT_NULL(goal, "the records cannot be made");
    TEST_ASSERT_EQ(refused, 1, "the grow was refused");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_MemoryError),
                "with MemoryError");
    PyErr_Clear();
    TEST_ASSERT_SAME(state.pieces, pieces, "the allocation stands");
    TEST_ASSERT_EQ(state.piece_cap, 1, "at one");
    TEST_ASSERT_EQ(state.piece_count, 0, "nothing is recorded");
    TEST_ASSERT_EQ(state.wound_count, 0, "nothing is listed");
    TEST_ASSERT_EQ(state.effects.count, 0, "the list is emptied");
    TEST_ASSERT_NULL(KosContext_GOAL(ctx), "no goal is pushed");
    TEST_ASSERT_EQ(Py_REFCNT(owner), owner_refs, "nothing holds the owner");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), tops[1], "the chain is whole");
    TEST_ASSERT_SAME(KosDynNode_PARENT(tops[0]), winds[0], "and uncut");
    for (int i = 0; i < 2; i++)
        TEST_ASSERT_EQ(KosDynWind_ENTERED(winds[i]), 1, "no wind is left");
    TEST_ASSERT(kos_dyn_skips_hold(tops[1], NULL), "the skip links hold");

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "a later yield works");
    TEST_ASSERT_EQ(state.piece_cap, 2, "and grows the allocation");

    kos_gen_state_clear(&state);
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    Py_DECREF(val);
    return 0;
}


/* === Effects refused at an install === */


/* While set, a RefusingKey's hash raises RuntimeError. A PARAM bound
   by one then refuses its wind: kos_dynparam_enter records the prior
   cell with a dict store, which hashes the key. */
static int refuse_hash = 0;


static Py_hash_t refusing_key_hash(PyObject *self) {
    if (refuse_hash) {
        PyErr_SetString(PyExc_RuntimeError, "refused hash");
        return -1;
    }
    return PyUnicode_Type.tp_hash(self);
}


static PyType_Slot refusing_key_slots[] = {
    {Py_tp_hash, (void *) refusing_key_hash},
    {0, NULL},
};


/* a str subclass, as a PARAM's binding names a str */
static PyType_Spec refusing_key_spec = {
    "test_dynchain.RefusingKey",
    0,
    0,
    Py_TPFLAGS_DEFAULT,
    refusing_key_slots,
};


/* Push a PARAM binding a RefusingKey of type to True. Returns the
   node, or NULL with an error set. */
static KosDynNode *push_refusing_param(KosContext *ctx, PyObject *type) {

    PyObject *key = PyObject_CallFunction(type, "s", "refusing");
    PyObject *bindings = PyDict_New();
    KosDynNode *node = NULL;

    if (key && bindings && PyDict_SetItem(bindings, key, Py_True) == 0)
        node = (KosDynNode *) kos_dyn_push_param_dict(ctx, bindings);
    Py_XDECREF(bindings);
    Py_XDECREF(key);
    return node;
}


/* A span of a root frame, a handling TRY, a PARAM bound by a
   RefusingKey and a frame on top. The freeze lists the PARAM then the
   TRY, innermost first. The plain install puts the TRY back first;
   the PARAM refuses, and the TRY goes out again. */
static int test_install_refused_takes_effects_out_again(void) {

    PyObject *owner = PyList_New(0);
    PyObject *type = PyType_FromSpecWithBases(
        &refusing_key_spec, (PyObject *) &PyUnicode_Type);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *try_;
    KosDynNode *param;
    KosDynNode *top;
    KosDynNode **items;
    KosContext *ctx;
    int r;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(type, "create the refusing key type");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;

    try_ = (KosDynNode *) kos_dyn_push_try(ctx, FAKE_INSN(1));
    TEST_ASSERT_NOT_NULL(try_, "push a try");
    KosDynTry_EXC(try_)->state = KOS_EXC_HANDLING;
    param = push_refusing_param(ctx, type);
    TEST_ASSERT_NOT_NULL(param, "push the param");
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx), "push the top frame");
    top = KosContext_DYN_TOP(ctx);

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, kos_dyn_find_gen(ctx)), 0,
                   "freeze the span");
    items = effects_items(&state.effects);
    TEST_ASSERT_EQ(state.effects.count, 2, "both effects are listed");
    TEST_ASSERT_SAME(items[0], param, "the param innermost");
    TEST_ASSERT_SAME(items[1], try_, "then the try");
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_FROZEN,
                "the try's effect is out");

    refuse_hash = 1;
    r = kos_dyn_gen_install(ctx, owner, &state);
    refuse_hash = 0;

    TEST_ASSERT_EQ(r, -1, "the install refuses");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
                "with the param's error");
    PyErr_Clear();
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_FROZEN,
                "the try's effect is out again");
    TEST_ASSERT_EQ(state.effects.count, 2, "the list stands");
    TEST_ASSERT_SAME(state.active, top, "the span rests in the state");
    TEST_ASSERT_NULL(KosDynNode_PARENT(state.bottom),
                     "off any GEN");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), (KosDynNode *) consumer,
                     "the consumer is the chain's top");

    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 0,
                   "a later install works");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), top, "the span is back");
    TEST_ASSERT_EQ(state.effects.count, 0, "the list is used up");
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_HANDLING,
                "the try's effect is in");
    TEST_ASSERT(kos_dyn_skips_hold(top, NULL), "the skip links hold");

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, kos_dyn_find_gen(ctx)), 0,
                   "freeze to tear down");
    kos_gen_state_clear(&state);
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    Py_DECREF(type);
    return 0;
}


/* A span of a root frame, a handling TRY, a PARAM bound by a
   RefusingKey, a dyn-wind and a frame on top. The yield lists the
   wind, the PARAM and the TRY, innermost first. The install puts back
   what is outside the wind, the TRY first; the PARAM refuses, the TRY
   goes out again, and the span rests with no goal pushed. */
static int test_winding_install_refused_takes_effects_out_again(void) {

    PyObject *owner = PyList_New(0);
    PyObject *val = PyLong_FromLong(7);
    PyObject *type = PyType_FromSpecWithBases(
        &refusing_key_spec, (PyObject *) &PyUnicode_Type);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *try_;
    KosDynNode *param;
    KosDynNode *wind;
    KosDynNode *top;
    KosDynNode **items;
    KosContext *ctx;
    Py_ssize_t owner_refs;
    int r;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(val, "create a value to yield");
    TEST_ASSERT_NOT_NULL(type, "create the refusing key type");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;

    try_ = (KosDynNode *) kos_dyn_push_try(ctx, FAKE_INSN(1));
    TEST_ASSERT_NOT_NULL(try_, "push a try");
    KosDynTry_EXC(try_)->state = KOS_EXC_HANDLING;
    param = push_refusing_param(ctx, type);
    TEST_ASSERT_NOT_NULL(param, "push the param");
    wind = (KosDynNode *) kos_dyn_push_wind(ctx, NULL, &bare_wind_row);
    TEST_ASSERT_NOT_NULL(wind, "push a wind");
    KosDynWind_ENTERED(wind) = 1;
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx), "push the top frame");
    top = KosContext_DYN_TOP(ctx);

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield");
    items = effects_items(&state.effects);
    TEST_ASSERT_EQ(state.wound_count, 3, "the wind and both effects");
    TEST_ASSERT_EQ(state.piece_count, 1, "one winder is recorded");
    TEST_ASSERT_SAME(items[0], wind, "the wind innermost");
    TEST_ASSERT_SAME(items[1], param, "then the param");
    TEST_ASSERT_SAME(items[2], try_, "then the try");
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_FROZEN,
                "the try's effect is out");
    owner_refs = Py_REFCNT(owner);

    refuse_hash = 1;
    r = kos_dyn_gen_install(ctx, owner, &state);
    refuse_hash = 0;

    TEST_ASSERT_EQ(r, -1, "the install refuses");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
                "with the param's error");
    PyErr_Clear();
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_FROZEN,
                "the try's effect is out again");
    TEST_ASSERT_EQ(state.wound_count, 3, "the list stands");
    TEST_ASSERT_EQ(state.piece_count, 1, "and the record");
    TEST_ASSERT_NULL(KosContext_GOAL(ctx), "no goal is pushed");
    TEST_ASSERT_EQ(Py_REFCNT(owner), owner_refs,
                   "the freed goal let go of the owner");
    TEST_ASSERT_SAME(state.active, top, "the span rests in the state");
    TEST_ASSERT_NULL(KosDynNode_PARENT(state.bottom), "off any GEN");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), (KosDynNode *) consumer,
                     "the consumer is the chain's top");

    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 1,
                   "a later install stops at the winder");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), wind, "which is the top");
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_HANDLING,
                "the try's effect is in");
    TEST_ASSERT_EQ(kos_xfer_goal_drive(ctx).kind, KOS_RES_NORMAL,
                   "the goal enters and attaches");
    TEST_ASSERT_NULL(KosContext_GOAL(ctx), "and retires");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), top, "the span is back");
    TEST_ASSERT_EQ(KosDynWind_ENTERED(wind), 1, "the wind is entered");
    TEST_ASSERT(kos_dyn_skips_hold(top, NULL), "the skip links hold");

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield to tear down");
    kos_gen_state_clear(&state);
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    Py_DECREF(val);
    Py_DECREF(type);
    return 0;
}


/* A span of a root frame, a dyn-wind, a handling TRY, a PARAM bound by
   a RefusingKey, a second dyn-wind and a frame on top. The yield lists
   the inner wind, the PARAM, the TRY and the outer wind. The install
   stops at the outer wind; once it is entered, the attach puts back
   what lies between the two winds, the TRY first. The PARAM refuses:
   the TRY goes out again and nothing is attached. The goal given up by
   the raise frees what it held. */
static int test_winding_attach_refused_takes_effects_out_again(void) {

    PyObject *owner = PyList_New(0);
    PyObject *val = PyLong_FromLong(7);
    PyObject *type = PyType_FromSpecWithBases(
        &refusing_key_spec, (PyObject *) &PyUnicode_Type);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *winds[2];
    KosDynNode *try_;
    KosDynNode *param;
    KosDynNode **items;
    KosContext *ctx;
    KosOpResult r;
    Py_ssize_t owner_refs;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(val, "create a value to yield");
    TEST_ASSERT_NOT_NULL(type, "create the refusing key type");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;

    winds[0] = (KosDynNode *) kos_dyn_push_wind(ctx, NULL, &bare_wind_row);
    TEST_ASSERT_NOT_NULL(winds[0], "push the outer wind");
    KosDynWind_ENTERED(winds[0]) = 1;
    try_ = (KosDynNode *) kos_dyn_push_try(ctx, FAKE_INSN(1));
    TEST_ASSERT_NOT_NULL(try_, "push a try");
    KosDynTry_EXC(try_)->state = KOS_EXC_HANDLING;
    param = push_refusing_param(ctx, type);
    TEST_ASSERT_NOT_NULL(param, "push the param");
    winds[1] = (KosDynNode *) kos_dyn_push_wind(ctx, NULL, &bare_wind_row);
    TEST_ASSERT_NOT_NULL(winds[1], "push the inner wind");
    KosDynWind_ENTERED(winds[1]) = 1;
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx), "push the top frame");

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield");
    items = effects_items(&state.effects);
    TEST_ASSERT_EQ(state.wound_count, 4, "both winds and both effects");
    TEST_ASSERT_EQ(state.piece_count, 2, "two winders are recorded");
    TEST_ASSERT_SAME(items[0], winds[1], "the inner wind innermost");
    TEST_ASSERT_SAME(items[1], param, "then the param");
    TEST_ASSERT_SAME(items[2], try_, "then the try");
    TEST_ASSERT_SAME(items[3], winds[0], "then the outer wind");
    owner_refs = Py_REFCNT(owner);

    TEST_ASSERT_EQ(kos_dyn_gen_install(ctx, owner, &state), 1,
                   "the install stops at the outer wind");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), winds[0], "which is the top");
    TEST_ASSERT_NULL(KosDynNode_PARENT(try_), "what rests on it is cut off");

    refuse_hash = 1;
    r = kos_xfer_goal_drive(ctx);
    refuse_hash = 0;

    TEST_ASSERT_EQ(r.kind, KOS_RES_RAISE, "the attach refuses");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
                "with the param's error");
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_FROZEN,
                "the try's effect is out again");
    TEST_ASSERT_NULL(KosDynNode_PARENT(try_), "nothing is attached");
    TEST_ASSERT_SAME(KosContext_DYN_TOP(ctx), winds[0],
                     "the outer wind is the top");
    TEST_ASSERT_EQ(KosDynWind_ENTERED(winds[0]), 1, "and is entered");
    TEST_ASSERT_NOT_NULL(KosContext_GOAL(ctx), "the goal is still pushed");

    /* the raise ingress gives the goal up with the error in hand */
    kos_xfer_goal_abandon_stepping(ctx, 1);
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_RuntimeError),
                "the error stands");
    PyErr_Clear();
    TEST_ASSERT_NULL(KosContext_GOAL(ctx), "the goal is given up");
    TEST_ASSERT_EQ(state.piece_count, 0, "with the records");
    TEST_ASSERT_EQ(state.wound_count, 0, "and the list");
    TEST_ASSERT_EQ(Py_REFCNT(owner), owner_refs, "and the owner");
    TEST_ASSERT(kos_dyn_skips_hold(winds[0], NULL), "the skip links hold");

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield to tear down");
    kos_gen_state_clear(&state);
    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    Py_DECREF(val);
    Py_DECREF(type);
    return 0;
}


/* === Clearing a resting span === */


/* Push a TRY handling exc, which displaced nothing: exc is the
   thread's handled exception, as the TRY's consume of a raise leaves
   it. Returns the node, or NULL. */
static KosDynNode *push_handling_try(KosContext *ctx, PyObject *exc) {

    KosDynNode *node = (KosDynNode *) kos_dyn_push_try(ctx, FAKE_INSN(1));

    if (! node)
        return NULL;
    KosDynTry_EXC(node)->state = KOS_EXC_HANDLING;
    PyErr_SetExcInfo(Py_NewRef((PyObject *) Py_TYPE(exc)),
                     Py_NewRef(exc), NULL);
    return node;
}


/* Push a PARAM binding name to value. Returns the node, or NULL with
   an error set. */
static KosDynNode *push_bound_param(
    KosContext *ctx,
    PyObject *name,
    PyObject *value) {

    PyObject *bindings = PyDict_New();
    KosDynNode *node = NULL;

    if (bindings && PyDict_SetItem(bindings, name, value) == 0)
        node = (KosDynNode *) kos_dyn_push_param_dict(ctx, bindings);
    Py_XDECREF(bindings);
    return node;
}


/* A span of a root frame, a TRY handling an exception, a PARAM and a
   frame on top, frozen with both effects out, then cleared from its
   state: the exception the TRY holds off the thread is released once,
   and the PARAM's cell lets go of its value. */
static int test_state_clear_releases_resting_effects(void) {

    PyObject *owner = PyList_New(0);
    PyObject *exc = PyObject_CallFunction(PyExc_ValueError, "s", "held");
    PyObject *name = PyUnicode_FromString("bound");
    PyObject *value = PyList_New(0);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *try_;
    KosContext *ctx;
    Py_ssize_t exc_refs;
    Py_ssize_t value_refs;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(exc, "create the handled exception");
    TEST_ASSERT(name && value, "create the binding");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");
    exc_refs = Py_REFCNT(exc);
    value_refs = Py_REFCNT(value);

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;

    try_ = push_handling_try(ctx, exc);
    TEST_ASSERT_NOT_NULL(try_, "push a try handling the exception");
    TEST_ASSERT_NOT_NULL(push_bound_param(ctx, name, value),
                         "push a param");
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx), "push the top frame");

    TEST_ASSERT_EQ(kos_dyn_gen_freeze(ctx, kos_dyn_find_gen(ctx)), 0,
                   "freeze the span");
    TEST_ASSERT_EQ(state.effects.count, 2, "both effects are listed");
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_FROZEN,
                "the try's effect is out");
    TEST_ASSERT_SAME(KosDynTry_EXC(try_)->saved.value, exc,
                     "holding its exception");
    TEST_ASSERT_EQ(Py_REFCNT(exc), exc_refs + 1,
                   "by the reference the thread gave up");
    TEST_ASSERT_NULL(PyDict_GetItem(KosContext_PARAM_REFS(ctx), name),
                     "the param's effect is out");
    TEST_ASSERT_EQ(Py_REFCNT(value), value_refs + 1,
                   "its cell holds the value");

    kos_gen_state_clear(&state);
    TEST_ASSERT_EQ(Py_REFCNT(exc), exc_refs,
                   "the clear releases the exception once");
    TEST_ASSERT_EQ(Py_REFCNT(value), value_refs,
                   "and the param's value");

    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    Py_DECREF(exc);
    Py_DECREF(name);
    Py_DECREF(value);
    return 0;
}


/* A span of a root frame, a TRY handling an exception, a PARAM, a
   dyn-wind and a frame on top, rested by a yield that left the wind.
   The span rests whole, so the clear reaches the nodes under the wind
   and releases what their effects hold, and frees the list's array. */
static int test_state_clear_releases_wound_span(void) {

    PyObject *owner = PyList_New(0);
    PyObject *val = PyLong_FromLong(7);
    PyObject *exc = PyObject_CallFunction(PyExc_ValueError, "s", "held");
    PyObject *name = PyUnicode_FromString("bound");
    PyObject *value = PyList_New(0);
    PyObject *ns = PyDict_New();
    PyObject *gen_ns = PyDict_New();
    KosGenState state;
    KosDynFrame *consumer;
    KosDynFrame *root;
    KosDynNode *try_;
    KosDynNode *wind;
    KosContext *ctx;
    Py_ssize_t exc_refs;
    Py_ssize_t value_refs;

    TEST_ASSERT_NOT_NULL(owner, "create a stand-in owner");
    TEST_ASSERT_NOT_NULL(val, "create a value to yield");
    TEST_ASSERT_NOT_NULL(exc, "create the handled exception");
    TEST_ASSERT(name && value, "create the binding");
    TEST_ASSERT(ns && gen_ns, "create the namespaces");
    consumer = kos_dynframe_new(NULL, NULL, ns);
    root = kos_dynframe_new(NULL, NULL, gen_ns);
    TEST_ASSERT(consumer && root, "create the frames");
    ctx = KosContext_New(consumer, kos_tls_current());
    TEST_ASSERT_NOT_NULL(ctx, "create the consumer's context");
    exc_refs = Py_REFCNT(exc);
    value_refs = Py_REFCNT(value);

    KosContext_EvalEnter(ctx, NULL);
    kos_gen_state_init(&state, root);
    TEST_ASSERT_EQ(kos_dyn_gen_install_bootstrap(
                       ctx, owner, &state, (KosDynNode *) root), 0,
                   "install the generator's frame");
    state.active = NULL;

    try_ = push_handling_try(ctx, exc);
    TEST_ASSERT_NOT_NULL(try_, "push a try handling the exception");
    TEST_ASSERT_NOT_NULL(push_bound_param(ctx, name, value),
                         "push a param");
    wind = (KosDynNode *) kos_dyn_push_wind(ctx, NULL, &bare_wind_row);
    TEST_ASSERT_NOT_NULL(wind, "push a wind");
    KosDynWind_ENTERED(wind) = 1;
    TEST_ASSERT_NOT_NULL(kos_dyn_push_frame_empty(ctx), "push the top frame");

    TEST_ASSERT_EQ(wind_out_and_freeze(ctx, val), 0, "yield");
    TEST_ASSERT_EQ(state.wound_count, 3, "the wind and both effects");
    TEST_ASSERT_EQ(state.effects.cap, 3, "listed on the heap");
    TEST_ASSERT(KosDynTry_EXC(try_)->state == KOS_EXC_FROZEN,
                "the try's effect is out");
    TEST_ASSERT_EQ(Py_REFCNT(exc), exc_refs + 1,
                   "holding its exception off the thread");
    TEST_ASSERT_NULL(PyDict_GetItem(KosContext_PARAM_REFS(ctx), name),
                     "the param's effect is out");

    kos_gen_state_clear(&state);
    TEST_ASSERT_EQ(Py_REFCNT(exc), exc_refs,
                   "the clear releases the exception once");
    TEST_ASSERT_EQ(Py_REFCNT(value), value_refs,
                   "and the param's value");
    TEST_ASSERT_EQ(state.effects.cap, 0, "and frees the array");

    KosContext_EvalExit(ctx);
    Py_DECREF(ctx);
    Py_DECREF(owner);
    Py_DECREF(val);
    Py_DECREF(exc);
    Py_DECREF(name);
    Py_DECREF(value);
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

    /* Traverse return propagation */
    RUN_TEST(test_traverse_with_saved_returns_visit_result);
    RUN_TEST(test_traverse_finally_saved_returns_visit_result);
    RUN_TEST(test_traverse_context_exc_returns_visit_result);

    /* Capability and skip link tests */
    RUN_TEST(test_node_header_size);
    RUN_TEST(test_caps_by_kind);
    RUN_TEST(test_skip_links_at_init);
    RUN_TEST(test_clone_skip_links_bit_at_top);
    RUN_TEST(test_clone_skip_links_bit_in_middle);
    RUN_TEST(test_clone_skip_links_bit_at_bottom);
    RUN_TEST(test_clone_skip_links_no_bit);
    RUN_TEST(test_node_sizes_by_kind);
    RUN_TEST(test_node_sizes_do_not_mix);
    RUN_TEST(test_frame_caps_survive_cache_reset);

    /* Effect list tests */
    RUN_TEST(test_freeze_install_no_effects);
    RUN_TEST(test_freeze_install_one_effect);
    RUN_TEST(test_freeze_install_two_effects);
    RUN_TEST(test_freeze_install_three_effects);
    RUN_TEST(test_freeze_refused_changes_nothing);
    RUN_TEST(test_freeze_effects_grow_between_yields);
    RUN_TEST(test_freeze_regrow_refused_changes_nothing);

    /* Winding across a yield */
    RUN_TEST(test_wind_out_and_in);
    RUN_TEST(test_wind_goal_outlives_the_state_clear);
    RUN_TEST(test_pieces_grow_between_yields);
    RUN_TEST(test_pieces_grow_refused_changes_nothing);

    /* Effects refused at an install */
    RUN_TEST(test_install_refused_takes_effects_out_again);
    RUN_TEST(test_winding_install_refused_takes_effects_out_again);
    RUN_TEST(test_winding_attach_refused_takes_effects_out_again);

    /* Clearing a resting span */
    RUN_TEST(test_state_clear_releases_resting_effects);
    RUN_TEST(test_state_clear_releases_wound_span);

    return 0;
}


// The end.
