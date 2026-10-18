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

#include "hints.h"
#include "tracing.h"



/* === Datum (matched value + optional source position) === */


typedef struct KosDatum {
  PyObject_HEAD
  PyObject *datum;
  PyObject *position;
  PyObject *binding;  /* capture slot name (str) or Py_None */
} KosDatum;


extern PyTypeObject KosDatumType;


/* Datum is not a base type and has no subtype, so the check is one
   pointer compare */
#define KosDatum_Check(obj)                                       \
  ((obj) && Py_IS_TYPE((obj), &KosDatumType))


/* === DatumFrame (capture dict + source provenance) === */


typedef struct KosDatumFrame {
  PyDictObject dict;
  PyObject *source;
  PyObject *position;
} KosDatumFrame;


extern PyTypeObject KosDatumFrameType;


/* DatumFrame is not a base type and has no subtype, so the check is
   one pointer compare */
#define KosDatumFrame_Check(obj)                                       \
  ((obj) && Py_IS_TYPE((obj), &KosDatumFrameType))


/**
   A match cursor over one subject, by kind: a sequence cursor holds
   the sequence in seq, its length, and the index of the next element;
   a spine cursor holds the current cell in seq, the count of cells
   from the cell it was made at in length, and the cells pulled in
   index; a single-value cursor holds the value in seq with a length
   of one. reserve is the count of elements a bounded cursor leaves
   before its subject's end, zero for every other cursor.
   Opaque to Python and untracked. A move writes the object in place
   when its stack entry is the only holder and stores a new object at
   the entry otherwise, so a cloned segment keeps the position it took.
 */
typedef struct KosMatchCursor {
  PyObject_HEAD
  PyObject *seq;
  Py_ssize_t length;
  Py_ssize_t index;
  Py_ssize_t reserve;
  unsigned char kind;
} KosMatchCursor;


enum {
  KOS_MATCH_CURSOR = 0,
  KOS_MATCH_SPINE = 1,
  KOS_MATCH_SINGLE = 2,
};


extern PyTypeObject KosMatchCursorType;


#define KosMatchCursor_Check(obj)                                  \
  ((obj) && ((obj)->ob_type == &KosMatchCursorType))

#define KosMatchCursor_KIND(obj)  (((KosMatchCursor *) (obj))->kind)
#define KosMatchCursor_INDEX(obj)  (((KosMatchCursor *) (obj))->index)


/* The core types a pattern names by their bare spelling: a type test
   against one is an instruction carrying this ordinal in a RAW field,
   with no reference to resolve, and the check is the type walk the
   isinstance of the builtin would run. The ordinals are bytecode ABI,
   mirrored by the Python CoreType IntEnum; keep the order stable. */
typedef enum KosCoreType {
  KOS_CORE_PAIR = 0,
  KOS_CORE_SYMBOL,
  KOS_CORE_KEYWORD,
  KOS_CORE_INT,
  KOS_CORE_STR,
  KOS_CORE_TUPLE,
  KOS_CORE_LIST,
  KOS_CORE_COUNT   /* number of core types; also the invalid sentinel */
} KosCoreType;


/**
   The type test: elem read through Datum is an instance of type.
   1, 0, or -1 with an exception set.
 */
int KosMatch_TypeTest(PyObject *elem, PyObject *type);

/**
   The type test against a core type: elem read through Datum must be
   an instance of the type the ordinal names. 1 or 0; -1 with an
   exception set for an ordinal out of range.
 */
int KosMatch_CoreTypeTest(PyObject *elem, uint32_t core);

/**
   The count check of a CLASS with npos positional fields, run after
   its type test: cls.__match_args__ must be a tuple of at least npos
   names. match_args is that tuple, borrowed, when the caller holds it
   (see KosMatch_PlainMatchArgs), else NULL and it is read from cls. 0,
   or -1 with a TypeError set.
 */
int KosMatch_ClassArgs(
    PyObject *cls,
    Py_ssize_t npos,
    PyObject *match_args);

/**
   cls.__match_args__ when a plain lookup decides it: cls's metaclass
   is exactly type and its MRO holds an exact tuple under the name, so
   no descriptor or metaclass hook can answer otherwise. Borrowed from
   the class's dict, valid while the class's version tag is unchanged;
   NULL with no exception set otherwise.
 */
PyObject *KosMatch_PlainMatchArgs(PyObject *cls);

/**
   The literal test: elem read through Datum is lit by identity, a
   gensym whose base is lit, or equal to lit. 1, 0, or -1.
 */
int KosMatch_LiteralTest(PyObject *elem, PyObject *lit);

/**
   The attribute of elem that cls.__match_args__[pos] names, the tuple
   given or read as KosMatch_ClassArgs takes it. Returns 1 with *out a
   new reference, 0 when the attribute is missing, -1 with an
   exception set.
 */
int KosMatch_FieldPos(
    PyObject *elem,
    PyObject *cls,
    Py_ssize_t pos,
    PyObject *match_args,
    PyObject **out);

/**
   The attribute of elem that name names, under the same returns.
 */
int KosMatch_FieldKw(PyObject *elem, PyObject *name, PyObject **out);

/**
   The mapping gate on elem read through Datum. 1, 0, or -1.
 */
int KosMatch_MapTest(PyObject *elem);

/**
   The equality test of SAME: elem and the bound value prev read
   through Datum; a DatumFrame on either side misses; identity, then
   Py_EQ, with prev held across the comparison so a borrowed value
   may be passed. 1, 0, or -1.
 */
int KosMatch_SameTest(PyObject *elem, PyObject *prev);

/**
   The value under key in the mapping elem, probed without inserting.
   Returns 1 with *out a new reference, 0 when the key is absent, -1
   with an exception set.
 */
int KosMatch_Item(PyObject *elem, PyObject *key, PyObject **out);


/* === cursors === */


/* The unbounded maximum of a 16-bit width field, as MATCH_FITS reads
   it. */
#define KOS_MATCH_UNBOUNDED  0xFFFFu


/* A helper that moves a cursor takes held, the references the caller
   accounts for (1 for a cursor its stack entry alone holds), and
   returns the moved cursor: the cursor itself, moved in place and
   borrowed, when held counts every reference to it; else a new cursor
   at the new position, a new reference for the caller to store at the
   entry, so a cloned segment keeps the position it took. */


/**
   The copy a move of a shared cursor takes: a new cursor of cursor's
   kind, length and reserve at seq and index. New reference; NULL with
   an exception set.
 */
PyObject *KosMatchCursor_CopyAt(
    PyObject *cursor,
    PyObject *seq,
    Py_ssize_t index);


/**
   cursor moved to seq and index (see held above). NULL with an
   exception set.
 */
static inline PyObject *KosMatchCursor_MoveTo(
    PyObject *cursor,
    PyObject *seq,
    Py_ssize_t index,
    Py_ssize_t held) {

  KosMatchCursor *c = (KosMatchCursor *) cursor;

  if (unlikely(Py_REFCNT(c) != held))
    return KosMatchCursor_CopyAt(cursor, seq, index);

  if (c->seq != seq) {
    Py_INCREF(seq);
    Py_SETREF(c->seq, seq);
  }
  c->index = index;
  return cursor;
}


/**
   MATCH_DESCEND: the sequence gate on value read through Datum (a
   pair spine, a tuple, a list, or another sequence type; not a str,
   bytes, mapping or iterator), and a cursor over it. A pair spine is
   counted once here: a spine that loops raises ValueError, an
   improper one is a miss. A list is read at its live length by every
   later step. Returns 1 with *out_cursor a new reference, 0 on a
   miss, -1 with an exception set.
 */
int KosMatchCursor_Descend(PyObject *value, PyObject **out_cursor);

/**
   MATCH_SINGLE: a cursor over the one-element subject value, which
   is not read through Datum. New reference; NULL with an exception
   set.
 */
PyObject *KosMatchCursor_Single(PyObject *value);

/**
   The elements left between cursor's position and its end, less its
   reserve: the count of a spine, the live length of a list, the
   length the cursor recorded for any other sequence. Never negative.
 */
static inline Py_ssize_t KosMatchCursor_Remaining(PyObject *cursor) {
  KosMatchCursor *c = (KosMatchCursor *) cursor;
  Py_ssize_t length = c->length;

  if (c->kind == KOS_MATCH_CURSOR && PyList_Check(c->seq))
    length = PyList_GET_SIZE(c->seq);
  length -= c->reserve + c->index;
  return length > 0 ? length : 0;
}

/**
   MATCH_NEXT: pull one element and advance. The element is a new
   Datum over the cell's car read through Datum, with the cell's
   position, from a spine, and the item itself from a sequence or a
   single value. Returns 1 with *out_elem a new reference and
   *out_next the moved cursor (see held above), 0 at the end (no
   element left, or a spine cell that is not a pair: user code cut the
   spine ahead of the cursor), -1 with an exception set.
 */
int KosMatchCursor_Next(
    PyObject *cursor,
    Py_ssize_t held,
    PyObject **out_elem,
    PyObject **out_next);

/**
   MATCH_BOUND: a new cursor at cursor's position whose end is w
   elements before cursor's end. Returns 1 with *out_bound a new
   reference, 0 when fewer than w elements remain, -1 with an
   exception set.
 */
int KosMatchCursor_Bound(
    PyObject *cursor,
    Py_ssize_t w,
    PyObject **out_bound);

/**
   MATCH_UNBOUND: a new cursor at cursor's position with no reserve,
   ending at its subject's end. New reference; NULL with an exception
   set.
 */
PyObject *KosMatchCursor_Unbound(PyObject *cursor);


/**
   MATCH_SYNC: cursor moved to the position of to, a cursor over the
   same subject (see held above). NULL with an exception set.
 */
static inline PyObject *KosMatchCursor_Sync(
    PyObject *cursor,
    PyObject *to,
    Py_ssize_t held) {

  KosMatchCursor *t = (KosMatchCursor *) to;

  return KosMatchCursor_MoveTo(cursor, t->seq, t->index, held);
}


/**
   MATCH_TAIL: the remaining elements less w, and cursor advanced past
   them. The span over a sequence is a tuple of the items (a list read
   within its live length); over a single value, a tuple of the value
   or the empty tuple; over a spine, a Datum over the terminator when
   it is empty, a Datum over its first cell (the sub-spine) when it
   runs to the spine's end, else a tuple of a Datum per cell. Returns
   1 with *out_span a new reference and *out_next the moved cursor
   (see held above), 0 when fewer than w remain or the span is
   shorter than min, -1 with an exception set, ValueError for a spine
   user code cut inside the span.
 */
int KosMatchCursor_Tail(
    PyObject *cursor,
    Py_ssize_t w,
    Py_ssize_t min,
    Py_ssize_t held,
    PyObject **out_span,
    PyObject **out_next);


/* === marks === */


/* The slot capacity of a default mark, the size the freelist keeps. */
#define KOS_MATCH_MARK_SLOTS  16


/**
   A capture scope's mark: the position of the cursor it was made at
   (its kind, seq and index, as the cursor held them) and the scope's
   count slot entries, NULL for an unbound slot. A mark of up to
   KOS_MATCH_MARK_SLOTS slots is allocated at that capacity, a larger
   one at its count; a mark never resizes. Its position never changes.
   A write to its slots goes to a mark its stack entry alone holds:
   KosMatchMark_Writable gives the mark or a copy for the caller to
   store back at the entry, so a resumed continuation writes into its
   own copy. Opaque to Python and not subclassable; tracked by the
   collector when it has slots.
 */
typedef struct KosMatchMark {
  PyObject_VAR_HEAD
  PyObject *seq;
  Py_ssize_t index;
  Py_ssize_t count;
  unsigned char kind;
  PyObject *slots[1];
} KosMatchMark;


extern PyTypeObject KosMatchMarkType;


#define KosMatchMark_Check(obj)                                    \
  ((obj) && ((obj)->ob_type == &KosMatchMarkType))

/* The slot count, and slot i (borrowed; NULL when unbound) for
   MATCH_LOAD and MATCH_SAME. The caller keeps i below the count. */
#define KosMatchMark_COUNT(obj)    (((KosMatchMark *) (obj))->count)
#define KosMatchMark_SLOT(obj, i)  (((KosMatchMark *) (obj))->slots[(i)])

/* The position's index, for MATCH_SPAN's test for an empty span
   against a cursor's index. */
#define KosMatchMark_INDEX(obj)    (((KosMatchMark *) (obj))->index)


/* The freed default-capacity marks KosMatchMark_New takes from,
   chained through seq, and their count; matcher/mark.c frees into the
   chain. Every slot of a mark on it is NULL. */
extern KosMatchMark *kos_match_mark_free_list;
extern int kos_match_mark_free_count;


/**
   A mark at seq, index and kind with count unbound slots from the
   allocator, at the default capacity or at count when that is larger,
   every slot NULL; tracked when count is not zero. The path
   KosMatchMark_New takes when the freelist is empty or count is past
   the default capacity. New reference; NULL with an exception set,
   SystemError for a negative count.
 */
PyObject *KosMatchMark_Allocate(
    PyObject *seq,
    Py_ssize_t index,
    unsigned char kind,
    Py_ssize_t count);


/**
   MATCH_SCOPE: a mark at cursor's position with count unbound slots,
   from the freelist when count fits a default mark. A mark with slots
   is tracked; one without holds only its position, as a cursor does,
   and is left untracked. New reference; NULL with an exception set.
 */
static inline PyObject *KosMatchMark_New(
    PyObject *cursor,
    Py_ssize_t count) {

  KosMatchCursor *c = (KosMatchCursor *) cursor;
  KosMatchMark *m = kos_match_mark_free_list;

  /* the unsigned compare sends a negative count to the allocator's
     SystemError as well */
  if (unlikely(! m || (size_t) count > KOS_MATCH_MARK_SLOTS))
    return KosMatchMark_Allocate(c->seq, c->index, c->kind, count);

  kos_match_mark_free_list = (KosMatchMark *) m->seq;
  kos_match_mark_free_count--;
  KOS_TRACE_FL_ALLOC("mark", 1);
  _Py_NewReference((PyObject *) m);

  Py_INCREF(c->seq);
  m->seq = c->seq;
  m->index = c->index;
  m->kind = c->kind;
  m->count = count;
  if (count)
    PyObject_GC_Track((PyObject *) m);
  return (PyObject *) m;
}


/**
   MATCH_RESET: cursor moved back to mark's position (see held above).
   NULL with an exception set.
 */
static inline PyObject *KosMatchMark_Reset(
    PyObject *cursor,
    PyObject *mark,
    Py_ssize_t held) {

  KosMatchMark *m = (KosMatchMark *) mark;

  return KosMatchCursor_MoveTo(cursor, m->seq, m->index, held);
}


/**
   MATCH_AT: the element at mark's position, as KosMatchCursor_Next
   gave it. New reference; NULL with an exception set, ValueError when
   the subject no longer holds the element (a list shrunk below the
   position, a spine cell that is no longer a pair).
 */
PyObject *KosMatchMark_At(PyObject *mark);

/**
   MATCH_SPAN: the elements from mark's position to cursor's: the
   empty tuple for none, the element as KosMatchMark_At gives it for
   one, else a tuple of them (a Datum per cell over a spine). New
   reference; NULL with an exception set, ValueError when the subject
   no longer holds the span (a list shrunk below its end, a spine
   that no longer reaches the cursor's cell from the mark's).
 */
PyObject *KosMatchMark_Span(PyObject *mark, PyObject *cursor);

/**
   A new mark holding mark's position and slot values, for a write to
   a mark another entry also holds. New reference; NULL with an
   exception set.
 */
PyObject *KosMatchMark_Copy(PyObject *mark);


/**
   The mark a write goes to: mark itself, borrowed, when held counts
   every reference to it, else KosMatchMark_Copy's copy, a new
   reference for the caller to store at the mark's stack entry. NULL
   with an exception set.
 */
static inline PyObject *KosMatchMark_Writable(
    PyObject *mark,
    Py_ssize_t held) {

  if (unlikely(Py_REFCNT(mark) != held))
    return KosMatchMark_Copy(mark);
  return mark;
}


/**
   MATCH_STORE: value into slot i of mark, which must be writable,
   consuming value on every path. When name is not NULL, a Datum value
   and each Datum item of a tuple value take name as their binding.
   0, or -1 with SystemError for an index outside the slot count.
 */
int KosMatchMark_Store(
    PyObject *mark,
    Py_ssize_t i,
    PyObject *value,
    PyObject *name);

/**
   MATCH_CLEAR: empty the slots of mark (writable) that slots, a tuple
   of int indices, names. 0, or -1 with an exception set, SystemError
   for an index outside the slot count.
 */
int KosMatchMark_Clear(PyObject *mark, PyObject *slots);

/**
   A plain dict of mark's bound slots keyed by names, a tuple of one
   name per slot: a REPEAT pass's captures over any subject. New
   reference; NULL with an exception set, SystemError when names does
   not have one name per slot.
 */
PyObject *KosMatchMark_Dict(PyObject *mark, PyObject *names);

/**
   MATCH_CAPTURES: the scope's captures, mark's bound slots keyed by
   names. Over a sequence or a single value a plain dict; over a pair
   spine a DatumFrame whose source is the span from mark's position to
   cursor's (the first cell itself when the span is empty or runs to
   the spine's end, the car read through Datum for one cell, else a
   tuple of the cars read through Datum) and whose position is the
   first cell's. The frame references the span, never the mark. New
   reference; NULL with an exception set, ValueError when the spine
   no longer reaches the cursor's cell from the mark's.
 */
PyObject *KosMatchMark_Captures(
    PyObject *mark,
    PyObject *cursor,
    PyObject *names);

/**
   MATCH_PASS: one REPEAT pass's value. When any slot of mark is
   bound, KosMatchMark_Dict, or KosMatchMark_Captures when captures is
   set (a REPEAT whose body is a GROUP); else the pass's span from
   mark's position to cursor's, as KosMatchMark_Span gives it. New
   reference; NULL with an exception set.
 */
PyObject *KosMatchMark_Pass(
    PyObject *mark,
    PyObject *cursor,
    PyObject *names,
    int captures);

/**
   MATCH_COLUMNS: for each name in names, the list of its values in
   the pass dicts of items that bound it, in pass order, stored into
   mark's (writable) slot at the same position in slots, a tuple of
   int indices; the empty list when no pass bound it or there are no
   passes. items is read, not written. 0, or -1 with an
   exception set, SystemError for an index outside the slot count or
   names and slots of different lengths.
 */
int KosMatchMark_Columns(
    PyObject *mark,
    PyObject *items,
    PyObject *names,
    PyObject *slots);


/* === unit lifecycle === */


int koskari_matcher_init(PyObject *mod);
int koskari_matcher_clear(PyObject *mod);


#endif


// The end.
