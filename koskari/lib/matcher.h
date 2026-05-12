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
  KOS_MATCH_NODE_MATCH,
  KOS_MATCH_NODE_GROUP,
  KOS_MATCH_NODE_ONEOF,
  KOS_MATCH_NODE_REPEAT,
  KOS_MATCH_NODE_OPTIONAL,
  KOS_MATCH_NODE_REST,
  KOS_MATCH_NODE_NOT,
  KOS_MATCH_NODE_CLASS,
  KOS_MATCH_NODE_FIELD,
  KOS_MATCH_NODE_KIND_COUNT
} KosMatchNodeKind;


typedef enum {
  KOS_MATCH_FIELD_POSITIONAL = 0,
  KOS_MATCH_FIELD_KEYWORD,
  KOS_MATCH_FIELD_KIND_COUNT
} KosMatchFieldKind;


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
} KosMatchMatch;


typedef struct {
  uint32_t first_child;
} KosMatchGroup;


typedef struct {
  uint32_t first_branch;
} KosMatchOneof;


typedef struct {
  uint32_t subnode;
  /**
   * Minimum successful iterations (0 allows zero matches; the CAPTURE
   * wrapping REPEAT still binds an empty list when min_count is 0).
   */
  uint32_t min_count;
} KosMatchRepeat;


typedef struct {
  uint32_t subnode;
} KosMatchOptional;


typedef struct {
  /**
   * Minimum spine elements this REST must consume from the current
   * subject (0 allows an empty remainder; bind nil for KosPair, () for
   * sequence subjects). Binding a name requires CAPTURE wrapping REST.
   */
  uint32_t min_tail;
} KosMatchRest;


typedef struct {
  uint32_t subnode;
} KosMatchNot;


typedef struct {
  uint32_t type_extern;   /* index into matcher->externs / extern_env */
  uint32_t first_field;   /* head of FIELD chain; NO_INDEX if empty */
} KosMatchClass;


typedef struct {
  uint16_t field_kind;    /* KosMatchFieldKind */
  uint16_t reserved;
  uint32_t subnode;       /* width-1 sub-matcher for the projected attr */
  uint32_t name_const;    /* index into matcher->consts for KEYWORD;
                             KOS_MATCH_NO_INDEX for POSITIONAL */
} KosMatchField;


typedef struct {
  uint16_t kind;
  uint16_t flags;
  uint32_t next;         /* next sibling in GROUP, next branch in ONEOF,
                            next FIELD in a CLASS chain, otherwise
                            KOS_MATCH_NO_INDEX */
  uint32_t min_width;
  uint32_t max_width;
  union {
    KosMatchTest     test;
    KosMatchCapture  capture;
    KosMatchForm     form;
    KosMatchMatch     match;
    KosMatchGroup    group;
    KosMatchOneof    oneof;
    KosMatchRepeat   repeat;
    KosMatchOptional optional;
    KosMatchRest     rest;
    KosMatchNot      not_;
    KosMatchClass    class_;
    KosMatchField    field;
  } as;
} KosMatchNode;


/* === Matcher object === */


typedef struct KosMatcher {
  PyObject_VAR_HEAD      /* ob_size carries node_count */

  PyObject *slots;       /* tuple[str], capture slot names */
  PyObject *externs;     /* tuple[str], extern parameter names */
  PyObject *consts;      /* tuple[object], literal/sub-matcher consts */

  uint32_t entry;

  KosMatchNode nodes[];  /* trailing flexible array; tp_itemsize */
} KosMatcher;


extern PyTypeObject KosMatcherType;


#define KosMatcher_Check(obj)                                   \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosMatcherType))

#define KosMatcher_CheckExact(obj)              \
  ((obj) && ((obj)->ob_type == &KosMatcherType))


/* === MatchResult object === */


typedef struct KosMatchResult {
  PyObject_HEAD
  PyObject *captures;  /* dict[str, value] */
  int matched;
} KosMatchResult;


extern PyTypeObject KosMatchResultType;


#define KosMatchResult_Check(obj)                                       \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosMatchResultType))


/* === Datum (matched value + optional source position) === */


typedef struct KosDatum {
  PyObject_HEAD
  PyObject *datum;
  PyObject *position;
  PyObject *binding;  /* capture slot name (str) or Py_None */
} KosDatum;


extern PyTypeObject KosDatumType;


#define KosDatum_Check(obj)                                       \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosDatumType))


/* === subject adapter === */


typedef struct KosMatchSubject KosMatchSubject;


typedef struct {
  Py_ssize_t (*length)(KosMatchSubject *subject);
  PyObject *(*get)(KosMatchSubject *subject, Py_ssize_t index);
  PyObject *(*tail)(KosMatchSubject *subject, Py_ssize_t start);
  PyObject *(*slice)(
      KosMatchSubject *subject,
      Py_ssize_t start,
      Py_ssize_t end);
  void (*release)(KosMatchSubject *subject);
} KosMatchSubjectVTable;


struct KosMatchSubject {
  const KosMatchSubjectVTable *vt;
  PyObject *backing;     /* ref-counted backing object (tuple or pair) */
  PyObject **kp_cars;    /* KosPair adapter only: borrowed car ptrs */
  PyObject **kp_tails;   /* KosPair adapter only: each tails[i] is the
                            pair-or-nil at position i (borrowed) */
  Py_ssize_t cached_length;
};


/**
   Initialize a subject from any Python sequence-like object. On
   success returns 0 and the subject must be released with
   KosMatchSubject_Release. On failure returns -1 with a Python
   exception set.
 */
int KosMatchSubject_FromPySequence(
    PyObject *seq,
    KosMatchSubject *out);

/**
   Initialize a subject from a KosPair root (or KosNil for the empty
   subject). Walks the cons spine once at construction time and caches
   per-position car and pair pointers so length/get/tail are O(1).
   Returns 0 on success; -1 with a Python exception on failure
   (improper list, recursive list, etc.).
 */
int KosMatchSubject_FromKosPair(
    PyObject *pair_or_nil,
    KosMatchSubject *out);


void KosMatchSubject_Release(KosMatchSubject *subject);


/* === engine entry === */


/**
   Run the matcher against the subject with the supplied extern
   environment (PyTuple aligned to matcher->externs, or NULL when no
   externs are required). Returns a new KosMatchResult reference on
   success. On invalid invocation (missing/misaligned extern tuple, or
   internal error) returns NULL with a Python exception set. Match
   failure is not an error: it returns a KosMatchResult with
   matched=False.
 */
PyObject *KosMatcher_Match(
    KosMatcher *matcher,
    KosMatchSubject *subject,
    PyObject *extern_env);


/**
   Borrowed reference to the mutable matcher extern-defaults mapping.
   Lazily allocates and inserts baseline entries on first call.
 */
PyObject *KosMatcher_ExternDefaults(void);


/**
   Discard the singleton defaults dict and rebuild from baseline; returns
   a borrowed reference to the fresh dict (same object identity rules
   as KosMatcher_ExternDefaults).
 */
PyObject *KosMatcher_ResetExternDefaults(void);


/* === C-internal builder ===
   The builder is the canonical low-level construction surface for a
   KosMatcher. Callers append slots, externs, consts, and nodes
   (bottom-up; child references must resolve to lower indices), then
   finalize. Container kinds (GROUP, ONEOF) are appended through
   dedicated entry points that take their child/branch indices and
   stitch them together via the intrusive next links on KosMatchNode.
   Finalize runs full validation (widths, slot/extern/const range
   checks, REPEAT min-width, ≤1 REST per branch, post-REST determinacy,
   no captures inside NOT) and constructs the immutable KosMatcher.

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
   Append a non-container node. Caller fills KosMatchNode kind and
   payload; min_width, max_width, and next are filled by the builder.
   The builder rejects appends whose subnode/scope_slot references the
   current count or higher (forward refs).

   Container kinds (MATCH, GROUP, ONEOF) are not accepted here; use
   KosMatcherBuilder_AppendMatch / KosMatcherBuilder_AppendGroup /
   KosMatcherBuilder_AppendOneof instead.

   Returns the index of the appended node, or -1 on error.
 */
int KosMatcherBuilder_AppendNode(
    KosMatcherBuilder *builder,
    const KosMatchNode *node);

/**
   Append a MATCH node (sequential spine, flat capture dict). Children
   are stitched via next links; first_child is the head index.

   Returns the index of the new MATCH node, or -1 on error.
 */
int KosMatcherBuilder_AppendMatch(
    KosMatcherBuilder *builder,
    const uint32_t *children,
    Py_ssize_t count);

/**
   Append a GROUP node: children are stitched like MATCH. Inner captures
   write into a fresh sub-dict; bind that dict with CAPTURE(slot, GROUP(...)).

   Returns the index of the new GROUP node, or -1 on error.
 */
int KosMatcherBuilder_AppendGroup(
    KosMatcherBuilder *builder,
    const uint32_t *children,
    Py_ssize_t count);

/**
   Append a ONEOF node whose branches are the supplied node indices in
   order. Behaves like KosMatcherBuilder_AppendMatch but the chain
   indicates branch order rather than sibling sequence.

   Returns the index of the new ONEOF node, or -1 on error.
 */
int KosMatcherBuilder_AppendOneof(
    KosMatcherBuilder *builder,
    const uint32_t *branches,
    Py_ssize_t count);

/**
   Append a positional FIELD node referencing an existing width-1
   sub-matcher. Forward references are rejected. The FIELD has no
   stored attribute name; its position in the parent CLASS's chain
   determines its index into the runtime-resolved __match_args__.

   Returns the index of the new FIELD node, or -1 on error.
 */
int KosMatcherBuilder_AppendFieldPositional(
    KosMatcherBuilder *builder,
    uint32_t subnode);

/**
   Append a keyword FIELD node referencing an existing width-1
   sub-matcher and an attribute name stored in the matcher's consts
   pool. The caller is responsible for having already added the
   attribute name (a Python str) to the const pool via
   KosMatcherBuilder_AddConst; pass the resulting index as
   name_const. Range-checks name_const against the current consts
   list size; type-checks (PyUnicode_Check) of the referenced const
   value happen at finalize alongside other index validation.

   Returns the index of the new FIELD node, or -1 on error.
 */
int KosMatcherBuilder_AppendFieldKeyword(
    KosMatcherBuilder *builder,
    uint32_t subnode,
    uint32_t name_const);

/**
   Append a CLASS node whose fields are the supplied FIELD-node
   indices in order. Behaves like KosMatcherBuilder_AppendMatch but
   stitches a chain through FIELD `next` links: positional FIELDs
   must precede any keyword FIELDs (validator enforces this at
   finalize). type_extern is range-checked at finalize against the
   matcher's extern tuple; each fields[i] must reference a node
   already appended whose kind is KOS_MATCH_NODE_FIELD with
   `next == NO_INDEX`, with no duplicates.

   Returns the index of the new CLASS node, or -1 on error.
 */
int KosMatcherBuilder_AppendClass(
    KosMatcherBuilder *builder,
    uint32_t type_extern,
    const uint32_t *fields,
    Py_ssize_t count);

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
