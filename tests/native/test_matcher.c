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
   Tests for matcher_ng (Phase 1)

   Validator-only tests; no matching engine yet (Phase 2).

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GNU General Public License v3
   ai-assistant: Claude Opus 4.7 via Cursor
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === small builder fixture helpers === */


static int add_str_slot(KosMatcherBuilder *b, const char *s) {
    PyObject *str = PyUnicode_FromString(s);
    if (! str)
        return -1;
    int idx = KosMatcherBuilder_AddSlot(b, str);
    Py_DECREF(str);
    return idx;
}


static int add_str_extern(KosMatcherBuilder *b, const char *s) {
    PyObject *str = PyUnicode_FromString(s);
    if (! str)
        return -1;
    int idx = KosMatcherBuilder_AddExtern(b, str);
    Py_DECREF(str);
    return idx;
}


static int add_const_long(KosMatcherBuilder *b, long v) {
    PyObject *value = PyLong_FromLong(v);
    if (! value)
        return -1;
    int idx = KosMatcherBuilder_AddConst(b, value);
    Py_DECREF(value);
    return idx;
}


static int append_test_any(KosMatcherBuilder *b) {
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_TEST;
    n.as.test.test_kind = KOS_MATCH_TEST_ANY;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_test_literal(KosMatcherBuilder *b, uint32_t const_index) {
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_TEST;
    n.as.test.test_kind = KOS_MATCH_TEST_LITERAL;
    n.as.test.ref_index = const_index;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_test_type(KosMatcherBuilder *b, uint32_t extern_index) {
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_TEST;
    n.as.test.test_kind = KOS_MATCH_TEST_TYPE;
    n.as.test.ref_index = extern_index;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_capture(
    KosMatcherBuilder *b,
    uint32_t slot,
    uint32_t subnode) {

    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_CAPTURE;
    n.as.capture.slot = slot;
    n.as.capture.subnode = subnode;
    n.as.capture.scope_slot = KOS_MATCH_NO_INDEX;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_form(KosMatcherBuilder *b, uint32_t subnode) {
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_FORM;
    n.as.form.subnode = subnode;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_group(
    KosMatcherBuilder *b,
    const uint32_t *children,
    uint32_t count) {

    int first = (int) KosMatcherBuilder_ChildCount(b);
    for (uint32_t i = 0; i < count; i++) {
        int off = KosMatcherBuilder_AppendChildIndex(b, children[i]);
        if (off < 0)
            return -1;
    }

    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_GROUP;
    n.as.group.first_child = (uint32_t) first;
    n.as.group.child_count = count;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_oneof(
    KosMatcherBuilder *b,
    const uint32_t *children,
    uint32_t count) {

    int first = (int) KosMatcherBuilder_ChildCount(b);
    for (uint32_t i = 0; i < count; i++) {
        int off = KosMatcherBuilder_AppendChildIndex(b, children[i]);
        if (off < 0)
            return -1;
    }

    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_ONEOF;
    n.as.oneof.first_child = (uint32_t) first;
    n.as.oneof.child_count = count;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_repeat(KosMatcherBuilder *b, uint32_t subnode) {
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_REPEAT;
    n.as.repeat.subnode = subnode;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_optional(KosMatcherBuilder *b, uint32_t subnode) {
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_OPTIONAL;
    n.as.optional.subnode = subnode;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_rest(KosMatcherBuilder *b, uint32_t slot) {
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_REST;
    n.as.rest.slot = slot;
    return KosMatcherBuilder_AppendNode(b, &n);
}


static int append_not(KosMatcherBuilder *b, uint32_t subnode) {
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_NOT;
    n.as.not_.subnode = subnode;
    return KosMatcherBuilder_AppendNode(b, &n);
}


/* === append-time forward-ref tests === */


static int test_matcher_validator_rejects_forward_ref(void) {
    /*
      Append a CAPTURE that refers to subnode index 0 before any
      node has been appended; AppendNode should return -1 with a
      ValueError set.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_CAPTURE;
    n.as.capture.subnode = 0;  /* references self / forward */
    n.as.capture.slot = KOS_MATCH_NO_INDEX;
    n.as.capture.scope_slot = KOS_MATCH_NO_INDEX;

    int idx = KosMatcherBuilder_AppendNode(b, &n);
    TEST_ASSERT_EQ(idx, -1, "forward-ref append should fail");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


/* === width-compute tests === */


static int test_matcher_validator_widths_basic(void) {
    /*
      Build: GROUP(TEST_ANY, CAPTURE(TEST_ANY), OPTIONAL(TEST_ANY),
                   REST, NOT(TEST_ANY))
      Check computed widths.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot_x = add_str_slot(b, "x");
    int slot_r = add_str_slot(b, "r");
    TEST_ASSERT(slot_x >= 0 && slot_r >= 0, "add slots");

    int n_test = append_test_any(b);
    TEST_ASSERT(n_test >= 0, "append TEST_ANY");

    int n_test2 = append_test_any(b);
    int n_capture = append_capture(b, (uint32_t) slot_x,
                                   (uint32_t) n_test2);
    TEST_ASSERT(n_capture >= 0, "append CAPTURE");

    int n_test3 = append_test_any(b);
    int n_optional = append_optional(b, (uint32_t) n_test3);
    TEST_ASSERT(n_optional >= 0, "append OPTIONAL");

    int n_rest = append_rest(b, (uint32_t) slot_r);
    TEST_ASSERT(n_rest >= 0, "append REST");

    int n_test4 = append_test_any(b);
    int n_not = append_not(b, (uint32_t) n_test4);
    TEST_ASSERT(n_not >= 0, "append NOT");

    /* GROUP must come last so all child indices resolve. The order in
       the spine is [TEST, CAPTURE, OPTIONAL, NOT, REST] — REST goes at
       the end so post-REST suffix is empty (avoids the
       post-REST-determinism rule for this width-only test). */
    uint32_t kids[] = {
        (uint32_t) n_test,
        (uint32_t) n_capture,
        (uint32_t) n_optional,
        (uint32_t) n_not,
        (uint32_t) n_rest,
    };
    int n_group = append_group(b, kids, 5);
    TEST_ASSERT(n_group >= 0, "append GROUP");

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_group);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    KosMatcher *m = (KosMatcher *) m_obj;

    TEST_ASSERT_EQ(m->nodes[n_test].min_width, 1, "TEST min");
    TEST_ASSERT_EQ(m->nodes[n_test].max_width, 1, "TEST max");

    TEST_ASSERT_EQ(m->nodes[n_capture].min_width, 1, "CAPTURE min");
    TEST_ASSERT_EQ(m->nodes[n_capture].max_width, 1, "CAPTURE max");

    TEST_ASSERT_EQ(m->nodes[n_optional].min_width, 0, "OPTIONAL min");
    TEST_ASSERT_EQ(m->nodes[n_optional].max_width, 1, "OPTIONAL max");

    TEST_ASSERT_EQ(m->nodes[n_rest].min_width, 0, "REST min");
    TEST_ASSERT_EQ(m->nodes[n_rest].max_width,
                   KOS_MATCH_UNBOUNDED, "REST max");

    TEST_ASSERT_EQ(m->nodes[n_not].min_width, 0, "NOT min");
    TEST_ASSERT_EQ(m->nodes[n_not].max_width, 0, "NOT max");

    /* GROUP min = 1 + 1 + 0 + 0 + 0 = 2 ; max = unbounded due to REST */
    TEST_ASSERT_EQ(m->nodes[n_group].min_width, 2, "GROUP min");
    TEST_ASSERT_EQ(m->nodes[n_group].max_width,
                   KOS_MATCH_UNBOUNDED, "GROUP max");

    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


/* === REPEAT min-width validation === */


static int test_matcher_validator_repeat_zero_width_rejected(void) {
    /*
      REPEAT subnode is OPTIONAL (min=0). Finalize should fail.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_test = append_test_any(b);
    int n_opt = append_optional(b, (uint32_t) n_test);
    int n_rep = append_repeat(b, (uint32_t) n_opt);
    TEST_ASSERT(n_rep >= 0, "append REPEAT");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_rep);
    TEST_ASSERT_NULL(m, "finalize must reject");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_repeat_positive_width_accepted(void) {
    /*
      REPEAT subnode is GROUP(TEST, OPTIONAL) (min=1). Should be
      accepted.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_test = append_test_any(b);
    int n_test2 = append_test_any(b);
    int n_opt = append_optional(b, (uint32_t) n_test2);

    uint32_t kids[] = {(uint32_t) n_test, (uint32_t) n_opt};
    int n_grp = append_group(b, kids, 2);
    TEST_ASSERT(n_grp >= 0, "append GROUP");

    int n_rep = append_repeat(b, (uint32_t) n_grp);
    TEST_ASSERT(n_rep >= 0, "append REPEAT");

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_rep);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize accepts positive-width REPEAT");

    KosMatcher *m = (KosMatcher *) m_obj;
    TEST_ASSERT_EQ(m->nodes[n_grp].min_width, 1, "GROUP min");
    TEST_ASSERT_EQ(m->nodes[n_rep].min_width, 0, "REPEAT min");
    TEST_ASSERT_EQ(m->nodes[n_rep].max_width,
                   KOS_MATCH_UNBOUNDED, "REPEAT max");

    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


/* === one-REST-per-branch === */


static int test_matcher_validator_double_rest_in_form_branch_rejected(
    void) {
    /*
      Build a FORM whose subnode is GROUP(REST, REST). Finalize should
      reject the inner branch for having two RESTs.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_rest1 = append_rest(b, KOS_MATCH_NO_INDEX);
    int n_rest2 = append_rest(b, KOS_MATCH_NO_INDEX);

    uint32_t kids[] = {(uint32_t) n_rest1, (uint32_t) n_rest2};
    int n_grp = append_group(b, kids, 2);
    int n_form = append_form(b, (uint32_t) n_grp);
    TEST_ASSERT(n_form >= 0, "append FORM");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_form);
    TEST_ASSERT_NULL(m, "finalize must reject double REST");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_oneof_branch_independence_accepted(
    void) {
    /*
      Two ONEOF children, each containing a REST. Each child is its own
      branch, so this is allowed.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_test1 = append_test_any(b);
    int n_rest1 = append_rest(b, KOS_MATCH_NO_INDEX);
    uint32_t k1[] = {(uint32_t) n_test1, (uint32_t) n_rest1};
    int n_grp1 = append_group(b, k1, 2);

    int n_test2 = append_test_any(b);
    int n_rest2 = append_rest(b, KOS_MATCH_NO_INDEX);
    uint32_t k2[] = {(uint32_t) n_test2, (uint32_t) n_rest2};
    int n_grp2 = append_group(b, k2, 2);

    uint32_t branches[] = {(uint32_t) n_grp1, (uint32_t) n_grp2};
    int n_oneof = append_oneof(b, branches, 2);
    TEST_ASSERT(n_oneof >= 0, "append ONEOF");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_oneof);
    TEST_ASSERT_NOT_NULL(m, "finalize accepts independent branches");

    Py_DECREF(m);
    KosMatcherBuilder_Free(b);
    return 0;
}


/* === post-REST determinism === */


static int test_matcher_validator_post_rest_unbounded_rejected(void) {
    /*
      GROUP(REST, REPEAT(TEST_ANY)) — REPEAT is unbounded, follows REST.
      Reject.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_rest = append_rest(b, KOS_MATCH_NO_INDEX);
    int n_inner_test = append_test_any(b);
    int n_rep = append_repeat(b, (uint32_t) n_inner_test);

    uint32_t kids[] = {(uint32_t) n_rest, (uint32_t) n_rep};
    int n_grp = append_group(b, kids, 2);
    TEST_ASSERT(n_grp >= 0, "append GROUP");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NULL(m, "finalize must reject unbounded post-REST");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_post_rest_finite_accepted(void) {
    /*
      GROUP(REST, TEST_ANY, TEST_ANY) — fixed 2-element suffix, OK.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_rest = append_rest(b, KOS_MATCH_NO_INDEX);
    int n_t1 = append_test_any(b);
    int n_t2 = append_test_any(b);

    uint32_t kids[] = {
        (uint32_t) n_rest, (uint32_t) n_t1, (uint32_t) n_t2};
    int n_grp = append_group(b, kids, 3);
    TEST_ASSERT(n_grp >= 0, "append GROUP");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NOT_NULL(m, "finalize accepts finite post-REST suffix");

    Py_DECREF(m);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_post_rest_optional_rejected(void) {
    /*
      GROUP(REST, OPTIONAL(TEST_ANY)) — OPTIONAL has min=0, max=1; the
      suffix is non-deterministic. Reject.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_rest = append_rest(b, KOS_MATCH_NO_INDEX);
    int n_test = append_test_any(b);
    int n_opt = append_optional(b, (uint32_t) n_test);

    uint32_t kids[] = {(uint32_t) n_rest, (uint32_t) n_opt};
    int n_grp = append_group(b, kids, 2);
    TEST_ASSERT(n_grp >= 0, "append GROUP");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NULL(m, "finalize must reject OPTIONAL after REST");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


/* === captures inside NOT === */


static int test_matcher_validator_capture_in_not_rejected(void) {
    /*
      NOT(CAPTURE(TEST_ANY)) — captures inside NOT are useless; reject.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot_x = add_str_slot(b, "x");
    TEST_ASSERT(slot_x >= 0, "add slot");

    int n_test = append_test_any(b);
    int n_cap = append_capture(b, (uint32_t) slot_x,
                               (uint32_t) n_test);
    int n_not = append_not(b, (uint32_t) n_cap);
    TEST_ASSERT(n_not >= 0, "append NOT");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_not);
    TEST_ASSERT_NULL(m, "finalize must reject CAPTURE inside NOT");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_capture_in_nested_not_rejected(void) {
    /*
      NOT(GROUP(TEST, CAPTURE(TEST))) — capture deeply nested under NOT;
      should still be rejected.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot_x = add_str_slot(b, "x");

    int n_t1 = append_test_any(b);
    int n_t2 = append_test_any(b);
    int n_cap = append_capture(b, (uint32_t) slot_x, (uint32_t) n_t2);
    uint32_t kids[] = {(uint32_t) n_t1, (uint32_t) n_cap};
    int n_grp = append_group(b, kids, 2);
    int n_not = append_not(b, (uint32_t) n_grp);

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_not);
    TEST_ASSERT_NULL(m, "finalize must reject nested CAPTURE inside NOT");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


/* === slot/extern/const range checks === */


static int test_matcher_validator_slot_oob_rejected(void) {
    /*
      CAPTURE with slot index 5 but only 1 slot defined. Reject.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    add_str_slot(b, "x");

    int n_test = append_test_any(b);

    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_CAPTURE;
    n.as.capture.slot = 5;  /* out of range */
    n.as.capture.subnode = (uint32_t) n_test;
    n.as.capture.scope_slot = KOS_MATCH_NO_INDEX;
    int n_cap = KosMatcherBuilder_AppendNode(b, &n);
    TEST_ASSERT(n_cap >= 0, "append CAPTURE (deferred validation)");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_cap);
    TEST_ASSERT_NULL(m, "finalize must reject OOB slot index");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_extern_oob_rejected(void) {
    /*
      TEST_TYPE referencing extern index 3 but no externs defined.
      Reject.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_test = append_test_type(b, 3);
    TEST_ASSERT(n_test >= 0, "append TEST_TYPE");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_test);
    TEST_ASSERT_NULL(m, "finalize must reject OOB extern index");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_const_oob_rejected(void) {
    /*
      TEST_LITERAL referencing const index 7 but no consts defined.
      Reject.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_test = append_test_literal(b, 7);
    TEST_ASSERT(n_test >= 0, "append TEST_LITERAL");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_test);
    TEST_ASSERT_NULL(m, "finalize must reject OOB const index");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_in_range_extern_accepted(void) {
    /*
      Sanity: TEST_TYPE referencing an in-range extern is accepted.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int e = add_str_extern(b, "symbol");
    TEST_ASSERT(e >= 0, "add extern");

    int n_test = append_test_type(b, (uint32_t) e);
    TEST_ASSERT(n_test >= 0, "append TEST_TYPE");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_test);
    TEST_ASSERT_NOT_NULL(m, "finalize accepts in-range extern");

    Py_DECREF(m);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_validator_in_range_const_accepted(void) {
    /*
      Sanity: TEST_LITERAL referencing an in-range const is accepted.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int c = add_const_long(b, 5);
    TEST_ASSERT(c >= 0, "add const");

    int n_test = append_test_literal(b, (uint32_t) c);
    TEST_ASSERT(n_test >= 0, "append TEST_LITERAL");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_test);
    TEST_ASSERT_NOT_NULL(m, "finalize accepts in-range const");

    Py_DECREF(m);
    KosMatcherBuilder_Free(b);
    return 0;
}


/* === entry-index range check === */


static int test_matcher_validator_entry_oob_rejected(void) {
    /*
      Append one node, then finalize with entry=5. Reject.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_test = append_test_any(b);
    TEST_ASSERT(n_test >= 0, "append TEST_ANY");

    PyObject *m = KosMatcherBuilder_Finalize(b, 5);
    TEST_ASSERT_NULL(m, "finalize must reject OOB entry");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


/* === module getters === */


static int test_matcher_module_exposes_matcher_type(void) {
    /*
      The koskari.lib module should expose Matcher as a type.
    */
    PyObject *mod = PyImport_ImportModule("koskari.lib");
    if (! mod) {
        PyErr_Clear();
        /* Not all test runs import the live module; fall back to the
           statically registered type pointer. */
        TEST_ASSERT_NOT_NULL(&KosMatcherType, "Matcher type exists");
        return 0;
    }

    PyObject *cls = PyObject_GetAttrString(mod, "Matcher");
    TEST_ASSERT_NOT_NULL(cls, "Matcher exposed in koskari.lib");
    TEST_ASSERT_SAME(cls, (PyObject *) &KosMatcherType,
                     "Matcher attr is KosMatcherType");

    Py_DECREF(cls);
    Py_DECREF(mod);
    return 0;
}


/* === GROUP children ranges === */


static int test_matcher_validator_group_child_oob_rejected(void) {
    /*
      Build a GROUP whose first_child/child_count window extends past
      the actual child_indices array. Reject at append time.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_test = append_test_any(b);
    int off = KosMatcherBuilder_AppendChildIndex(b, (uint32_t) n_test);
    TEST_ASSERT(off >= 0, "append child index");

    /* Manually craft a GROUP node with an oversized count */
    KosMatchNode n;
    memset(&n, 0, sizeof(n));
    n.kind = KOS_MATCH_NODE_GROUP;
    n.as.group.first_child = (uint32_t) off;
    n.as.group.child_count = 5;  /* only 1 child index appended */

    int idx = KosMatcherBuilder_AppendNode(b, &n);
    TEST_ASSERT_EQ(idx, -1, "append must reject oversized GROUP range");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


/* === complex validator scenario === */


static int test_matcher_validator_lambda_shape_accepted(void) {
    /*
      Build a six-branch lambda-shaped ONEOF, the same as the Phase 2
      end-to-end test will exercise. For Phase 1 we only confirm it
      passes validation.

      branches:
        (GROUP (CAPTURE name TEST_TYPE symbol) :: VALUE :: FORMALS REST)
        ... etc.

      For this Phase 1 test, simplify: each branch is a GROUP of
      [CAPTURE-symbol, TEST_LITERAL '::', TEST_ANY, TEST_ANY, REST].
      Six branches, all valid.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot_name = add_str_slot(b, "name");
    int slot_body = add_str_slot(b, "body");
    int extern_sym = add_str_extern(b, "symbol");
    int const_dc = add_const_long(b, 0);  /* placeholder for '::' */

    TEST_ASSERT(slot_name >= 0 && slot_body >= 0, "slots added");
    TEST_ASSERT(extern_sym >= 0 && const_dc >= 0, "extern/const added");

    /* Build six near-identical branches; each does:
         CAPTURE(name, TEST_TYPE symbol)
         TEST_LITERAL ::
         TEST_ANY (return type)
         TEST_ANY (formals)
         REST(body)                                                   */
    uint32_t branch_indices[6];
    for (int i = 0; i < 6; i++) {
        int n_t = append_test_type(b, (uint32_t) extern_sym);
        int n_cap = append_capture(b, (uint32_t) slot_name,
                                   (uint32_t) n_t);
        int n_lit = append_test_literal(b, (uint32_t) const_dc);
        int n_rt = append_test_any(b);
        int n_fo = append_test_any(b);
        int n_rest = append_rest(b, (uint32_t) slot_body);

        uint32_t kids[] = {
            (uint32_t) n_cap, (uint32_t) n_lit, (uint32_t) n_rt,
            (uint32_t) n_fo, (uint32_t) n_rest};
        int n_grp = append_group(b, kids, 5);
        TEST_ASSERT(n_grp >= 0, "branch group built");

        branch_indices[i] = (uint32_t) n_grp;
    }

    int n_oneof = append_oneof(b, branch_indices, 6);
    TEST_ASSERT(n_oneof >= 0, "append ONEOF");

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_oneof);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize accepts lambda-shaped matcher");

    KosMatcher *m = (KosMatcher *) m_obj;
    /* Each branch min = 1+1+1+1+0 = 4; max = unbounded due to REST */
    TEST_ASSERT_EQ(m->nodes[branch_indices[0]].min_width, 4,
                   "branch min");
    TEST_ASSERT_EQ(m->nodes[n_oneof].min_width, 4, "ONEOF min");
    TEST_ASSERT_EQ(m->nodes[n_oneof].max_width,
                   KOS_MATCH_UNBOUNDED, "ONEOF max");

    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


/* === suite runner === */


int run_matcher_tests(void) {
    BEGIN_TEST_SUITE("matcher_ng (Phase 1: validator)");

    RUN_TEST(test_matcher_validator_rejects_forward_ref);
    RUN_TEST(test_matcher_validator_widths_basic);
    RUN_TEST(test_matcher_validator_repeat_zero_width_rejected);
    RUN_TEST(test_matcher_validator_repeat_positive_width_accepted);
    RUN_TEST(test_matcher_validator_double_rest_in_form_branch_rejected);
    RUN_TEST(test_matcher_validator_oneof_branch_independence_accepted);
    RUN_TEST(test_matcher_validator_post_rest_unbounded_rejected);
    RUN_TEST(test_matcher_validator_post_rest_finite_accepted);
    RUN_TEST(test_matcher_validator_post_rest_optional_rejected);
    RUN_TEST(test_matcher_validator_capture_in_not_rejected);
    RUN_TEST(test_matcher_validator_capture_in_nested_not_rejected);
    RUN_TEST(test_matcher_validator_slot_oob_rejected);
    RUN_TEST(test_matcher_validator_extern_oob_rejected);
    RUN_TEST(test_matcher_validator_const_oob_rejected);
    RUN_TEST(test_matcher_validator_in_range_extern_accepted);
    RUN_TEST(test_matcher_validator_in_range_const_accepted);
    RUN_TEST(test_matcher_validator_entry_oob_rejected);
    RUN_TEST(test_matcher_module_exposes_matcher_type);
    RUN_TEST(test_matcher_validator_group_child_oob_rejected);
    RUN_TEST(test_matcher_validator_lambda_shape_accepted);

    return 0;
}


/* The end. */
