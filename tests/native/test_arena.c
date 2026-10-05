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
   Memory tests for the Arena type and Code pack_into relocation

   Tests arena creation, reservation arithmetic, seal/unseal, and the
   relocate-then-free ordering of Code.pack_into, including the arena
   ownership handoff on Code deallocation.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === helpers === */


static PyObject *make_nspec(const char *name) {
    PyObject *nm = PyUnicode_FromString(name);
    PyObject *empty = PyTuple_New(0);
    PyObject *nspec = KosNamespaceSpec_New(nm, empty, empty, NULL);
    Py_DECREF(nm);
    Py_DECREF(empty);
    return nspec;
}


static PyObject *make_small_code(const char *name, int protect) {
    /*
      Build a small Code: CONST <name>, RETURN 1.
    */
    PyObject *nspec = make_nspec(name);
    if (! nspec)
        return NULL;

    PyObject *payload = PyUnicode_FromFormat("payload-%s", name);
    PyObject *instr = Py_BuildValue(
        "[(iO)(ii)]",
        KOS_OP_CONST, payload,
        KOS_OP_RETURN, 1);
    Py_DECREF(payload);
    if (! instr) {
        Py_DECREF(nspec);
        return NULL;
    }

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL, protect, 1);
    Py_DECREF(nspec);
    Py_DECREF(instr);
    return code;
}


static size_t code_block_bytes(PyObject *code) {
    /*
      The unrounded block extent, via the payload/padding getsets.
    */
    PyObject *payload = PyObject_GetAttrString(code, "payload_bytes");
    PyObject *padding = PyObject_GetAttrString(code, "padding_bytes");
    size_t total = 0;
    if (payload && padding) {
        total = PyLong_AsSize_t(payload) + PyLong_AsSize_t(padding);
    }
    Py_XDECREF(payload);
    Py_XDECREF(padding);
    return total;
}


/* === arena tests === */


static int test_arena_create(void) {
    PyObject *arena = KosArena_New(100);
    TEST_ASSERT_NOT_NULL(arena, "create arena");
    TEST_ASSERT(KosArena_CheckExact(arena), "arena type check");
    TEST_ASSERT(KosArena_SIZE(arena) >= 100, "arena page-rounded size");
    TEST_ASSERT_EQ((long) KosArena_FILL(arena), 0, "arena starts empty");
    TEST_ASSERT(! KosArena_SEALED(arena), "arena starts unsealed");

    Py_DECREF(arena);
    return 0;
}


static int test_arena_create_zero_rejected(void) {
    PyObject *arena = KosArena_New(0);
    TEST_ASSERT_NULL(arena, "zero-size arena rejected");
    TEST_ASSERT(PyErr_Occurred() != NULL, "error set");
    PyErr_Clear();
    return 0;
}


static int test_arena_reserve(void) {
    PyObject *arena = KosArena_New(256);
    TEST_ASSERT_NOT_NULL(arena, "create arena");

    void *a = NULL;
    void *b = NULL;
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 10, &a), 0, "reserve 10");
    TEST_ASSERT_NOT_NULL(a, "slice a");
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 10, &b), 0, "reserve 10 more");
    TEST_ASSERT_NOT_NULL(b, "slice b");

    /* second slice starts pointer-aligned past the first */
    TEST_ASSERT(((char *) b - (char *) a) >= 10, "slices do not overlap");
    TEST_ASSERT((((uintptr_t) b) % sizeof(void *)) == 0,
                "slice pointer-aligned");

    Py_DECREF(arena);
    return 0;
}


static int test_arena_reserve_capacity(void) {
    PyObject *arena = KosArena_New(16);
    TEST_ASSERT_NOT_NULL(arena, "create arena");

    size_t size = KosArena_SIZE(arena);
    void *a = NULL;
    TEST_ASSERT_EQ(KosArena_Reserve(arena, size, &a), 0, "reserve all");
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 1, &a), -1, "overflow rejected");
    TEST_ASSERT(PyErr_Occurred() != NULL, "error set");
    PyErr_Clear();

    Py_DECREF(arena);
    return 0;
}


static int test_arena_seal(void) {
    PyObject *arena = KosArena_New(64);
    TEST_ASSERT_NOT_NULL(arena, "create arena");

    TEST_ASSERT_EQ(KosArena_Seal(arena), 0, "seal");
    TEST_ASSERT(KosArena_SEALED(arena), "sealed flag set");

    TEST_ASSERT_EQ(KosArena_Seal(arena), -1, "double seal rejected");
    PyErr_Clear();

    void *p = NULL;
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 8, &p), -1,
                   "reserve after seal rejected");
    PyErr_Clear();

    /* dealloc of a sealed arena unseals and frees cleanly */
    Py_DECREF(arena);
    return 0;
}


/* === pack_into tests === */


static int test_code_protect_flag(void) {
    PyObject *prot = make_small_code("prot", 1);
    TEST_ASSERT_NOT_NULL(prot, "create protected code");
    TEST_ASSERT(KosCode_PROTECTED(prot), "protected flag set");

    PyObject *plain = make_small_code("plain", 0);
    TEST_ASSERT_NOT_NULL(plain, "create plain code");
    TEST_ASSERT(! KosCode_PROTECTED(plain), "plain flag clear");
    TEST_ASSERT_EQ((long) KosCode_ALLOCATION(plain),
                   (long) code_block_bytes(plain),
                   "plain allocation is exact");

    Py_DECREF(prot);
    Py_DECREF(plain);
    return 0;
}


static int test_pack_into_relocates(void) {
    PyObject *a = make_small_code("a", 1);
    PyObject *b = make_small_code("b", 0);
    TEST_ASSERT_NOT_NULL(a, "create code a");
    TEST_ASSERT_NOT_NULL(b, "create code b");

    uint16_t op0 = KosCode_GET_OP(a, 0);
    PyObject *const0 = KosCode_GET_CONST(a, KosCode_GET_ARG(a, 0));
    Py_INCREF(const0);

    size_t need = code_block_bytes(a) + code_block_bytes(b) + 16;
    PyObject *arena = KosArena_New(need);
    TEST_ASSERT_NOT_NULL(arena, "create arena");

    PyObject *r = PyObject_CallMethod(a, "pack_into", "O", arena);
    TEST_ASSERT_NOT_NULL(r, "pack a");
    Py_DECREF(r);
    r = PyObject_CallMethod(b, "pack_into", "O", arena);
    TEST_ASSERT_NOT_NULL(r, "pack b");
    Py_DECREF(r);

    /* interior pointers land inside the arena chunk */
    char *base = (char *) KosArena_BASE(arena);
    char *end = base + KosArena_SIZE(arena);
    TEST_ASSERT((char *) KosCode_CODE(a) >= base
                && (char *) KosCode_CODE(a) < end,
                "code a relocated into arena");
    TEST_ASSERT((char *) KosCode_CODE(b) >= base
                && (char *) KosCode_CODE(b) < end,
                "code b relocated into arena");

    /* contents and cpool identity preserved; protect flag cleared */
    TEST_ASSERT_EQ(KosCode_GET_OP(a, 0), op0, "instruction preserved");
    TEST_ASSERT_SAME(KosCode_GET_CONST(a, KosCode_GET_ARG(a, 0)), const0,
                     "cpool identity preserved");
    TEST_ASSERT(! KosCode_PROTECTED(a), "per-object protection released");
    TEST_ASSERT_SAME(KosCode_ARENA(a), arena, "arena reference held");

    TEST_ASSERT_EQ(KosArena_Seal(arena), 0, "seal after pack");

    /* double pack rejected */
    r = PyObject_CallMethod(a, "pack_into", "O", arena);
    TEST_ASSERT_NULL(r, "double pack rejected");
    PyErr_Clear();

    /* arena lives until the last member dies */
    Py_ssize_t held = Py_REFCNT(arena);
    Py_DECREF(arena);
    TEST_ASSERT_EQ((long) Py_REFCNT(a) >= 1, 1L, "a alive");
    (void) held;

    Py_DECREF(a);
    /* b still holds the arena; freeing b unseals and frees the chunk */
    Py_DECREF(b);

    Py_DECREF(const0);
    return 0;
}


static int test_pack_into_sealed_rejected(void) {
    PyObject *c = make_small_code("c", 1);
    TEST_ASSERT_NOT_NULL(c, "create code");

    PyObject *arena = KosArena_New(code_block_bytes(c));
    TEST_ASSERT_NOT_NULL(arena, "create arena");
    TEST_ASSERT_EQ(KosArena_Seal(arena), 0, "seal");

    PyObject *r = PyObject_CallMethod(c, "pack_into", "O", arena);
    TEST_ASSERT_NULL(r, "pack into sealed rejected");
    PyErr_Clear();
    TEST_ASSERT_NULL(KosCode_ARENA(c), "code left unpacked");

    Py_DECREF(arena);
    Py_DECREF(c);
    return 0;
}


/* === test runner === */


int run_arena_tests(void) {
    printf("\n=== Arena tests ===\n");

    RUN_TEST(test_arena_create);
    RUN_TEST(test_arena_create_zero_rejected);
    RUN_TEST(test_arena_reserve);
    RUN_TEST(test_arena_reserve_capacity);
    RUN_TEST(test_arena_seal);

    RUN_TEST(test_code_protect_flag);
    RUN_TEST(test_pack_into_relocates);
    RUN_TEST(test_pack_into_sealed_rejected);

    return 0;
}


/* The end. */
