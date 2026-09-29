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
  KOS_MATCH_NODE_SAME,
  KOS_MATCH_NODE_APP,
  KOS_MATCH_NODE_MAP,
  KOS_MATCH_NODE_MAP_ENTRY,
  KOS_MATCH_NODE_KIND_COUNT
} KosMatchNodeKind;


typedef enum {
  KOS_MATCH_FIELD_POSITIONAL = 0,
  KOS_MATCH_FIELD_KEYWORD,
  KOS_MATCH_FIELD_KIND_COUNT
} KosMatchFieldKind;


typedef enum {
  KOS_MATCH_MAPKEY_CONST = 0,   /* key_ref indexes matcher->consts */
  KOS_MATCH_MAPKEY_EXTERN,      /* key_ref indexes externs / extern_env */
  KOS_MATCH_MAPKEY_KIND_COUNT
} KosMatchMapKeyKind;


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
  uint32_t num_positional; /* POSITIONAL FIELDs on the chain, counted
                              by the builder; 0 skips the
                              __match_args__ lookup at match time */
} KosMatchClass;


typedef struct {
  uint16_t field_kind;    /* KosMatchFieldKind */
  uint16_t reserved;
  uint32_t subnode;       /* width-1 sub-matcher for the projected attr */
  uint32_t name_const;    /* index into matcher->consts for KEYWORD;
                             KOS_MATCH_NO_INDEX for POSITIONAL */
} KosMatchField;


typedef struct {
  uint32_t slot;
  uint32_t depth;
} KosMatchSame;


typedef struct {
  uint32_t fn_extern;     /* index into matcher->externs / extern_env:
                             the synchronous extractor callable */
  uint32_t subnode;       /* width-1 sub-matcher for the call result */
} KosMatchApp;


typedef struct {
  uint32_t first_entry;   /* head of a MAP_ENTRY chain via next, or
                             KOS_MATCH_NO_INDEX for a bare mapping
                             check */
} KosMatchMap;


typedef struct {
  uint16_t key_kind;      /* KosMatchMapKeyKind */
  uint16_t reserved;
  uint32_t key_ref;       /* const or extern index, per key_kind */
  uint32_t subnode;       /* width-1 sub-matcher for the looked-up
                             value; KOS_MATCH_NO_INDEX = presence only */
} KosMatchMapEntry;


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
    KosMatchSame     same;
    KosMatchApp      app;
    KosMatchMap      map;
    KosMatchMapEntry   map_entry;
  } as;
} KosMatchNode;


/* === Matcher object === */


/* The shape a subject must have for the matcher's first element test
   to pass, read off the entry at finalize: the test the engine would
   run first, so a site can run it before any engine setup and skip
   the arm when it fails. */
typedef enum {
  KOS_MATCH_ROOT_NONE = 0,   /* no cheap first test; run the engine */
  KOS_MATCH_ROOT_TEST_TYPE,  /* a type test: isinstance of extern
                                root_ref on the peeled element */
  KOS_MATCH_ROOT_CLASS,      /* a class pattern: isinstance of extern
                                root_ref on the element as it is */
  KOS_MATCH_ROOT_SEQUENCE,   /* the sequence-pattern gate */
  KOS_MATCH_ROOT_MAPPING,    /* the mapping-pattern gate */
} KosMatchRootKind;


typedef struct KosMatcher {
  PyObject_VAR_HEAD      /* ob_size carries node_count */

  PyObject *slots;       /* tuple[str], capture slot names */
  PyObject *externs;     /* tuple[str], extern parameter names */
  PyObject *consts;      /* tuple[object], literal/sub-matcher consts */

  uint32_t entry;
  uint32_t root_kind;    /* KosMatchRootKind, set at finalize */
  uint32_t root_ref;     /* extern index for the isinstance roots */

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


/* === DatumFrame (capture dict + source provenance) === */


typedef struct KosDatumFrame {
  PyDictObject dict;
  PyObject *source;
  PyObject *position;
} KosDatumFrame;


extern PyTypeObject KosDatumFrameType;


#define KosDatumFrame_Check(obj)                                       \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosDatumFrameType))


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
  PyObject **kp_cars;    /* KosPair adapter only: owned car refs */
  PyObject **kp_tails;   /* KosPair adapter only: each tails[i] is the
                            pair-or-nil at position i (owned) */
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


/**
   Initialize a length-1 subject over a single value without
   materializing a backing tuple. The value is the sole spine element,
   exactly as if it had been wrapped in a 1-tuple and passed through
   ``KosMatchSubject_FromPySequence``. Always succeeds; the subject
   must be released with KosMatchSubject_Release.
 */
void KosMatchSubject_FromSingle(
    PyObject *value,
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
   Allocation-lean core of ``KosMatcher_Match``: run the matcher and
   report the outcome without building a MatchResult. ``extern_env``
   is a borrowed value array aligned to the matcher's extern table
   (``n_extern_env`` must equal the table length; NULL/0 when the
   table is empty) -- a tuple's item array works directly. Returns 1
   on a match with ``*out_caps`` set to a new reference to the capture
   frame; 0 on no match with ``*out_caps`` set to NULL and nothing
   allocated; -1 with a Python exception set on error.
 */
int KosMatcher_Run(
    KosMatcher *matcher,
    KosMatchSubject *subject,
    PyObject *const *extern_env,
    Py_ssize_t n_extern_env,
    PyObject **out_caps);


/**
   Vectorcall-style entry to ``KosMatcher_Run``: ``ext_values`` is a
   borrowed value array (a stack segment works as-is) named by the
   aligned ``ext_names`` tuple. Each extern-table entry resolves from
   the named values first, then the matcher's inbuilt extern defaults;
   a genuine miss raises the matcher's missing-binding TypeError. When
   the names cover the whole extern table in order the value array is
   used as the env directly with no allocation. Same returns as
   ``KosMatcher_Run``.
 */
int KosMatcher_RunVectorcall(
    KosMatcher *matcher,
    KosMatchSubject *subject,
    PyObject *const *ext_values,
    PyObject *ext_names,
    PyObject **out_caps);


/**
   Number of capture slots in the matcher's slot table; the length of
   the array the slot-array entries below fill.
 */
#define KosMatcher_SLOT_COUNT(m)  PyTuple_GET_SIZE(((KosMatcher *) (m))->slots)


/**
   Slot-array entries a caller keeps in a stack buffer before spilling
   to the heap; the common slot table is far smaller.
 */
#define KOS_MATCH_SLOTS_INLINE 8


/**
   Frame-free core of ``KosMatcher_Run``: same arguments, but the
   captures land in ``out_slots``, the caller's array of
   ``KosMatcher_SLOT_COUNT`` entries aligned to the slot table. On a
   match (1) each slot that fired holds a new reference and the rest
   are NULL; on no match (0) or error (-1, exception set) every entry
   is NULL and nothing is owned, the one exception being a call
   rejected for a NULL matcher or array, which touches nothing. The
   array need not be cleared beforehand. No dict or frame is built.
 */
int KosMatcher_RunSlots(
    KosMatcher *matcher,
    KosMatchSubject *subject,
    PyObject *const *extern_env,
    Py_ssize_t n_extern_env,
    PyObject **out_slots);


/**
   ``KosMatcher_RunVectorcall`` with the ``KosMatcher_RunSlots``
   result contract: same extern resolution, captures into the caller's
   slot array.
 */
int KosMatcher_RunSlotsVectorcall(
    KosMatcher *matcher,
    KosMatchSubject *subject,
    PyObject *const *ext_values,
    PyObject *ext_names,
    PyObject **out_slots);


/**
   Thaw a frozen matcher 6-tuple (`("MATCHER", slots, externs, consts,
   nodes, entry)`) into a native Matcher. Same reconstruction as the
   `Matcher.from_tuple` classmethod. Returns a new reference on success;
   NULL with a Python exception set on an invalid tuple.
 */
PyObject *KosMatcher_FromTuple(PyObject *tuple);


/**
   Run matchers in sequence order against one subject until the first
   successful match. ``kwargs`` is optional (NULL) and follows the same
   extern resolution rules as ``Matcher.match`` for each arm.

   Returns ``0`` on success: ``*out_result`` is a new ``MatchResult``
   reference and ``*out_index`` is the winning index in ``matchers``.
   Returns ``1`` if no arm matched (outs unchanged; caller should
   pre-clear). Returns ``-1`` with a Python exception set (outs
   unchanged).
 */
int KosMatcher_MatchSelect(
    PyObject *matchers,
    PyObject *subject,
    PyObject *kwargs,
    PyObject **out_result,
    Py_ssize_t *out_index);


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


/**
   The version of the extern defaults: a number that changes on every
   reset and every mutation of the dict, so an env resolved under one
   version is what resolution yields while the version holds. The
   compat header decides its source: the dict's own version tag, or a
   counter bumped by a dict watcher where the tag is deprecated
   (KOS_MATCHER_DEFAULTS_WATCHER).
 */
uint64_t KosMatcher_ExternDefaultsVersion(void);

/**
   Resolve every extern of m from the defaults into a new tuple, in
   extern-table order. Returns the tuple, or NULL with an exception
   set when a default is missing.
 */
PyObject *KosMatcher_ResolveDefaultEnv(KosMatcher *m);

/**
   Whether names covers m's extern table in order, so a value array
   stacked under names is the env as-is. Names may be NULL or empty.
   Returns 1, 0, or -1 with an exception set.
 */
int KosMatcher_ExternNamesAligned(KosMatcher *m, PyObject *names);

/**
   The engine's first element test run on the bare subject with the
   env in hand: the isinstance test, the sequence gate, or the mapping
   gate that match_node would apply first, and nothing when the entry
   has no such test. Returns 1 when the subject cannot match, 0 when
   the engine must run, -1 with an exception set.
 */
int KosMatcher_RootReject(
    KosMatcher *m, PyObject *subject, PyObject *const *extern_env);


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
   The builder rejects appends whose subnode references the current
   count or higher (forward refs).

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
   Append a MAP_ENTRY node: one mapping-pattern entry. key_kind selects
   whether key_ref indexes the consts pool (a literal key) or the
   extern table (a key resolved from the namespace at match time);
   subnode is an existing width-1 sub-matcher for the looked-up value,
   or KOS_MATCH_NO_INDEX for a presence-only check.

   Returns the index of the new MAP_ENTRY node, or -1 on error.
 */
int KosMatcherBuilder_AppendMapEntry(
    KosMatcherBuilder *builder,
    uint16_t key_kind,
    uint32_t key_ref,
    uint32_t subnode);

/**
   Append a MAP node whose entries are the supplied MAP_ENTRY-node indices
   in order, stitched into a chain through their `next` links (same
   contract as KosMatcherBuilder_AppendClass over FIELDs).

   Returns the index of the new MAP node, or -1 on error.
 */
int KosMatcherBuilder_AppendMap(
    KosMatcherBuilder *builder,
    const uint32_t *entries,
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
