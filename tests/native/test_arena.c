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
   Memory tests for the Arena and CodeAllocator types and the Code
   protect flag

   Tests arena creation, reservation arithmetic, release of the newest
   reservation, seal, and code allocator placement and sealing,
   including the arena ownership handoff on Code deallocation.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"

#include <unistd.h>


/* === helpers === */


static PyObject *make_nspec(const char *name) {
    PyObject *nm = PyUnicode_FromString(name);
    PyObject *empty = PyTuple_New(0);
    PyObject *nspec = KosNamespaceSpec_New(nm, empty, empty, NULL);
    Py_DECREF(nm);
    Py_DECREF(empty);
    return nspec;
}


static PyObject *make_placed_code(
        const char *name,
        int protect,
        PyObject *allocator) {
    /*
      Build a small Code: CONST <name>, RETURN 1, placed by allocator
      when one is given.
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

    PyObject *code = KosCode_NewWithAllocator(nspec, instr, NULL, NULL,
                                              protect, 1, allocator);
    Py_DECREF(nspec);
    Py_DECREF(instr);
    return code;
}


static PyObject *make_small_code(const char *name, int protect) {
    return make_placed_code(name, protect, NULL);
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
    TEST_ASSERT_EQ((long) KosArena_SIZE(arena), (long) KosArena_ChunkUnit(),
                   "arena rounded to one chunk unit");
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

    /* nothing was reserved, so the seal unmapped the chunk whole */
    TEST_ASSERT_NULL(KosArena_BASE(arena), "empty chunk unmapped");
    TEST_ASSERT_EQ((long) KosArena_SIZE(arena), 0L, "empty chunk has no size");

    TEST_ASSERT_EQ(KosArena_Seal(arena), -1, "double seal rejected");
    PyErr_Clear();

    void *p = NULL;
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 8, &p), -1,
                   "reserve after seal rejected");
    PyErr_Clear();

    /* dealloc of a sealed arena unmaps cleanly */
    Py_DECREF(arena);
    return 0;
}


static int test_arena_release(void) {
    PyObject *arena = KosArena_New(256);
    TEST_ASSERT_NOT_NULL(arena, "create arena");

    void *a = NULL;
    void *b = NULL;
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 40, &a), 0, "reserve a");
    KosArena_Complete(arena);
    size_t after_a = KosArena_FILL(arena);
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 40, &b), 0, "reserve b");
    KosArena_Complete(arena);

    /* an older reservation is not returned */
    KosArena_Release(arena, a, 40);
    TEST_ASSERT(KosArena_FILL(arena) > after_a, "older kept");

    /* the newest is, back to its aligned start, and its space is handed
       out again */
    KosArena_Release(arena, b, 40);
    TEST_ASSERT_EQ((long) KosArena_FILL(arena),
                   (long) (((after_a + KOS_ARENA_UNIT - 1) / KOS_ARENA_UNIT)
                           * KOS_ARENA_UNIT),
                   "newest returned");

    void *c = NULL;
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 40, &c), 0, "reserve c");
    KosArena_Complete(arena);
    TEST_ASSERT_SAME(c, b, "space reused");

    /* releases cascade newest first: c, then a, whose exact end lies
       below the boundary c's release rewound to */
    KosArena_Release(arena, c, 40);
    KosArena_Release(arena, a, 40);
    TEST_ASSERT_EQ((long) KosArena_FILL(arena), 0L, "every block returned");

    TEST_ASSERT_EQ(KosArena_Reserve(arena, 40, &c), 0, "reserve again");
    KosArena_Complete(arena);

    /* nothing is returned to a sealed arena */
    size_t fill = KosArena_FILL(arena);
    TEST_ASSERT_EQ(KosArena_Seal(arena), 0, "seal");
    KosArena_Release(arena, c, 40);
    TEST_ASSERT_EQ((long) KosArena_FILL(arena), (long) fill,
                   "sealed arena keeps its fill");

    Py_DECREF(arena);
    return 0;
}



static int test_arena_seal_waits_for_writing(void) {
    PyObject *arena = KosArena_New(256);
    TEST_ASSERT_NOT_NULL(arena, "create arena");

    /* a reservation being written refuses the seal; completing it
       lets the seal through */
    void *a = NULL;
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 40, &a), 0, "reserve a");
    TEST_ASSERT_EQ(KosArena_Seal(arena), -1, "seal refused under a");
    TEST_ASSERT(PyErr_ExceptionMatches(PyExc_ValueError), "ValueError");
    PyErr_Clear();
    TEST_ASSERT(! KosArena_SEALED(arena), "still open");

    /* a reservation given back is completed first, as its releaser
       does */
    void *b = NULL;
    TEST_ASSERT_EQ(KosArena_Reserve(arena, 40, &b), 0, "reserve b");
    KosArena_Complete(arena);
    KosArena_Release(arena, b, 40);
    TEST_ASSERT_EQ(KosArena_Seal(arena), -1, "seal still refused under a");
    PyErr_Clear();

    KosArena_Complete(arena);
    TEST_ASSERT_EQ(KosArena_Seal(arena), 0, "seal once written");
    TEST_ASSERT(KosArena_SEALED(arena), "sealed");

    Py_DECREF(arena);
    return 0;
}

/* === code allocator tests === */


static int test_code_allocator_reserve(void) {
    PyObject *alloc = KosCodeAllocator_New();
    TEST_ASSERT_NOT_NULL(alloc, "create allocator");

    size_t unit = KosArena_ChunkUnit();
    void *a = NULL;
    void *b = NULL;
    void *c = NULL;
    PyObject *chunk_a = NULL;
    PyObject *chunk_b = NULL;
    PyObject *chunk_c = NULL;

    /* most of a chunk, then more than its tail, then a small block
       that fits behind the first */
    TEST_ASSERT_EQ(KosCodeAllocator_Reserve(alloc, unit - 128, &a, &chunk_a),
                   0, "reserve a");
    KosArena_Complete(chunk_a);
    TEST_ASSERT_EQ(KosCodeAllocator_Reserve(alloc, 256, &b, &chunk_b),
                   0, "reserve b");
    KosArena_Complete(chunk_b);
    TEST_ASSERT(chunk_b != chunk_a, "b opens a second chunk");
    TEST_ASSERT_EQ(KosCodeAllocator_Reserve(alloc, 64, &c, &chunk_c),
                   0, "reserve c");
    KosArena_Complete(chunk_c);
    TEST_ASSERT_SAME(chunk_c, chunk_a, "c fits the first chunk");

    /* an oversize block gets whole chunk units */
    void *d = NULL;
    PyObject *chunk_d = NULL;
    TEST_ASSERT_EQ(KosCodeAllocator_Reserve(alloc, unit * 2 + 1, &d,
                                            &chunk_d),
                   0, "reserve oversize");
    KosArena_Complete(chunk_d);
    TEST_ASSERT_EQ((long) KosArena_SIZE(chunk_d), (long) (unit * 3),
                   "oversize chunk is three units");

    PyObject *sealed = KosCodeAllocator_Seal(alloc);
    TEST_ASSERT_NOT_NULL(sealed, "seal");
    TEST_ASSERT_EQ((long) PyTuple_GET_SIZE(sealed), 3L, "three chunks");
    TEST_ASSERT(KosArena_SEALED(chunk_a), "chunk sealed");

    /* each sealed chunk is trimmed to the whole pages its code fills:
       the second holds 256 bytes, so it is one page */
    long page = sysconf(_SC_PAGE_SIZE);
    TEST_ASSERT_EQ((long) KosArena_SIZE(chunk_b), page,
                   "second chunk trimmed to one page");
    TEST_ASSERT((long) KosArena_SIZE(chunk_a) <= (long) unit,
                "first chunk no larger than the unit");
    TEST_ASSERT_EQ((long) KosArena_SIZE(chunk_a) % page, 0L,
                   "first chunk whole pages");
    TEST_ASSERT(KosArena_SIZE(chunk_a) >= KosArena_FILL(chunk_a),
                "first chunk holds its fill");
    Py_DECREF(sealed);

    TEST_ASSERT_EQ(KosCodeAllocator_Reserve(alloc, 8, &a, &chunk_a), -1,
                   "reserve after seal rejected");
    PyErr_Clear();
    TEST_ASSERT_NULL(KosCodeAllocator_Seal(alloc), "double seal rejected");
    PyErr_Clear();

    Py_DECREF(alloc);
    return 0;
}


static int test_code_allocator_seal_resumes(void) {
    PyObject *alloc = KosCodeAllocator_New();
    TEST_ASSERT_NOT_NULL(alloc, "create allocator");

    void *a = NULL;
    void *b = NULL;
    PyObject *chunk_a = NULL;
    PyObject *chunk_b = NULL;

    /* a chunk sealed ahead of the allocator stands in for one a failed
       seal left behind: reservations pass it by, and the allocator's
       seal takes the rest and closes */
    TEST_ASSERT_EQ(KosCodeAllocator_Reserve(alloc, 64, &a, &chunk_a),
                   0, "reserve a");
    KosArena_Complete(chunk_a);
    TEST_ASSERT_EQ(KosArena_Seal(chunk_a), 0, "seal a's chunk directly");

    TEST_ASSERT_EQ(KosCodeAllocator_Reserve(alloc, 64, &b, &chunk_b),
                   0, "reserve past a sealed chunk");
    KosArena_Complete(chunk_b);
    TEST_ASSERT(chunk_b != chunk_a, "b opens a second chunk");

    PyObject *sealed = KosCodeAllocator_Seal(alloc);
    TEST_ASSERT_NOT_NULL(sealed, "seal skips the sealed chunk");
    TEST_ASSERT_EQ((long) PyTuple_GET_SIZE(sealed), 2L, "two chunks");
    TEST_ASSERT(KosArena_SEALED(chunk_a), "first chunk sealed");
    TEST_ASSERT(KosArena_SEALED(chunk_b), "second chunk sealed");
    Py_DECREF(sealed);

    Py_DECREF(alloc);
    return 0;
}


static int test_code_allocator_places_code(void) {
    PyObject *alloc = KosCodeAllocator_New();
    TEST_ASSERT_NOT_NULL(alloc, "create allocator");

    PyObject *a = make_placed_code("a", 1, alloc);
    TEST_ASSERT_NOT_NULL(a, "create placed code a");
    PyObject *chunk = KosCode_ARENA(a);
    TEST_ASSERT_NOT_NULL(chunk, "a holds its chunk");
    TEST_ASSERT(! KosCode_PROTECTED(a), "placement ignores protect");
    TEST_ASSERT_EQ((long) KosCode_ALLOCATION(a),
                   (long) code_block_bytes(a), "allocation is exact");

    char *base = (char *) KosArena_BASE(chunk);
    TEST_ASSERT((char *) KosCode_CODE(a) >= base
                && (char *) KosCode_CODE(a) < base + KosArena_SIZE(chunk),
                "a lives in its chunk");

    /* the newest member returns its block when it dies */
    size_t fill = KosArena_FILL(chunk);
    PyObject *b = make_placed_code("b", 0, alloc);
    TEST_ASSERT_NOT_NULL(b, "create placed code b");
    TEST_ASSERT_SAME(KosCode_ARENA(b), chunk, "b shares the chunk");
    Py_DECREF(b);
    TEST_ASSERT_EQ((long) KosArena_FILL(chunk),
                   (long) (((fill + KOS_ARENA_UNIT - 1) / KOS_ARENA_UNIT)
                           * KOS_ARENA_UNIT),
                   "b's block returned to its aligned start");

    /* a member outlives the allocator, and frees its sealed chunk */
    PyObject *sealed = KosCodeAllocator_Seal(alloc);
    TEST_ASSERT_NOT_NULL(sealed, "seal");
    Py_DECREF(sealed);
    Py_DECREF(alloc);
    TEST_ASSERT(KosArena_SEALED(KosCode_ARENA(a)), "chunk sealed");
    Py_DECREF(a);

    return 0;
}


/* === Code protect tests === */


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


/* === test runner === */


int run_arena_tests(void) {
    printf("\n=== Arena tests ===\n");

    RUN_TEST(test_arena_create);
    RUN_TEST(test_arena_create_zero_rejected);
    RUN_TEST(test_arena_reserve);
    RUN_TEST(test_arena_reserve_capacity);
    RUN_TEST(test_arena_seal);
    RUN_TEST(test_arena_release);
    RUN_TEST(test_arena_seal_waits_for_writing);

    RUN_TEST(test_code_allocator_reserve);
    RUN_TEST(test_code_allocator_seal_resumes);
    RUN_TEST(test_code_allocator_places_code);

    RUN_TEST(test_code_protect_flag);

    return 0;
}


/* The end. */
