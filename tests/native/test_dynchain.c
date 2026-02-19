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
    /* Test frame cloning with COW stack */
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
    KosDynWind *wind = kos_dynwind_new(NULL, NULL, 10, NULL, 20);
    TEST_ASSERT_NOT_NULL(wind, "create wind");

    TEST_ASSERT_EQ(KosDynNode_KIND(wind), KOS_DYN_WIND, "wind kind is WIND");
    TEST_ASSERT_EQ(KosDynWind_BEFORE_PC(wind), 10, "wind before_pc is set");
    TEST_ASSERT_EQ(KosDynWind_AFTER_PC(wind), 20, "wind after_pc is set");
    TEST_ASSERT_EQ(KosDynWind_ENTERED(wind), 0, "wind entered is 0");

    kos_dynwind_free(wind);

    return 0;
}


static int test_dynwind_clone(void) {
    /* Test wind node cloning */
    KosDynWind *wind = kos_dynwind_new(NULL, NULL, 10, NULL, 20);
    TEST_ASSERT_NOT_NULL(wind, "create wind");

    KosDynWind_ENTERED(wind) = 1;

    KosDynWind *clone = kos_dynwind_clone(wind);
    TEST_ASSERT_NOT_NULL(clone, "clone wind");

    TEST_ASSERT_NOT_SAME(wind, clone, "clone is different object");
    TEST_ASSERT_EQ(KosDynWind_ENTERED(clone), 1, "clone has same entered state");
    TEST_ASSERT(KosDynNode_FLAGS(clone) & KOS_DYNF_CLONED, "clone has CLONED flag");

    kos_dynwind_free(clone);
    kos_dynwind_free(wind);

    return 0;
}


/* === DynRegion tests === */


static int test_dynregion_new_free(void) {
    /* Test region node allocation and freeing */
    KosDynRegion *region = kos_dynregion_new(NULL, KOS_REGION_TRY, 100, 200);
    TEST_ASSERT_NOT_NULL(region, "create region");

    TEST_ASSERT_EQ(KosDynNode_KIND(region), KOS_DYN_REGION, "region kind is REGION");
    TEST_ASSERT_EQ(KosDynRegion_STYLE(region), KOS_REGION_TRY, "region style is TRY");
    TEST_ASSERT_EQ(KosDynRegion_BREAK_PC(region), 100, "region break_pc is set");
    TEST_ASSERT_EQ(KosDynRegion_CONTINUE_PC(region), 200, "region continue_pc is set");
    TEST_ASSERT_EQ(KosDynRegion_ENABLED(region), 1, "region enabled by default");

    kos_dynregion_free(region);

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

    /* Region tests */
    RUN_TEST(test_dynregion_new_free);

    /* Chain operation tests */
    RUN_TEST(test_chain_free);
    RUN_TEST(test_chain_clone_segment);

    /* LCA tests */
    RUN_TEST(test_compute_lca);

    /* Continuation tests */
    RUN_TEST(test_continuation_free_null);

    /* Transfer plan tests */
    RUN_TEST(test_transfer_plan_free_null);

    return 0;
}


// The end.
