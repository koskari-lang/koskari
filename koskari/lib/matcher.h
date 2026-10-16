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
   index of the next element. A pair spine needs none, the cell is the
   cursor. Opaque to Python and untracked. An advance moves the cursor
   in place when its entry is the only holder and stores a new cursor
   at the entry otherwise, so a mark, which is the cursor itself, and
   a cloned segment keep the position they took.
 */
typedef struct KosMatchCursor {
  PyObject_HEAD
  PyObject *seq;
  Py_ssize_t length;
  Py_ssize_t index;
} KosMatchCursor;


extern PyTypeObject KosMatchCursorType;


#define KosMatchCursor_Check(obj)                                  \
  ((obj) && ((obj)->ob_type == &KosMatchCursorType))


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
   The literal test: elem read through Datum is lit by identity, a
   gensym whose base is lit, or equal to lit. 1, 0, or -1.
 */
/**
   The type test against a core type: elem read through Datum must be
   an instance of the type the ordinal names. 1 or 0; -1 with an
   exception set for an ordinal out of range.
 */
int KosMatch_CoreTypeTest(PyObject *elem, uint32_t core);

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
   is the start cell's; an index makes a plain dict. New reference.
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
   zero-width span is an error. New reference.
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


/* === unit lifecycle === */


int koskari_matcher_init(PyObject *mod);
int koskari_matcher_clear(PyObject *mod);


#endif


// The end.
