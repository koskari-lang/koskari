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


/**
   A cursor over a sequence subject: the sequence, its length and the
   index of the next element, and a mark over one, a separate object
   of the same type with the kind byte set, holding the index a walk
   may return to. A pair spine needs neither, the cell is the cursor
   and the mark.
   The matcher core's cursors (KosMatchCursor_* below) use the same
   type with two more kinds: a spine cursor holds the current cell in
   seq, the count of cells from the cell it was made at in length, and
   the cells pulled in index; a single-value cursor holds the value in
   seq with a length of one. A core sequence cursor has the sequence
   kind. reserve is the count of elements a bounded cursor leaves
   before its subject's end, zero for every other cursor.
   Opaque to Python and untracked. A write to either, an advance or a
   re-mark, moves the object in place when its stack entry is the only
   holder and stores a new object at the entry otherwise, so a cloned
   segment keeps the position it took.
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
  KOS_MATCH_MARK = 1,
  KOS_MATCH_SPINE = 2,
  KOS_MATCH_SINGLE = 3,
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


/* The packed widths of a FORM row, in one sixteen-bit immediate: min
   in the high eight bits, max in the low, 0xFF for an unbounded max.
   A REST row packs its minimum tail and the suffix width the same
   way; the lowering refuses a pattern whose counts do not fit. */
#define KOS_MATCH_PACK(hi, lo)  ((((uint32_t) (hi)) << 8) | ((lo) & 0xFFu))
#define KOS_MATCH_HI(w)         (((w) >> 8) & 0xFFu)
#define KOS_MATCH_LO(w)         ((w) & 0xFFu)
#define KOS_MATCH_UNBOUNDED8    0xFFu


/**
   One pulled element: the element itself, the pair cell it came from
   (NULL from a sequence cursor), the sequence cursor it came from
   (NULL from a pair spine), and the advanced pair cursor to store
   back (NULL from a sequence cursor), every one a new reference the
   step owns until KosMatch_StepClear, so the test a body runs on the
   element, which may reach user code, cannot leave the step holding
   a released cursor. A pull advances nothing: the caller stores what
   KosMatch_StepAdvance returns once the element has passed its test,
   so a miss leaves the cursor where it was.
 */
typedef struct KosMatchStep {
  PyObject *elem;
  PyObject *cell;
  PyObject *cursor;
  PyObject *next;
} KosMatchStep;


/**
   The FORM descent: the PEP 634 sequence gate on elem read through
   Datum, the length precheck against the packed widths for a
   sequence, the loop check over a pair spine, and the cursor for the
   walk. Returns 1 with *out_cursor a
   new reference (the pair or nil itself, or a KosMatchCursor), 0 on a
   miss, -1 with an exception set, ValueError for a spine that loops.
 */
int KosMatch_Descend(PyObject *elem, uint32_t widths, PyObject **out_cursor);

/**
   Pull one element from cursor. Returns 1 with out filled, 0 at the
   end of the spine or sequence (an improper tail is an end that
   MATCH_END then rejects), -1 with an exception set.
 */
int KosMatch_Pull(PyObject *cursor, KosMatchStep *out);

/**
   The element of a step as a capture binds it: Datum(elem, position)
   from a pair cell, the element itself from a sequence. New reference.
 */
PyObject *KosMatch_Box(const KosMatchStep *step);

/**
   The cursor to store at the entry the step was pulled from: the next
   cell of a pair spine, taken from the step, or the sequence cursor
   advanced past the element. New reference; NULL with an exception
   set.
 */
PyObject *KosMatch_StepAdvance(KosMatchStep *step);

/**
   Release what a step owns.
 */
void KosMatch_StepClear(KosMatchStep *step);

/**
   Whether cursor is at its end: nil for a pair spine, the length for a
   sequence. An improper tail is not an end. 1 or 0.
 */
int KosMatch_End(PyObject *cursor);

/**
   The remainder of cursor but the suffix: counts packs the minimum
   tail and the suffix width. Returns 1 with *out_value the capture
   value (the boxed tail, a tuple of boxed cells, or a tuple slice of
   a sequence; new reference) and *out_next the advanced cursor (new
   reference), 0 on a miss, -1 with an exception set.
 */
int KosMatch_Rest(
    PyObject *cursor,
    uint32_t counts,
    PyObject **out_value,
    PyObject **out_next);

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
   The literal test: elem read through Datum is lit by identity, a
   gensym whose base is lit, or equal to lit. 1, 0, or -1.
 */
int KosMatch_LiteralTest(PyObject *elem, PyObject *lit);

/**
   The class test: elem is an instance of cls, and when npos is
   positive cls has a __match_args__ tuple of at least npos names.
   1, 0, or -1 with the engine's TypeError for a class that cannot
   serve the positional fields.
 */
int KosMatch_ClassTest(PyObject *elem, PyObject *cls, Py_ssize_t npos);

/**
   The attribute of elem that cls.__match_args__[pos] names. Returns 1
   with *out a new reference, 0 when the attribute is missing, -1 with
   an exception set.
 */
int KosMatch_FieldPos(
    PyObject *elem,
    PyObject *cls,
    Py_ssize_t pos,
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
   Whether at least needed elements remain from cursor: the length
   less the index for a sequence, a walk of needed cells for a pair
   spine, where an improper tail inside the walk counts as too few.
   1 or 0.
 */
int KosMatch_Available(PyObject *cursor, Py_ssize_t needed);

/**
   The equality test of SAME: elem and the bound value prev read
   through Datum; a DatumFrame on either side misses; identity, then
   Py_EQ. 1, 0, or -1.
 */
int KosMatch_SameTest(PyObject *elem, PyObject *prev);

/**
   The mark to push for MATCH_MARK: the cell of a pair spine, or a
   mark over the sequence at the cursor's index. New reference; NULL
   with an exception set.
 */
PyObject *KosMatch_Mark(PyObject *cursor);

/**
   The mark to store for MATCH_REMARK, at the cursor's position: the
   cell of a pair spine, or mark moved in place when its entry is its
   only holder, else a new mark. Beside a sequence cursor the mark
   must be exactly a sequence mark; a pair spine takes any mark. New
   reference; NULL with an exception set, RuntimeError for a sequence
   cursor beside another mark.
 */
PyObject *KosMatch_Remark(PyObject *cursor, PyObject *mark);

/**
   The cursor to store for MATCH_RESET, back at the mark's position:
   the cell the mark is over a pair spine, or cursor moved in place
   when its entry is its only holder, else a new cursor. Beside a
   sequence cursor the mark must be exactly a sequence mark; a pair
   spine takes any mark. New reference; NULL with an exception set,
   RuntimeError for a sequence cursor beside another mark.
 */
PyObject *KosMatch_Reset(PyObject *cursor, PyObject *mark);

/**
   Append every remaining element of cursor, boxed from a pair spine,
   to list. A pair spine is counted first, so an improper tail is a
   miss before anything is appended. Returns 1 with *out_next the
   cursor to store, the terminator of a pair spine or the sequence
   cursor at its end (new reference), 0 on a miss, -1 with an
   exception set.
 */
int KosMatch_Collect(PyObject *cursor, PyObject *list, PyObject **out_next);

/**
   The captures: names is the slot table, values the matching array of
   bound values (borrowed; NULL for an unbound slot), start the saved
   position the span began at and cursor its end. A pair cell as the
   start makes a DatumFrame whose source is the span and whose position
   is the start cell's; an index makes a plain dict. New reference;
   NULL with an exception set, ValueError when a pair span no longer
   reaches the cursor, by a cut or a loop user code made in the spine.
 */
PyObject *KosMatch_Captures(
    PyObject *names,
    PyObject *const *values,
    PyObject *start,
    PyObject *cursor);

/**
   One repeat iteration's value: KosMatch_Captures when any slot is
   bound, else the span itself, the boxed single element or a tuple of
   boxed elements, as the engine records a capture-less iteration. A
   zero-width span is an error. New reference; NULL with an exception
   set, ValueError for a pair span as KosMatch_Captures raises it.
 */
PyObject *KosMatch_Iteration(
    PyObject *names,
    PyObject *const *values,
    PyObject *start,
    PyObject *cursor);

/**
   The value under key in the mapping elem, probed without inserting.
   Returns 1 with *out a new reference, 0 when the key is absent, -1
   with an exception set.
 */
int KosMatch_Item(PyObject *elem, PyObject *key, PyObject **out);


/* === the matcher core: cursors === */


/* The unbounded maximum of a 16-bit width field, as MATCH_FITS reads
   it. */
#define KOS_MATCH_UNBOUNDED  0xFFFFu


/* A helper that moves a cursor takes held, the references the caller
   accounts for (1 for a cursor its stack entry alone holds), and
   returns the moved cursor as a new reference for the caller to store
   at the entry: the cursor itself, moved in place, when held counts
   every reference to it, else a new cursor at the new position, so a
   cloned segment keeps the position it took. */


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
Py_ssize_t KosMatchCursor_Remaining(PyObject *cursor);

/**
   MATCH_NEXT: pull one element and advance. The element is a new
   Datum over the cell's car read through Datum, with the cell's
   position, from a spine, and the item itself from a sequence or a
   single value. Returns 1 with *out_elem and *out_next (the moved
   cursor, see held above) new references, 0 at the end (no element
   left, or a spine cell that is not a pair: user code cut the spine
   ahead of the cursor), -1 with an exception set.
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
   MATCH_SYNC: cursor moved to the position of to, a cursor over the
   same subject. New reference (see held above); NULL with an
   exception set.
 */
PyObject *KosMatchCursor_Sync(
    PyObject *cursor,
    PyObject *to,
    Py_ssize_t held);

/**
   MATCH_TAIL: the remaining elements less w, and cursor advanced past
   them. The span over a sequence is a tuple of the items (a list read
   within its live length); over a single value, a tuple of the value
   or the empty tuple; over a spine, a Datum over the terminator when
   it is empty, a Datum over its first cell (the sub-spine) when it
   runs to the spine's end, else a tuple of a Datum per cell. Returns
   1 with *out_span and *out_next (see held above) new references, 0
   when fewer than w remain or the span is shorter than min, -1 with
   an exception set, ValueError for a spine user code cut inside the
   span.
 */
int KosMatchCursor_Tail(
    PyObject *cursor,
    Py_ssize_t w,
    Py_ssize_t min,
    Py_ssize_t held,
    PyObject **out_span,
    PyObject **out_next);


/* === the matcher core: marks === */


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


/**
   MATCH_SCOPE: a mark at cursor's position with count unbound slots.
   New reference; NULL with an exception set.
 */
PyObject *KosMatchMark_New(PyObject *cursor, Py_ssize_t count);

/**
   MATCH_RESET: cursor moved back to mark's position. New reference
   (see held above); NULL with an exception set.
 */
PyObject *KosMatchMark_Reset(
    PyObject *cursor,
    PyObject *mark,
    Py_ssize_t held);

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
   The mark a write goes to: mark itself when held counts every
   reference to it, else a copy holding the same position and slot
   values, for the caller to store at the mark's stack entry. New
   reference; NULL with an exception set.
 */
PyObject *KosMatchMark_Writable(PyObject *mark, Py_ssize_t held);

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
