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


#ifndef KOSKARI_MATCHER_H
#define KOSKARI_MATCHER_H


#include <Python.h>
#include <stdint.h>


/* === node kinds === */


typedef enum {
  KOS_MATCH_NODE_TEST = 0,
  KOS_MATCH_NODE_CAPTURE,
  KOS_MATCH_NODE_FORM,
  KOS_MATCH_NODE_GROUP,
  KOS_MATCH_NODE_ONEOF,
  KOS_MATCH_NODE_REPEAT,
  KOS_MATCH_NODE_OPTIONAL,
  KOS_MATCH_NODE_REST,
  KOS_MATCH_NODE_NOT,
  KOS_MATCH_NODE_KIND_COUNT
} KosMatchNodeKind;


typedef enum {
  KOS_MATCH_TEST_ANY = 0,
  KOS_MATCH_TEST_LITERAL,
  KOS_MATCH_TEST_TYPE,
  KOS_MATCH_TEST_PREDICATE,
  KOS_MATCH_TEST_KIND_COUNT
} KosMatchTestKind;


/* === sentinel index values === */


/* Used in slot/extern/const reference fields to mean "unset". */
#define KOS_MATCH_NO_INDEX  ((uint32_t) 0xFFFFFFFFu)

/* Used in width fields to mean "unbounded". */
#define KOS_MATCH_UNBOUNDED ((uint32_t) 0xFFFFFFFFu)


/* === node payload structs === */


typedef struct {
  uint16_t test_kind;
  uint16_t reserved;
  uint32_t ref_index;
} KosMatchTest;


typedef struct {
  uint32_t slot;
  uint32_t subnode;
  uint16_t hygiene_role;
  uint16_t reserved;
  uint32_t scope_slot;
} KosMatchCapture;


typedef struct {
  uint32_t subnode;
} KosMatchForm;


typedef struct {
  uint32_t first_child;
  uint32_t child_count;
} KosMatchGroup;


typedef struct {
  uint32_t first_child;
  uint32_t child_count;
} KosMatchOneof;


typedef struct {
  uint32_t subnode;
} KosMatchRepeat;


typedef struct {
  uint32_t subnode;
} KosMatchOptional;


typedef struct {
  uint32_t slot;
  uint32_t reserved_suffix_min;
} KosMatchRest;


typedef struct {
  uint32_t subnode;
} KosMatchNot;


typedef struct {
  uint16_t kind;
  uint16_t flags;
  uint32_t min_width;
  uint32_t max_width;
  union {
    KosMatchTest     test;
    KosMatchCapture  capture;
    KosMatchForm     form;
    KosMatchGroup    group;
    KosMatchOneof    oneof;
    KosMatchRepeat   repeat;
    KosMatchOptional optional;
    KosMatchRest     rest;
    KosMatchNot      not_;
  } as;
} KosMatchNode;


/* === Matcher object === */


typedef struct KosMatcher {
  PyObject_HEAD

  PyObject *slots;       /* tuple[str], capture slot names */
  PyObject *externs;     /* tuple[str], extern parameter names */
  PyObject *consts;      /* tuple[object], literal/sub-matcher consts */
  PyObject *meta;        /* dict, hygiene/provenance metadata */

  KosMatchNode *nodes;
  Py_ssize_t node_count;

  uint32_t *child_indices;
  Py_ssize_t child_count;

  uint32_t entry;
} KosMatcher;


extern PyTypeObject KosMatcherType;


#define KosMatcher_Check(obj)                                   \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosMatcherType))

#define KosMatcher_CheckExact(obj)              \
  ((obj) && ((obj)->ob_type == &KosMatcherType))


/* === C-internal builder ===
   The builder is the canonical low-level construction surface for a
   KosMatcher in Phase 1. Callers append slots, externs, consts, child
   indices, and nodes (bottom-up; child references must resolve to lower
   indices), and then finalize. Finalize runs full validation (widths,
   slot/extern/const range checks, REPEAT min-width, ≤1 REST per branch,
   post-REST determinacy, no captures inside NOT) and constructs the
   immutable KosMatcher.

   On any error the builder records a Python exception and the call
   returns -1 (or NULL for KosMatcherBuilder_Finalize). The builder
   should be freed with KosMatcherBuilder_Free in either case.
*/


typedef struct KosMatcherBuilder KosMatcherBuilder;


KosMatcherBuilder *KosMatcherBuilder_New(void);

void KosMatcherBuilder_Free(KosMatcherBuilder *builder);

/**
   Number of nodes appended to the builder so far.
 */
Py_ssize_t KosMatcherBuilder_NodeCount(KosMatcherBuilder *builder);

/**
   Number of child indices appended to the builder so far.
 */
Py_ssize_t KosMatcherBuilder_ChildCount(KosMatcherBuilder *builder);

/**
   Append a capture slot name. Returns the new slot index, or -1 on
   error. The builder takes a new reference to name.
 */
int KosMatcherBuilder_AddSlot(
    KosMatcherBuilder *builder,
    PyObject *name);

/**
   Append an extern parameter name. Returns the new extern index, or -1
   on error. The builder takes a new reference to name.
 */
int KosMatcherBuilder_AddExtern(
    KosMatcherBuilder *builder,
    PyObject *name);

/**
   Append a constant value. Returns the new const index, or -1 on
   error. The builder takes a new reference to value.
 */
int KosMatcherBuilder_AddConst(
    KosMatcherBuilder *builder,
    PyObject *value);

/**
   Append a child index for use by GROUP/ONEOF first_child/child_count
   slices. Returns the offset of the appended entry, or -1 on error.
 */
int KosMatcherBuilder_AppendChildIndex(
    KosMatcherBuilder *builder,
    uint32_t child_node_index);

/**
   Append a node. Caller fills KosMatchNode kind and payload; min_width
   and max_width are computed by the builder during finalize. The
   builder rejects appends whose subnode/first_child references the
   current count or higher (forward refs).

   Returns the index of the appended node, or -1 on error.
 */
int KosMatcherBuilder_AppendNode(
    KosMatcherBuilder *builder,
    const KosMatchNode *node);

/**
   Set the meta dict. The builder takes a new reference to meta. Pass
   NULL to clear; finalize defaults a NULL meta to an empty dict.
 */
int KosMatcherBuilder_SetMeta(
    KosMatcherBuilder *builder,
    PyObject *meta);

/**
   Run full validation, build, and return a new KosMatcher (a new
   reference). On validation failure, sets a Python exception and
   returns NULL. The builder should still be freed afterward.
 */
PyObject *KosMatcherBuilder_Finalize(
    KosMatcherBuilder *builder,
    uint32_t entry);


/* === unit lifecycle === */


int koskari_matcher_init(PyObject *mod);
int koskari_matcher_clear(PyObject *mod);


#endif


// The end.
