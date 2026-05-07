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

    return KosMatcherBuilder_AppendGroup(b, children, (Py_ssize_t) count);
}


static int append_oneof(
    KosMatcherBuilder *b,
    const uint32_t *children,
    uint32_t count) {

    return KosMatcherBuilder_AppendOneof(b, children, (Py_ssize_t) count);
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
      AppendGroup must reject child indices that have not yet been
      appended (forward references).
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_test = append_test_any(b);
    TEST_ASSERT(n_test >= 0, "append TEST node");

    uint32_t kids[] = { (uint32_t) n_test, 99 };
    int idx = KosMatcherBuilder_AppendGroup(b, kids, 2);
    TEST_ASSERT_EQ(idx, -1, "AppendGroup must reject forward ref");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_builder_rejects_duplicate_child(void) {
    /*
      AppendGroup must reject a child list that names the same node
      twice; the intrusive next chain cannot represent that.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int n_a = append_test_any(b);
    int n_b = append_test_any(b);
    TEST_ASSERT(n_a >= 0 && n_b >= 0, "append TEST nodes");

    uint32_t kids[] = { (uint32_t) n_a, (uint32_t) n_b, (uint32_t) n_a };
    int idx = KosMatcherBuilder_AppendGroup(b, kids, 3);
    TEST_ASSERT_EQ(idx, -1, "AppendGroup must reject duplicate child");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception is set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_matcher_var_size(void) {
    /*
      KosMatcher is variable-sized; ob_size carries node_count and the
      type advertises tp_itemsize == sizeof(KosMatchNode).
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int a = append_test_any(b);
    int t = append_test_any(b);
    int c = append_test_any(b);
    TEST_ASSERT(a >= 0 && t >= 0 && c >= 0, "append three TEST nodes");

    uint32_t kids[] = { (uint32_t) a, (uint32_t) t, (uint32_t) c };
    int g = KosMatcherBuilder_AppendGroup(b, kids, 3);
    TEST_ASSERT(g >= 0, "AppendGroup");

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) g);
    KosMatcherBuilder_Free(b);
    TEST_ASSERT_NOT_NULL(m, "Finalize");

    TEST_ASSERT_EQ((long) Py_SIZE(m), 4L,
                   "ob_size matches node_count");
    TEST_ASSERT_EQ((long) Py_TYPE(m)->tp_itemsize,
                   (long) sizeof(KosMatchNode),
                   "tp_itemsize == sizeof(KosMatchNode)");

    Py_DECREF(m);
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


/* === Phase 2: engine tests === */


/**
   Run matcher m against a pysequence subject (a borrowed PyList or
   PyTuple). Returns a NEW MatchResult reference, or NULL.
*/
static PyObject *run_match_pyseq(KosMatcher *m, PyObject *seq) {
    KosMatchSubject subj;
    if (KosMatchSubject_FromPySequence(seq, &subj))
        return NULL;
    PyObject *r = KosMatcher_Match(m, &subj, NULL);
    KosMatchSubject_Release(&subj);
    return r;
}


static PyObject *run_match_pyseq_externs(
    KosMatcher *m,
    PyObject *seq,
    PyObject *extern_env) {

    KosMatchSubject subj;
    if (KosMatchSubject_FromPySequence(seq, &subj))
        return NULL;
    PyObject *r = KosMatcher_Match(m, &subj, extern_env);
    KosMatchSubject_Release(&subj);
    return r;
}


static PyObject *run_match_kospair(KosMatcher *m, PyObject *pair_or_nil) {
    KosMatchSubject subj;
    if (KosMatchSubject_FromKosPair(pair_or_nil, &subj))
        return NULL;
    PyObject *r = KosMatcher_Match(m, &subj, NULL);
    KosMatchSubject_Release(&subj);
    return r;
}


/**
   Build a proper-list KosPair from an array of borrowed-reference
   PyObjects (length n). Returns a new reference to the head pair (or
   KosNil for n=0).
*/
static PyObject *make_kospair(PyObject *const *items, Py_ssize_t n) {
    /* KosPair_ConsFromPtr treats ptr[count-1] as the cdr/tail; to get
       a proper nil-terminated list we tack KosNil onto the array. */
    if (n == 0) {
        Py_INCREF(KosNil);
        return KosNil;
    }
    PyObject **arr = PyMem_Malloc((n + 1) * sizeof(PyObject *));
    if (! arr) {
        PyErr_NoMemory();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++)
        arr[i] = items[i];
    arr[n] = KosNil;
    PyObject *r = KosPair_ConsFromPtr(arr, (int) (n + 1));
    PyMem_Free(arr);
    return r;
}


static int test_match_group_pysequence(void) {
    /*
      Match (TEST_ANY, CAPTURE(name, TEST_TYPE symbol)) against
      [123, foo].
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot = add_str_slot(b, "name");
    int e_sym = add_str_extern(b, "symbol");

    int n_t1 = append_test_any(b);
    int n_t2 = append_test_type(b, (uint32_t) e_sym);
    int n_cap = append_capture(b, (uint32_t) slot, (uint32_t) n_t2);

    uint32_t kids[] = {(uint32_t) n_t1, (uint32_t) n_cap};
    int n_grp = append_group(b, kids, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *foo_name = PyUnicode_FromString("foo");
    PyObject *foo = KosSymbol_FromString(foo_name);
    Py_DECREF(foo_name);
    PyObject *one_two_three = PyLong_FromLong(123);

    PyObject *seq = PyTuple_Pack(2, one_two_three, foo);
    PyObject *extern_env = PyTuple_Pack(1, (PyObject *) &KosSymbolType);

    PyObject *r = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq, extern_env);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    KosMatchResult *mr = (KosMatchResult *) r;
    TEST_ASSERT(mr->matched, "matched");

    PyObject *captured = PyDict_GetItemString(mr->captures, "name");
    TEST_ASSERT_NOT_NULL(captured, "name capture present");
    TEST_ASSERT_SAME(captured, foo, "captured is foo");

    Py_DECREF(r);
    Py_DECREF(extern_env);
    Py_DECREF(seq);
    Py_DECREF(one_two_three);
    Py_DECREF(foo);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_group_kospair(void) {
    /*
      Same matcher as the PySequence test, but the subject is a cons
      list (KosPair). Captures must be identical.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot = add_str_slot(b, "name");
    int e_sym = add_str_extern(b, "symbol");

    int n_t1 = append_test_any(b);
    int n_t2 = append_test_type(b, (uint32_t) e_sym);
    int n_cap = append_capture(b, (uint32_t) slot, (uint32_t) n_t2);

    uint32_t kids[] = {(uint32_t) n_t1, (uint32_t) n_cap};
    int n_grp = append_group(b, kids, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *foo_name = PyUnicode_FromString("bar");
    PyObject *bar = KosSymbol_FromString(foo_name);
    Py_DECREF(foo_name);
    PyObject *seven = PyLong_FromLong(7);

    PyObject *items[2] = {seven, bar};
    PyObject *cons = make_kospair(items, 2);

    PyObject *extern_env = PyTuple_Pack(1, (PyObject *) &KosSymbolType);

    KosMatchSubject subj;
    TEST_ASSERT_EQ(KosMatchSubject_FromKosPair(cons, &subj), 0,
                   "kospair subject built");

    PyObject *r = KosMatcher_Match((KosMatcher *) m_obj, &subj, extern_env);
    KosMatchSubject_Release(&subj);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");
    PyObject *cap = PyDict_GetItemString(
        ((KosMatchResult *) r)->captures, "name");
    TEST_ASSERT_SAME(cap, bar, "captured is bar");

    Py_DECREF(r);
    Py_DECREF(extern_env);
    Py_DECREF(cons);
    Py_DECREF(seven);
    Py_DECREF(bar);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_form_descends_into_pair(void) {
    /*
      Outer matcher is GROUP(TEST_ANY, FORM(GROUP(CAPTURE(x,
      TEST_ANY), TEST_ANY))) — the second outer element is a pair, and
      its first element is captured into x.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot_x = add_str_slot(b, "x");

    int inner_t1 = append_test_any(b);
    int inner_cap = append_capture(b, (uint32_t) slot_x,
                                   (uint32_t) inner_t1);
    int inner_t2 = append_test_any(b);
    uint32_t inner_kids[] = {
        (uint32_t) inner_cap, (uint32_t) inner_t2};
    int inner_grp = append_group(b, inner_kids, 2);
    int n_form = append_form(b, (uint32_t) inner_grp);

    int outer_t1 = append_test_any(b);

    uint32_t outer_kids[] = {(uint32_t) outer_t1, (uint32_t) n_form};
    int outer_grp = append_group(b, outer_kids, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) outer_grp);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *one = PyLong_FromLong(1);
    PyObject *two = PyLong_FromLong(2);
    PyObject *three = PyLong_FromLong(3);
    PyObject *inner_items[2] = {two, three};
    PyObject *inner_pair = make_kospair(inner_items, 2);
    PyObject *outer_items[2] = {one, inner_pair};
    PyObject *outer_pair = make_kospair(outer_items, 2);

    PyObject *r = run_match_kospair((KosMatcher *) m_obj, outer_pair);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");
    PyObject *cap = PyDict_GetItemString(
        ((KosMatchResult *) r)->captures, "x");
    TEST_ASSERT_SAME(cap, two, "captured is 2");

    Py_DECREF(r);
    Py_DECREF(outer_pair);
    Py_DECREF(inner_pair);
    Py_DECREF(one);
    Py_DECREF(two);
    Py_DECREF(three);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_oneof_first_branch_matches(void) {
    /*
      ONEOF(GROUP(CAPTURE(a, LITERAL "x")), GROUP(CAPTURE(b, LITERAL "y")))
      against ["x"]. Captures: a="x", b=ABSENT.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot_a = add_str_slot(b, "a");
    int slot_b = add_str_slot(b, "b");

    PyObject *x_str = PyUnicode_FromString("x");
    int c_x = KosMatcherBuilder_AddConst(b, x_str);
    Py_DECREF(x_str);

    PyObject *y_str = PyUnicode_FromString("y");
    int c_y = KosMatcherBuilder_AddConst(b, y_str);
    Py_DECREF(y_str);

    int t_x = append_test_literal(b, (uint32_t) c_x);
    int cap_a = append_capture(b, (uint32_t) slot_a, (uint32_t) t_x);
    uint32_t k_a[] = {(uint32_t) cap_a};
    int g_a = append_group(b, k_a, 1);

    int t_y = append_test_literal(b, (uint32_t) c_y);
    int cap_b = append_capture(b, (uint32_t) slot_b, (uint32_t) t_y);
    uint32_t k_b[] = {(uint32_t) cap_b};
    int g_b = append_group(b, k_b, 1);

    uint32_t branches[] = {(uint32_t) g_a, (uint32_t) g_b};
    int n_oneof = append_oneof(b, branches, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_oneof);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *xv = PyUnicode_FromString("x");
    PyObject *seq = PyTuple_Pack(1, xv);

    PyObject *r = run_match_pyseq((KosMatcher *) m_obj, seq);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *captures = ((KosMatchResult *) r)->captures;
    PyObject *a = PyDict_GetItemString(captures, "a");
    PyObject *bb = PyDict_GetItemString(captures, "b");
    TEST_ASSERT_NOT_NULL(a, "a present");
    TEST_ASSERT_NOT_NULL(bb, "b present (as ABSENT)");
    TEST_ASSERT_EQ(PyUnicode_CompareWithASCIIString(a, "x"), 0,
                   "a == 'x'");
    TEST_ASSERT_SAME(bb, KosMatchAbsent, "b is ABSENT");

    Py_DECREF(r);
    Py_DECREF(seq);
    Py_DECREF(xv);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_oneof_falls_through(void) {
    /*
      Same matcher as above against ["y"] — second branch matches.
      Captures: a=ABSENT, b="y".
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();

    int slot_a = add_str_slot(b, "a");
    int slot_b = add_str_slot(b, "b");

    PyObject *x_str = PyUnicode_FromString("x");
    int c_x = KosMatcherBuilder_AddConst(b, x_str);
    Py_DECREF(x_str);

    PyObject *y_str = PyUnicode_FromString("y");
    int c_y = KosMatcherBuilder_AddConst(b, y_str);
    Py_DECREF(y_str);

    int t_x = append_test_literal(b, (uint32_t) c_x);
    int cap_a = append_capture(b, (uint32_t) slot_a, (uint32_t) t_x);
    uint32_t k_a[] = {(uint32_t) cap_a};
    int g_a = append_group(b, k_a, 1);

    int t_y = append_test_literal(b, (uint32_t) c_y);
    int cap_b = append_capture(b, (uint32_t) slot_b, (uint32_t) t_y);
    uint32_t k_b[] = {(uint32_t) cap_b};
    int g_b = append_group(b, k_b, 1);

    uint32_t branches[] = {(uint32_t) g_a, (uint32_t) g_b};
    int n_oneof = append_oneof(b, branches, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_oneof);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *yv = PyUnicode_FromString("y");
    PyObject *seq = PyTuple_Pack(1, yv);

    PyObject *r = run_match_pyseq((KosMatcher *) m_obj, seq);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *captures = ((KosMatchResult *) r)->captures;
    PyObject *a = PyDict_GetItemString(captures, "a");
    PyObject *bb = PyDict_GetItemString(captures, "b");
    TEST_ASSERT_SAME(a, KosMatchAbsent, "a is ABSENT");
    TEST_ASSERT_NOT_NULL(bb, "b present");
    TEST_ASSERT_EQ(PyUnicode_CompareWithASCIIString(bb, "y"), 0,
                   "b == 'y'");

    Py_DECREF(r);
    Py_DECREF(seq);
    Py_DECREF(yv);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_oneof_rollback_clean_frame(void) {
    /*
      ONEOF where the first branch partially captures then fails. Make
      sure rollback unsets the partial captures.

      Branch 0: GROUP(CAPTURE(a, TEST_ANY), TEST_LITERAL "Z")
      Branch 1: GROUP(CAPTURE(b, TEST_ANY), TEST_LITERAL "Y")

      Subject: ["x", "Y"] — branch 0 captures a="x" then fails on "Z";
      branch 1 succeeds with b="x". Final captures: a=ABSENT, b="x".
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();

    int slot_a = add_str_slot(b, "a");
    int slot_b = add_str_slot(b, "b");

    PyObject *zstr = PyUnicode_FromString("Z");
    int c_z = KosMatcherBuilder_AddConst(b, zstr);
    Py_DECREF(zstr);
    PyObject *ystr = PyUnicode_FromString("Y");
    int c_y = KosMatcherBuilder_AddConst(b, ystr);
    Py_DECREF(ystr);

    int t_any_a = append_test_any(b);
    int cap_a = append_capture(b, (uint32_t) slot_a, (uint32_t) t_any_a);
    int t_z = append_test_literal(b, (uint32_t) c_z);
    uint32_t k_a[] = {(uint32_t) cap_a, (uint32_t) t_z};
    int g_a = append_group(b, k_a, 2);

    int t_any_b = append_test_any(b);
    int cap_b = append_capture(b, (uint32_t) slot_b, (uint32_t) t_any_b);
    int t_y = append_test_literal(b, (uint32_t) c_y);
    uint32_t k_b[] = {(uint32_t) cap_b, (uint32_t) t_y};
    int g_b = append_group(b, k_b, 2);

    uint32_t branches[] = {(uint32_t) g_a, (uint32_t) g_b};
    int n_oneof = append_oneof(b, branches, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_oneof);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *xv = PyUnicode_FromString("x");
    PyObject *yv = PyUnicode_FromString("Y");
    PyObject *seq = PyTuple_Pack(2, xv, yv);

    PyObject *r = run_match_pyseq((KosMatcher *) m_obj, seq);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *captures = ((KosMatchResult *) r)->captures;
    PyObject *a = PyDict_GetItemString(captures, "a");
    PyObject *bb = PyDict_GetItemString(captures, "b");
    TEST_ASSERT_SAME(a, KosMatchAbsent, "a rollback to ABSENT");
    TEST_ASSERT_EQ(PyUnicode_CompareWithASCIIString(bb, "x"), 0,
                   "b == 'x'");

    Py_DECREF(r);
    Py_DECREF(seq);
    Py_DECREF(xv);
    Py_DECREF(yv);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_repeat_named_per_iteration_frames(void) {
    /*
      CAPTURE(items, REPEAT(GROUP(CAPTURE(val, TEST_ANY))))
      against [1, 2, 3].

      Result: items = [{val: 1}, {val: 2}, {val: 3}]. The val slot
      itself is ABSENT at top level (per-iteration only).
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();

    int slot_items = add_str_slot(b, "items");
    int slot_val = add_str_slot(b, "val");

    int t_any = append_test_any(b);
    int cap_val = append_capture(b, (uint32_t) slot_val, (uint32_t) t_any);
    uint32_t inner_kids[] = {(uint32_t) cap_val};
    int inner_grp = append_group(b, inner_kids, 1);
    int n_rep = append_repeat(b, (uint32_t) inner_grp);
    int cap_items = append_capture(b, (uint32_t) slot_items,
                                   (uint32_t) n_rep);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) cap_items);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *one = PyLong_FromLong(1);
    PyObject *two = PyLong_FromLong(2);
    PyObject *three = PyLong_FromLong(3);
    PyObject *seq = PyTuple_Pack(3, one, two, three);

    PyObject *r = run_match_pyseq((KosMatcher *) m_obj, seq);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *captures = ((KosMatchResult *) r)->captures;
    PyObject *items = PyDict_GetItemString(captures, "items");
    PyObject *val_top = PyDict_GetItemString(captures, "val");
    TEST_ASSERT_NOT_NULL(items, "items present");
    TEST_ASSERT(PyList_Check(items), "items is list");
    TEST_ASSERT_EQ(PyList_GET_SIZE(items), 3, "3 iterations");

    PyObject *iter0 = PyList_GET_ITEM(items, 0);
    TEST_ASSERT(PyDict_Check(iter0), "iter0 is dict");
    PyObject *v0 = PyDict_GetItemString(iter0, "val");
    TEST_ASSERT_SAME(v0, one, "iter0.val == 1");

    PyObject *iter2 = PyList_GET_ITEM(items, 2);
    PyObject *v2 = PyDict_GetItemString(iter2, "val");
    TEST_ASSERT_SAME(v2, three, "iter2.val == 3");

    TEST_ASSERT_SAME(val_top, KosMatchAbsent, "val (top-level) ABSENT");

    Py_DECREF(r);
    Py_DECREF(seq);
    Py_DECREF(one);
    Py_DECREF(two);
    Py_DECREF(three);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_rest_terminal_pysequence(void) {
    /*
      GROUP(TEST_ANY, REST(rest)) against [1, 2, 3, 4].
      rest captures a tuple (2, 3, 4).
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int slot_r = add_str_slot(b, "rest");

    int n_t = append_test_any(b);
    int n_rest = append_rest(b, (uint32_t) slot_r);
    uint32_t kids[] = {(uint32_t) n_t, (uint32_t) n_rest};
    int n_grp = append_group(b, kids, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *one = PyLong_FromLong(1);
    PyObject *two = PyLong_FromLong(2);
    PyObject *three = PyLong_FromLong(3);
    PyObject *four = PyLong_FromLong(4);
    PyObject *seq = PyTuple_Pack(4, one, two, three, four);

    PyObject *r = run_match_pyseq((KosMatcher *) m_obj, seq);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *cap = PyDict_GetItemString(
        ((KosMatchResult *) r)->captures, "rest");
    TEST_ASSERT(PyTuple_Check(cap), "rest is tuple");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(cap), 3, "3-element rest");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(cap, 0), two, "rest[0]==2");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(cap, 2), four, "rest[2]==4");

    Py_DECREF(r);
    Py_DECREF(seq);
    Py_DECREF(one);
    Py_DECREF(two);
    Py_DECREF(three);
    Py_DECREF(four);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_rest_terminal_kospair(void) {
    /*
      Same matcher but kospair input. rest must be a KosPair containing
      (2 . (3 . (4 . nil))).
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int slot_r = add_str_slot(b, "rest");

    int n_t = append_test_any(b);
    int n_rest = append_rest(b, (uint32_t) slot_r);
    uint32_t kids[] = {(uint32_t) n_t, (uint32_t) n_rest};
    int n_grp = append_group(b, kids, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *one = PyLong_FromLong(1);
    PyObject *two = PyLong_FromLong(2);
    PyObject *three = PyLong_FromLong(3);
    PyObject *items[3] = {one, two, three};
    PyObject *cons = make_kospair(items, 3);

    PyObject *r = run_match_kospair((KosMatcher *) m_obj, cons);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *cap = PyDict_GetItemString(
        ((KosMatchResult *) r)->captures, "rest");
    TEST_ASSERT(KosPair_Check(cap) || KosNil_Check(cap),
                "rest is kospair");
    TEST_ASSERT_SAME(KosPair_CAR(cap), two, "rest car==2");

    Py_DECREF(r);
    Py_DECREF(cons);
    Py_DECREF(one);
    Py_DECREF(two);
    Py_DECREF(three);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_rest_nonterminal_with_finite_suffix(void) {
    /*
      GROUP(TEST_ANY, REST(rest), TEST_ANY) against [1, 2, 3, 4, 5].
      rest captures (2, 3, 4); final TEST consumes 5.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int slot_r = add_str_slot(b, "rest");

    int n_t1 = append_test_any(b);
    int n_rest = append_rest(b, (uint32_t) slot_r);
    int n_t2 = append_test_any(b);
    uint32_t kids[] = {
        (uint32_t) n_t1, (uint32_t) n_rest, (uint32_t) n_t2};
    int n_grp = append_group(b, kids, 3);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *one = PyLong_FromLong(1);
    PyObject *two = PyLong_FromLong(2);
    PyObject *three = PyLong_FromLong(3);
    PyObject *four = PyLong_FromLong(4);
    PyObject *five = PyLong_FromLong(5);
    PyObject *seq = PyTuple_Pack(5, one, two, three, four, five);

    PyObject *r = run_match_pyseq((KosMatcher *) m_obj, seq);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *cap = PyDict_GetItemString(
        ((KosMatchResult *) r)->captures, "rest");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(cap), 3, "rest has 3 items");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(cap, 0), two, "rest[0]==2");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(cap, 2), four, "rest[2]==4");

    Py_DECREF(r);
    Py_DECREF(seq);
    Py_DECREF(one);
    Py_DECREF(two);
    Py_DECREF(three);
    Py_DECREF(four);
    Py_DECREF(five);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_optional_absent_present(void) {
    /*
      GROUP(OPTIONAL(CAPTURE(x, TEST_ANY)), TEST_LITERAL "end")
      Against ["v", "end"]: x = "v".
      Against ["end"]:      x = ABSENT.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int slot_x = add_str_slot(b, "x");

    PyObject *end_str = PyUnicode_FromString("end");
    int c_end = KosMatcherBuilder_AddConst(b, end_str);
    Py_DECREF(end_str);

    int t_any = append_test_any(b);
    int cap_x = append_capture(b, (uint32_t) slot_x, (uint32_t) t_any);
    int n_opt = append_optional(b, (uint32_t) cap_x);
    int t_end = append_test_literal(b, (uint32_t) c_end);
    uint32_t kids[] = {(uint32_t) n_opt, (uint32_t) t_end};
    int n_grp = append_group(b, kids, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *vstr = PyUnicode_FromString("v");
    PyObject *estr = PyUnicode_FromString("end");
    PyObject *seq_present = PyTuple_Pack(2, vstr, estr);
    PyObject *r1 = run_match_pyseq((KosMatcher *) m_obj, seq_present);
    TEST_ASSERT_NOT_NULL(r1, "match (present) returned");
    TEST_ASSERT(((KosMatchResult *) r1)->matched, "matched present");
    PyObject *x1 = PyDict_GetItemString(
        ((KosMatchResult *) r1)->captures, "x");
    TEST_ASSERT_EQ(PyUnicode_CompareWithASCIIString(x1, "v"), 0,
                   "x present == 'v'");

    PyObject *seq_absent = PyTuple_Pack(1, estr);
    PyObject *r2 = run_match_pyseq((KosMatcher *) m_obj, seq_absent);
    TEST_ASSERT_NOT_NULL(r2, "match (absent) returned");
    TEST_ASSERT(((KosMatchResult *) r2)->matched, "matched absent");
    PyObject *x2 = PyDict_GetItemString(
        ((KosMatchResult *) r2)->captures, "x");
    TEST_ASSERT_SAME(x2, KosMatchAbsent, "x ABSENT");

    Py_DECREF(r1);
    Py_DECREF(r2);
    Py_DECREF(seq_present);
    Py_DECREF(seq_absent);
    Py_DECREF(vstr);
    Py_DECREF(estr);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_not_blocks(void) {
    /*
      GROUP(NOT(TEST_LITERAL "no"), CAPTURE(x, TEST_ANY))
      Against ["go"]: succeeds, x="go".
      Against ["no"]: fails.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int slot_x = add_str_slot(b, "x");
    PyObject *no_str = PyUnicode_FromString("no");
    int c_no = KosMatcherBuilder_AddConst(b, no_str);
    Py_DECREF(no_str);

    int t_no = append_test_literal(b, (uint32_t) c_no);
    int n_not = append_not(b, (uint32_t) t_no);
    int t_any = append_test_any(b);
    int cap_x = append_capture(b, (uint32_t) slot_x, (uint32_t) t_any);
    uint32_t kids[] = {(uint32_t) n_not, (uint32_t) cap_x};
    int n_grp = append_group(b, kids, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_grp);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *go = PyUnicode_FromString("go");
    PyObject *no = PyUnicode_FromString("no");
    PyObject *seq_ok = PyTuple_Pack(1, go);
    PyObject *seq_bad = PyTuple_Pack(1, no);

    PyObject *r1 = run_match_pyseq((KosMatcher *) m_obj, seq_ok);
    TEST_ASSERT_NOT_NULL(r1, "ok match returned");
    TEST_ASSERT(((KosMatchResult *) r1)->matched, "matched 'go'");
    PyObject *x1 = PyDict_GetItemString(
        ((KosMatchResult *) r1)->captures, "x");
    TEST_ASSERT_EQ(PyUnicode_CompareWithASCIIString(x1, "go"), 0,
                   "x == 'go'");

    PyObject *r2 = run_match_pyseq((KosMatcher *) m_obj, seq_bad);
    TEST_ASSERT_NOT_NULL(r2, "bad match returned");
    TEST_ASSERT_EQ(((KosMatchResult *) r2)->matched, 0,
                   "matched=False for 'no'");

    Py_DECREF(r1);
    Py_DECREF(r2);
    Py_DECREF(seq_ok);
    Py_DECREF(seq_bad);
    Py_DECREF(go);
    Py_DECREF(no);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_extern_type_test(void) {
    /*
      Single-element matcher: TEST_TYPE bound at extern slot 0 to
      &KosSymbolType. Matches a symbol; rejects a non-symbol.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int e_sym = add_str_extern(b, "symbol");
    int n_test = append_test_type(b, (uint32_t) e_sym);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_test);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *sym_name = PyUnicode_FromString("hi");
    PyObject *sym = KosSymbol_FromString(sym_name);
    Py_DECREF(sym_name);

    PyObject *seq_ok = PyTuple_Pack(1, sym);
    PyObject *extern_env = PyTuple_Pack(1, (PyObject *) &KosSymbolType);
    PyObject *r1 = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq_ok, extern_env);
    TEST_ASSERT_NOT_NULL(r1, "ok match returned");
    TEST_ASSERT(((KosMatchResult *) r1)->matched, "matched symbol");

    PyObject *one = PyLong_FromLong(1);
    PyObject *seq_bad = PyTuple_Pack(1, one);
    PyObject *r2 = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq_bad, extern_env);
    TEST_ASSERT_NOT_NULL(r2, "bad match returned");
    TEST_ASSERT_EQ(((KosMatchResult *) r2)->matched, 0,
                   "non-symbol fails");

    Py_DECREF(r1);
    Py_DECREF(r2);
    Py_DECREF(seq_ok);
    Py_DECREF(seq_bad);
    Py_DECREF(extern_env);
    Py_DECREF(sym);
    Py_DECREF(one);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


/**
   Build a six-branch lambda-shaped ONEOF and exercise it. Each branch
   is GROUP(CAPTURE(name, TEST_TYPE symbol), TEST_LITERAL '::', TEST_ANY,
   TEST_ANY, REST(body)) — i.e., name :: ret formals body...

   Branch 0 expects the literal '::' = "::". Other branches each have a
   distinct discriminator literal so we can confirm rollback chooses
   the matching branch.
*/
static int test_match_lambda_shaped_oneof(void) {
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int slot_name = add_str_slot(b, "name");
    int slot_body = add_str_slot(b, "body");
    int e_sym = add_str_extern(b, "symbol");

    /* six discriminator literals (different strings so only one
       branch matches a given input). */
    int discs[6];
    for (int i = 0; i < 6; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "::%d", i);
        PyObject *s = PyUnicode_FromString(buf);
        discs[i] = KosMatcherBuilder_AddConst(b, s);
        Py_DECREF(s);
    }

    uint32_t branches[6];
    for (int i = 0; i < 6; i++) {
        int t_sym = append_test_type(b, (uint32_t) e_sym);
        int cap_name = append_capture(
            b, (uint32_t) slot_name, (uint32_t) t_sym);
        int t_disc = append_test_literal(b, (uint32_t) discs[i]);
        int t_ret = append_test_any(b);
        int t_form = append_test_any(b);
        int n_rest = append_rest(b, (uint32_t) slot_body);
        uint32_t kids[] = {
            (uint32_t) cap_name, (uint32_t) t_disc, (uint32_t) t_ret,
            (uint32_t) t_form, (uint32_t) n_rest};
        int g = append_group(b, kids, 5);
        branches[i] = (uint32_t) g;
    }

    int n_oneof = append_oneof(b, branches, 6);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_oneof);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    /* Build a subject matching branch 3:
       (foo, "::3", retval, formals, body1, body2) */
    PyObject *foo_name = PyUnicode_FromString("foo");
    PyObject *foo = KosSymbol_FromString(foo_name);
    Py_DECREF(foo_name);
    PyObject *disc3 = PyUnicode_FromString("::3");
    PyObject *retv = PyUnicode_FromString("retv");
    PyObject *forms = PyUnicode_FromString("forms");
    PyObject *b1 = PyUnicode_FromString("b1");
    PyObject *b2 = PyUnicode_FromString("b2");
    PyObject *seq = PyTuple_Pack(6, foo, disc3, retv, forms, b1, b2);

    PyObject *extern_env = PyTuple_Pack(1, (PyObject *) &KosSymbolType);
    PyObject *r = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq, extern_env);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *captures = ((KosMatchResult *) r)->captures;
    PyObject *name_val = PyDict_GetItemString(captures, "name");
    PyObject *body_val = PyDict_GetItemString(captures, "body");
    TEST_ASSERT_SAME(name_val, foo, "name == foo");
    TEST_ASSERT(PyTuple_Check(body_val), "body is tuple");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(body_val), 2, "body has 2 items");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(body_val, 0), b1, "body[0]==b1");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(body_val, 1), b2, "body[1]==b2");

    Py_DECREF(r);
    Py_DECREF(extern_env);
    Py_DECREF(seq);
    Py_DECREF(foo);
    Py_DECREF(disc3);
    Py_DECREF(retv);
    Py_DECREF(forms);
    Py_DECREF(b1);
    Py_DECREF(b2);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


/* === Phase 3: thaw tests === */


/**
   Build the thaw entry point lazily by importing the type from the
   already-registered KosMatcherType. Returns a NEW reference to a
   bound classmethod ('Matcher.from_tuple') or NULL on error.
*/
static PyObject *get_from_tuple_method(void) {
    PyObject *m = PyObject_GetAttrString(
        (PyObject *) &KosMatcherType, "from_tuple");
    return m;
}


static int test_thaw_minimal(void) {
    /*
      Frozen schema for a minimal matcher: one TEST_ANY at index 0,
      entry=0, no slots/externs/consts/meta.
    */
    PyObject *empty = PyTuple_New(0);
    PyObject *meta = PyDict_New();
    PyObject *test_node = Py_BuildValue("(ssi)", "TEST", "ANY", -1);
    TEST_ASSERT_NOT_NULL(test_node, "build TEST node");

    PyObject *nodes = PyTuple_Pack(1, test_node);
    PyObject *frozen = Py_BuildValue("(siOOOOiO)",
                                     "MATCHER", 1,
                                     empty, empty, empty, nodes, 0, meta);
    TEST_ASSERT_NOT_NULL(frozen, "build frozen tuple");

    PyObject *from_tuple = get_from_tuple_method();
    TEST_ASSERT_NOT_NULL(from_tuple, "fetch from_tuple");

    PyObject *m = PyObject_CallOneArg(from_tuple, frozen);
    TEST_ASSERT_NOT_NULL(m, "thaw minimal matcher");
    TEST_ASSERT(KosMatcher_Check(m), "result is Matcher");

    KosMatcher *km = (KosMatcher *) m;
    TEST_ASSERT_EQ(Py_SIZE(km), 1, "1 node");
    TEST_ASSERT_EQ(km->nodes[0].kind, KOS_MATCH_NODE_TEST, "TEST kind");
    TEST_ASSERT_EQ(km->nodes[0].as.test.test_kind,
                   KOS_MATCH_TEST_ANY, "ANY test_kind");

    Py_DECREF(m);
    Py_DECREF(from_tuple);
    Py_DECREF(frozen);
    Py_DECREF(nodes);
    Py_DECREF(test_node);
    Py_DECREF(meta);
    Py_DECREF(empty);
    return 0;
}


static int test_thaw_rejects_bad_version(void) {
    /*
      Reject a frozen tuple whose version != 1.
    */
    PyObject *empty = PyTuple_New(0);
    PyObject *meta = PyDict_New();
    PyObject *frozen = Py_BuildValue("(siOOOOiO)",
                                     "MATCHER", 99,
                                     empty, empty, empty, empty, 0, meta);
    TEST_ASSERT_NOT_NULL(frozen, "build frozen tuple");

    PyObject *from_tuple = get_from_tuple_method();
    PyObject *m = PyObject_CallOneArg(from_tuple, frozen);
    TEST_ASSERT_NULL(m, "thaw must reject bad version");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception set");
    PyErr_Clear();

    Py_DECREF(from_tuple);
    Py_DECREF(frozen);
    Py_DECREF(meta);
    Py_DECREF(empty);
    return 0;
}


static int test_thaw_rejects_unknown_node_tag(void) {
    PyObject *empty = PyTuple_New(0);
    PyObject *meta = PyDict_New();
    PyObject *bad_node = Py_BuildValue("(si)", "BOGUS", 0);
    PyObject *nodes = PyTuple_Pack(1, bad_node);
    PyObject *frozen = Py_BuildValue("(siOOOOiO)",
                                     "MATCHER", 1,
                                     empty, empty, empty, nodes, 0, meta);

    PyObject *from_tuple = get_from_tuple_method();
    PyObject *m = PyObject_CallOneArg(from_tuple, frozen);
    TEST_ASSERT_NULL(m, "thaw must reject unknown node tag");
    PyErr_Clear();

    Py_DECREF(from_tuple);
    Py_DECREF(frozen);
    Py_DECREF(nodes);
    Py_DECREF(bad_node);
    Py_DECREF(meta);
    Py_DECREF(empty);
    return 0;
}


static int test_thaw_rejects_index_oob(void) {
    /*
      CAPTURE.slot index 5 with 1 slot defined. Validator rejects.
    */
    PyObject *empty = PyTuple_New(0);
    PyObject *meta = PyDict_New();

    PyObject *xname = PyUnicode_FromString("x");
    PyObject *slots = PyTuple_Pack(1, xname);
    PyObject *test_node = Py_BuildValue("(ssi)", "TEST", "ANY", -1);
    /* CAPTURE: kind=CAPTURE, slot=5 (OOB), subnode=0, hr=0, scope=-1 */
    PyObject *cap_node = Py_BuildValue("(siiii)",
                                       "CAPTURE", 5, 0, 0, -1);
    PyObject *nodes = PyTuple_Pack(2, test_node, cap_node);

    PyObject *frozen = Py_BuildValue("(siOOOOiO)",
                                     "MATCHER", 1,
                                     slots, empty, empty,
                                     nodes, 1, meta);

    PyObject *from_tuple = get_from_tuple_method();
    PyObject *m = PyObject_CallOneArg(from_tuple, frozen);
    TEST_ASSERT_NULL(m, "thaw must reject OOB slot index");
    PyErr_Clear();

    Py_DECREF(from_tuple);
    Py_DECREF(frozen);
    Py_DECREF(nodes);
    Py_DECREF(test_node);
    Py_DECREF(cap_node);
    Py_DECREF(slots);
    Py_DECREF(xname);
    Py_DECREF(meta);
    Py_DECREF(empty);
    return 0;
}


static int test_thaw_full_lambda(void) {
    /*
      Thaw a six-branch lambda-style matcher and run it. Mirrors
      test_match_lambda_shaped_oneof but starts from a frozen tuple
      built in C.
    */
    PyObject *meta = PyDict_New();
    PyObject *empty = PyTuple_New(0);

    /* slots */
    PyObject *s_name = PyUnicode_FromString("name");
    PyObject *s_body = PyUnicode_FromString("body");
    PyObject *slots = PyTuple_Pack(2, s_name, s_body);

    /* externs */
    PyObject *e_sym = PyUnicode_FromString("symbol");
    PyObject *externs = PyTuple_Pack(1, e_sym);

    /* consts: "::0".."::5" */
    PyObject *consts_list = PyList_New(0);
    for (int i = 0; i < 6; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "::%d", i);
        PyObject *s = PyUnicode_FromString(buf);
        PyList_Append(consts_list, s);
        Py_DECREF(s);
    }
    PyObject *consts = PySequence_Tuple(consts_list);
    Py_DECREF(consts_list);

    /* Build nodes list bottom-up. Each branch is 6 nodes:
       TEST_TYPE, CAPTURE, TEST_LITERAL, TEST_ANY, TEST_ANY, REST,
       then GROUP. ONEOF references the GROUPs by index. */
    PyObject *nodes_list = PyList_New(0);
    Py_ssize_t branch_indices[6];

    for (int i = 0; i < 6; i++) {
        Py_ssize_t base = PyList_GET_SIZE(nodes_list);

        PyObject *t_sym = Py_BuildValue("(ssi)", "TEST", "TYPE", 0);
        PyObject *cap = Py_BuildValue("(siiii)", "CAPTURE", 0,
                                      (int)(base + 0), 0, -1);
        PyObject *t_lit = Py_BuildValue("(ssi)", "TEST", "LITERAL", i);
        PyObject *t_ret = Py_BuildValue("(ssi)", "TEST", "ANY", -1);
        PyObject *t_form = Py_BuildValue("(ssi)", "TEST", "ANY", -1);
        PyObject *rest = Py_BuildValue("(si)", "REST", 1);
        PyObject *grp = Py_BuildValue(
            "(siiiii)", "GROUP",
            (int)(base + 1), (int)(base + 2), (int)(base + 3),
            (int)(base + 4), (int)(base + 5));

        PyList_Append(nodes_list, t_sym);
        PyList_Append(nodes_list, cap);
        PyList_Append(nodes_list, t_lit);
        PyList_Append(nodes_list, t_ret);
        PyList_Append(nodes_list, t_form);
        PyList_Append(nodes_list, rest);
        PyList_Append(nodes_list, grp);
        Py_DECREF(t_sym);
        Py_DECREF(cap);
        Py_DECREF(t_lit);
        Py_DECREF(t_ret);
        Py_DECREF(t_form);
        Py_DECREF(rest);
        Py_DECREF(grp);

        branch_indices[i] = base + 6;
    }

    Py_ssize_t oneof_idx = PyList_GET_SIZE(nodes_list);
    PyObject *oneof = Py_BuildValue("(siiiiii)", "ONEOF",
                                    (int) branch_indices[0],
                                    (int) branch_indices[1],
                                    (int) branch_indices[2],
                                    (int) branch_indices[3],
                                    (int) branch_indices[4],
                                    (int) branch_indices[5]);
    PyList_Append(nodes_list, oneof);
    Py_DECREF(oneof);

    PyObject *nodes = PySequence_Tuple(nodes_list);
    Py_DECREF(nodes_list);

    PyObject *frozen = Py_BuildValue("(siOOOOiO)",
                                     "MATCHER", 1,
                                     slots, externs, consts,
                                     nodes, (int) oneof_idx, meta);

    PyObject *from_tuple = get_from_tuple_method();
    PyObject *m_obj = PyObject_CallOneArg(from_tuple, frozen);
    TEST_ASSERT_NOT_NULL(m_obj, "thaw lambda matcher");

    /* Build a subject matching branch 4. */
    PyObject *foo_n = PyUnicode_FromString("zoo");
    PyObject *foo = KosSymbol_FromString(foo_n);
    Py_DECREF(foo_n);
    PyObject *disc4 = PyUnicode_FromString("::4");
    PyObject *retv = PyUnicode_FromString("rt");
    PyObject *forms = PyUnicode_FromString("fm");
    PyObject *b1 = PyUnicode_FromString("b1");
    PyObject *b2 = PyUnicode_FromString("b2");
    PyObject *seq = PyTuple_Pack(6, foo, disc4, retv, forms, b1, b2);
    PyObject *extern_env = PyTuple_Pack(1, (PyObject *) &KosSymbolType);

    PyObject *r = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq, extern_env);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *captures = ((KosMatchResult *) r)->captures;
    PyObject *name_v = PyDict_GetItemString(captures, "name");
    PyObject *body_v = PyDict_GetItemString(captures, "body");
    TEST_ASSERT_SAME(name_v, foo, "name == zoo");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(body_v), 2, "body has 2 items");

    Py_DECREF(r);
    Py_DECREF(extern_env);
    Py_DECREF(seq);
    Py_DECREF(b2);
    Py_DECREF(b1);
    Py_DECREF(forms);
    Py_DECREF(retv);
    Py_DECREF(disc4);
    Py_DECREF(foo);
    Py_DECREF(m_obj);
    Py_DECREF(from_tuple);
    Py_DECREF(frozen);
    Py_DECREF(nodes);
    Py_DECREF(consts);
    Py_DECREF(externs);
    Py_DECREF(e_sym);
    Py_DECREF(slots);
    Py_DECREF(s_body);
    Py_DECREF(s_name);
    Py_DECREF(empty);
    Py_DECREF(meta);
    return 0;
}


/* === Phase 4: CLASS / FIELD tests === */


static int append_field_pos(KosMatcherBuilder *b, uint32_t subnode) {
    return KosMatcherBuilder_AppendFieldPositional(b, subnode);
}


static int append_field_kw(
    KosMatcherBuilder *b,
    uint32_t subnode,
    const char *attr) {

    PyObject *name = PyUnicode_FromString(attr);
    if (! name)
        return -1;
    int cidx = KosMatcherBuilder_AddConst(b, name);
    Py_DECREF(name);
    if (cidx < 0)
        return -1;
    return KosMatcherBuilder_AppendFieldKeyword(
        b, subnode, (uint32_t) cidx);
}


static int append_class(
    KosMatcherBuilder *b,
    uint32_t type_extern,
    const uint32_t *fields,
    uint32_t count) {

    return KosMatcherBuilder_AppendClass(
        b, type_extern, fields, (Py_ssize_t) count);
}


/**
   Build a fresh Python class via the runtime metaclass:
     type(name, (object,), {'__match_args__': match_args} or {})
   match_args may be NULL, in which case __match_args__ is not set.
   Returns a new class reference.
*/
static PyObject *make_test_class(const char *name, PyObject *match_args) {
    PyObject *cls_name = PyUnicode_FromString(name);
    PyObject *bases = PyTuple_Pack(1, (PyObject *) &PyBaseObject_Type);
    PyObject *attrs = PyDict_New();
    if (match_args)
        PyDict_SetItemString(attrs, "__match_args__", match_args);

    PyObject *args = PyTuple_Pack(3, cls_name, bases, attrs);
    PyObject *cls = PyObject_CallObject(
        (PyObject *) &PyType_Type, args);

    Py_DECREF(args);
    Py_DECREF(attrs);
    Py_DECREF(bases);
    Py_DECREF(cls_name);
    return cls;
}


/**
   Instantiate the supplied class (no __init__ args) and decorate the
   instance with the given keyword attributes. attrs is a NULL-terminated
   list of (const char *, PyObject *) pairs (borrowed values).
*/
static PyObject *make_test_instance(
    PyObject *cls,
    const char **attr_names,
    PyObject *const *attr_values,
    Py_ssize_t n) {

    PyObject *inst = PyObject_CallObject(cls, NULL);
    if (! inst)
        return NULL;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (PyObject_SetAttrString(inst, attr_names[i],
                                   attr_values[i])) {
            Py_DECREF(inst);
            return NULL;
        }
    }
    return inst;
}


static int test_match_class_kw_only(void) {
    /*
      CLASS(Foo, FIELD_KW('a', CAPTURE('a', TEST_ANY)),
                 FIELD_KW('b', CAPTURE('b', TEST_ANY)))
      against (Foo(a=X, b=Y),). Captures a=X, b=Y.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    TEST_ASSERT_NOT_NULL(b, "create builder");

    int slot_a = add_str_slot(b, "a");
    int slot_b = add_str_slot(b, "b");
    int e_foo = add_str_extern(b, "Foo");
    TEST_ASSERT(slot_a >= 0 && slot_b >= 0 && e_foo >= 0,
                "slots/extern added");

    int t1 = append_test_any(b);
    int cap_a = append_capture(b, (uint32_t) slot_a, (uint32_t) t1);
    int f_a = append_field_kw(b, (uint32_t) cap_a, "a");

    int t2 = append_test_any(b);
    int cap_b = append_capture(b, (uint32_t) slot_b, (uint32_t) t2);
    int f_b = append_field_kw(b, (uint32_t) cap_b, "b");
    TEST_ASSERT(f_a >= 0 && f_b >= 0, "FIELD_KWs appended");

    uint32_t fields[] = {(uint32_t) f_a, (uint32_t) f_b};
    int n_cls = append_class(b, (uint32_t) e_foo, fields, 2);
    TEST_ASSERT(n_cls >= 0, "AppendClass");

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_cls);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *cls = make_test_class("Foo", NULL);
    TEST_ASSERT_NOT_NULL(cls, "build Foo class");

    PyObject *X = PyLong_FromLong(11);
    PyObject *Y = PyLong_FromLong(22);
    const char *names[] = {"a", "b"};
    PyObject *vals[] = {X, Y};
    PyObject *inst = make_test_instance(cls, names, vals, 2);
    TEST_ASSERT_NOT_NULL(inst, "build instance");

    PyObject *seq = PyTuple_Pack(1, inst);
    PyObject *extern_env = PyTuple_Pack(1, cls);
    PyObject *r = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq, extern_env);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *captures = ((KosMatchResult *) r)->captures;
    TEST_ASSERT_SAME(PyDict_GetItemString(captures, "a"), X, "a captured");
    TEST_ASSERT_SAME(PyDict_GetItemString(captures, "b"), Y, "b captured");

    Py_DECREF(r);
    Py_DECREF(extern_env);
    Py_DECREF(seq);
    Py_DECREF(inst);
    Py_DECREF(Y);
    Py_DECREF(X);
    Py_DECREF(cls);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_class_positional(void) {
    /*
      Class Bar with __match_args__ = ("first", "second").
      CLASS(Bar, FIELD_POS(CAPTURE('first', TEST_ANY)),
                 FIELD_POS(CAPTURE('second', TEST_ANY))).
      Subject (Bar(first=A, second=B),).
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int slot_first = add_str_slot(b, "first");
    int slot_second = add_str_slot(b, "second");
    int e_bar = add_str_extern(b, "Bar");

    int t1 = append_test_any(b);
    int cap1 = append_capture(b, (uint32_t) slot_first, (uint32_t) t1);
    int f1 = append_field_pos(b, (uint32_t) cap1);

    int t2 = append_test_any(b);
    int cap2 = append_capture(b, (uint32_t) slot_second, (uint32_t) t2);
    int f2 = append_field_pos(b, (uint32_t) cap2);

    uint32_t fields[] = {(uint32_t) f1, (uint32_t) f2};
    int n_cls = append_class(b, (uint32_t) e_bar, fields, 2);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_cls);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *first_name = PyUnicode_FromString("first");
    PyObject *second_name = PyUnicode_FromString("second");
    PyObject *match_args = PyTuple_Pack(2, first_name, second_name);
    PyObject *cls = make_test_class("Bar", match_args);

    PyObject *A = PyLong_FromLong(101);
    PyObject *B = PyLong_FromLong(202);
    const char *names[] = {"first", "second"};
    PyObject *vals[] = {A, B};
    PyObject *inst = make_test_instance(cls, names, vals, 2);

    PyObject *seq = PyTuple_Pack(1, inst);
    PyObject *extern_env = PyTuple_Pack(1, cls);
    PyObject *r = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq, extern_env);
    TEST_ASSERT_NOT_NULL(r, "match returned");
    TEST_ASSERT(((KosMatchResult *) r)->matched, "matched");

    PyObject *captures = ((KosMatchResult *) r)->captures;
    TEST_ASSERT_SAME(PyDict_GetItemString(captures, "first"), A,
                     "first captured");
    TEST_ASSERT_SAME(PyDict_GetItemString(captures, "second"), B,
                     "second captured");

    Py_DECREF(r);
    Py_DECREF(extern_env);
    Py_DECREF(seq);
    Py_DECREF(inst);
    Py_DECREF(B);
    Py_DECREF(A);
    Py_DECREF(cls);
    Py_DECREF(match_args);
    Py_DECREF(second_name);
    Py_DECREF(first_name);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_class_typeerror_no_match_args(void) {
    /*
      Positional FIELDs but the class has no __match_args__ — match
      must raise TypeError (NULL result, exception set).
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int slot_first = add_str_slot(b, "first");
    int e_baz = add_str_extern(b, "Baz");

    int t1 = append_test_any(b);
    int cap1 = append_capture(b, (uint32_t) slot_first, (uint32_t) t1);
    int f1 = append_field_pos(b, (uint32_t) cap1);
    uint32_t fields[] = {(uint32_t) f1};
    int n_cls = append_class(b, (uint32_t) e_baz, fields, 1);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_cls);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *cls = make_test_class("Baz", NULL);
    const char *names[] = {"first"};
    PyObject *A = PyLong_FromLong(7);
    PyObject *vals[] = {A};
    PyObject *inst = make_test_instance(cls, names, vals, 1);

    PyObject *seq = PyTuple_Pack(1, inst);
    PyObject *extern_env = PyTuple_Pack(1, cls);
    PyObject *r = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq, extern_env);
    TEST_ASSERT_NULL(r, "match must fail with TypeError");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception set");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_TypeError),
                "TypeError raised");
    PyErr_Clear();

    Py_DECREF(extern_env);
    Py_DECREF(seq);
    Py_DECREF(inst);
    Py_DECREF(A);
    Py_DECREF(cls);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_match_class_empty_isinstance(void) {
    /*
      CLASS(Qux) with no FIELDs. Matches any Qux instance, fails on
      non-Qux instance.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int e_qux = add_str_extern(b, "Qux");
    int n_cls = append_class(b, (uint32_t) e_qux, NULL, 0);

    PyObject *m_obj = KosMatcherBuilder_Finalize(b, (uint32_t) n_cls);
    TEST_ASSERT_NOT_NULL(m_obj, "finalize");

    PyObject *cls = make_test_class("Qux", NULL);
    PyObject *inst = make_test_instance(cls, NULL, NULL, 0);
    PyObject *seq_ok = PyTuple_Pack(1, inst);
    PyObject *extern_env = PyTuple_Pack(1, cls);

    PyObject *r1 = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq_ok, extern_env);
    TEST_ASSERT_NOT_NULL(r1, "match returned (Qux)");
    TEST_ASSERT(((KosMatchResult *) r1)->matched, "matched Qux");

    PyObject *not_qux = PyLong_FromLong(123);
    PyObject *seq_bad = PyTuple_Pack(1, not_qux);
    PyObject *r2 = run_match_pyseq_externs(
        (KosMatcher *) m_obj, seq_bad, extern_env);
    TEST_ASSERT_NOT_NULL(r2, "match returned (non-Qux)");
    TEST_ASSERT_EQ(((KosMatchResult *) r2)->matched, 0,
                   "non-Qux fails");

    Py_DECREF(r1);
    Py_DECREF(r2);
    Py_DECREF(extern_env);
    Py_DECREF(seq_ok);
    Py_DECREF(seq_bad);
    Py_DECREF(inst);
    Py_DECREF(not_qux);
    Py_DECREF(cls);
    Py_DECREF(m_obj);
    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_validator_field_orphan_rejected(void) {
    /*
      Append a FIELD_POS but never reference it from a CLASS.
      Finalize must reject the orphan.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();

    int t1 = append_test_any(b);
    int f1 = append_field_pos(b, (uint32_t) t1);
    TEST_ASSERT(f1 >= 0, "FIELD_POS appended");

    int t_entry = append_test_any(b);

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) t_entry);
    TEST_ASSERT_NULL(m, "finalize must reject orphan FIELD");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_validator_field_subnode_width_rejected(void) {
    /*
      FIELD_POS(GROUP(TEST_ANY, TEST_ANY)) — subnode min/max width is
      2, not 1. Finalize must reject.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int e_foo = add_str_extern(b, "Foo");

    int t1 = append_test_any(b);
    int t2 = append_test_any(b);
    uint32_t kids[] = {(uint32_t) t1, (uint32_t) t2};
    int g = append_group(b, kids, 2);
    int f = append_field_pos(b, (uint32_t) g);

    uint32_t fields[] = {(uint32_t) f};
    int n_cls = append_class(b, (uint32_t) e_foo, fields, 1);

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_cls);
    TEST_ASSERT_NULL(m, "finalize must reject FIELD subnode width != 1");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_validator_field_kw_name_const_oob_rejected(void) {
    /*
      AppendFieldKeyword with a name_const past the end of the consts
      list is rejected at append time.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();

    int t = append_test_any(b);
    int rc = KosMatcherBuilder_AppendFieldKeyword(
        b, (uint32_t) t, 99);
    TEST_ASSERT_EQ(rc, -1, "AppendFieldKeyword OOB rejected");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception set");
    PyErr_Clear();

    KosMatcherBuilder_Free(b);
    return 0;
}


static int test_validator_field_kw_name_const_not_unicode_rejected(void) {
    /*
      FIELD_KW pointing at a const that is not str — AppendFieldKeyword
      accepts at append (range-only check) but Finalize must reject when
      it sees a non-PyUnicode value at the slot.
    */
    KosMatcherBuilder *b = KosMatcherBuilder_New();
    int e_foo = add_str_extern(b, "Foo");

    int c_int = add_const_long(b, 42);
    TEST_ASSERT(c_int >= 0, "add non-str const");

    int t = append_test_any(b);
    int f = KosMatcherBuilder_AppendFieldKeyword(
        b, (uint32_t) t, (uint32_t) c_int);
    TEST_ASSERT(f >= 0, "AppendFieldKeyword (non-unicode const)");

    uint32_t fields[] = {(uint32_t) f};
    int n_cls = append_class(b, (uint32_t) e_foo, fields, 1);

    PyObject *m = KosMatcherBuilder_Finalize(b, (uint32_t) n_cls);
    TEST_ASSERT_NULL(m, "finalize must reject non-unicode kw name_const");
    TEST_ASSERT(PyErr_Occurred() != NULL, "exception set");
    PyErr_Clear();

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
    RUN_TEST(test_matcher_builder_rejects_duplicate_child);
    RUN_TEST(test_matcher_var_size);
    RUN_TEST(test_matcher_validator_lambda_shape_accepted);

    BEGIN_TEST_SUITE("matcher_ng (Phase 2: engine)");

    RUN_TEST(test_match_group_pysequence);
    RUN_TEST(test_match_group_kospair);
    RUN_TEST(test_match_form_descends_into_pair);
    RUN_TEST(test_match_oneof_first_branch_matches);
    RUN_TEST(test_match_oneof_falls_through);
    RUN_TEST(test_match_oneof_rollback_clean_frame);
    RUN_TEST(test_match_repeat_named_per_iteration_frames);
    RUN_TEST(test_match_rest_terminal_pysequence);
    RUN_TEST(test_match_rest_terminal_kospair);
    RUN_TEST(test_match_rest_nonterminal_with_finite_suffix);
    RUN_TEST(test_match_optional_absent_present);
    RUN_TEST(test_match_not_blocks);
    RUN_TEST(test_match_extern_type_test);
    RUN_TEST(test_match_lambda_shaped_oneof);

    BEGIN_TEST_SUITE("matcher_ng (Phase 3: thaw)");

    RUN_TEST(test_thaw_minimal);
    RUN_TEST(test_thaw_rejects_bad_version);
    RUN_TEST(test_thaw_rejects_unknown_node_tag);
    RUN_TEST(test_thaw_rejects_index_oob);
    RUN_TEST(test_thaw_full_lambda);

    BEGIN_TEST_SUITE("matcher_ng (Phase 4: CLASS / FIELD)");

    RUN_TEST(test_match_class_kw_only);
    RUN_TEST(test_match_class_positional);
    RUN_TEST(test_match_class_typeerror_no_match_args);
    RUN_TEST(test_match_class_empty_isinstance);
    RUN_TEST(test_validator_field_orphan_rejected);
    RUN_TEST(test_validator_field_subnode_width_rejected);
    RUN_TEST(test_validator_field_kw_name_const_oob_rejected);
    RUN_TEST(test_validator_field_kw_name_const_not_unicode_rejected);

    return 0;
}


/* The end. */
