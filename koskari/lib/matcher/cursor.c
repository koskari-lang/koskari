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
  Included by matcher.c after step.c. The match cursor: one
  KosMatchCursor over a pair spine, a sequence or a single value, with
  an end bound, and the steps the cursor instructions call
  (design/interpreter/matcher-core.md).

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/


/* === core cursors === */


static PyObject *core_cursor_new(
    PyObject *seq,
    Py_ssize_t length,
    Py_ssize_t index,
    Py_ssize_t reserve,
    unsigned char kind) {

  PyObject *cursor = match_cursor_new(seq, length, index, kind);

  if (cursor)
    ((KosMatchCursor *) cursor)->reserve = reserve;
  return cursor;
}


/**
   c at seq and index, as a new reference for the caller to store at
   c's entry: c itself, moved in place, when held counts every
   reference to it, else a new cursor of the same kind, length and
   reserve. NULL with an exception set.
*/
static PyObject *core_cursor_moved(
    KosMatchCursor *c,
    PyObject *seq,
    Py_ssize_t index,
    Py_ssize_t held) {

  if (Py_REFCNT(c) == held) {
    if (c->seq != seq) {
      Py_INCREF(seq);
      Py_SETREF(c->seq, seq);
    }
    c->index = index;
    Py_INCREF(c);
    return (PyObject *) c;
  }
  return core_cursor_new(seq, c->length, index, c->reserve, c->kind);
}


static Py_ssize_t core_remaining(const KosMatchCursor *c) {
  Py_ssize_t length = c->length;

  if (c->kind == KOS_MATCH_CURSOR && PyList_Check(c->seq))
    length = PyList_GET_SIZE(c->seq);
  length -= c->reserve + c->index;
  return length > 0 ? length : 0;
}


/**
   The ValueError a step raises when the subject no longer holds what
   the walk took from it. NULL.
*/
static PyObject *core_changed(void) {
  PyErr_SetString(PyExc_ValueError, "match subject changed under the walk");
  return NULL;
}


/**
   The element at index of a subject of kind, as a pull gives it: a
   Datum over the car of seq, a spine cell; the single value; the
   sequence's item. New reference; NULL with an exception set,
   ValueError when the subject no longer holds the element.
*/
static PyObject *core_element(
    unsigned char kind,
    PyObject *seq,
    Py_ssize_t index) {

  switch (kind) {
  case KOS_MATCH_SPINE:
    if (! KosPair_Check(seq) || KosNilP(seq))
      return core_changed();
    return step_box_cell(seq, matcher_peel_datum(KosPair_CAR(seq)));

  case KOS_MATCH_SINGLE:
    if (index != 0)
      return core_changed();
    Py_INCREF(seq);
    return seq;

  default:
    if ((PyList_Check(seq) || PyTuple_CheckExact(seq))
        && index >= Py_SIZE(seq))
      return core_changed();
    return step_seq_item(seq, index);
  }
}


/**
   The cell n cells on from start, each one a pair. Borrowed; NULL
   with ValueError set when the spine ends inside the n cells.
*/
static PyObject *core_spine_walk(PyObject *start, Py_ssize_t n) {
  PyObject *cell = start;

  for (Py_ssize_t i = 0; i < n; i++) {
    if (! KosPair_Check(cell) || KosNilP(cell))
      return core_changed();
    cell = KosPair_CDR(cell);
  }
  return cell;
}


/**
   A tuple of a Datum per cell for the n cells from start. The walk
   holds the cell it boxes, since a box's allocation may run a
   finalizer that rewrites the spine. New reference; NULL with an
   exception set, ValueError when the spine ends inside the n cells.
*/
static PyObject *core_spine_boxes(PyObject *start, Py_ssize_t n) {
  PyObject *t = PyTuple_New(n);
  PyObject *cell = start;

  if (! t)
    return NULL;

  Py_INCREF(cell);
  for (Py_ssize_t i = 0; i < n; i++) {
    if (! KosPair_Check(cell) || KosNilP(cell)) {
      Py_DECREF(cell);
      Py_DECREF(t);
      return core_changed();
    }

    PyObject *boxed = step_box_cell(
        cell, matcher_peel_datum(KosPair_CAR(cell)));
    if (! boxed) {
      Py_DECREF(cell);
      Py_DECREF(t);
      return NULL;
    }
    PyTuple_SET_ITEM(t, i, boxed);

    PyObject *next = KosPair_CDR(cell);
    Py_INCREF(next);
    Py_SETREF(cell, next);
  }
  Py_DECREF(cell);
  return t;
}


/* === the cursor steps === */


int KosMatchCursor_Descend(PyObject *value, PyObject **out_cursor) {
  PyObject *peeled = matcher_peel_datum(value);
  int gate = matcher_sequence_gate(peeled);
  PyObject *cursor;

  *out_cursor = NULL;
  if (gate <= 0)
    return gate;

  if (KosPair_Check(peeled) || KosNilP(peeled)) {
    /* the one walk over the spine: the count every later step reads */
    PyObject *term = NULL;
    Py_ssize_t count = step_count_spine(peeled, &term);

    if (count == -2)
      return -1;
    if (count == -1)
      return 0;
    cursor = core_cursor_new(peeled, count, 0, 0, KOS_MATCH_SPINE);

  } else {
    Py_ssize_t length;

    if (PyList_CheckExact(peeled) || PyTuple_CheckExact(peeled)) {
      length = Py_SIZE(peeled);
    } else {
      length = PyObject_Length(peeled);
      if (length < 0)
        return -1;
    }
    cursor = core_cursor_new(peeled, length, 0, 0, KOS_MATCH_CURSOR);
  }

  if (! cursor)
    return -1;
  *out_cursor = cursor;
  return 1;
}


PyObject *KosMatchCursor_Single(PyObject *value) {
  return core_cursor_new(value, 1, 0, 0, KOS_MATCH_SINGLE);
}


Py_ssize_t KosMatchCursor_Remaining(PyObject *cursor) {
  return core_remaining((KosMatchCursor *) cursor);
}


int KosMatchCursor_Next(
    PyObject *cursor,
    Py_ssize_t held,
    PyObject **out_elem,
    PyObject **out_next) {

  KosMatchCursor *c = (KosMatchCursor *) cursor;
  PyObject *seq = c->seq;
  PyObject *elem;

  *out_elem = NULL;
  *out_next = NULL;

  if (core_remaining(c) == 0)
    return 0;

  if (c->kind == KOS_MATCH_SPINE) {
    if (! KosPair_Check(seq) || KosNilP(seq))
      return 0;
    elem = step_box_cell(seq, matcher_peel_datum(KosPair_CAR(seq)));
    if (! elem)
      return -1;
    /* read after the box, whose allocation may run a finalizer */
    seq = KosPair_CDR(c->seq);

  } else if (c->kind == KOS_MATCH_SINGLE) {
    elem = seq;
    Py_INCREF(elem);

  } else {
    elem = step_seq_item(seq, c->index);
    if (! elem)
      return -1;
  }

  PyObject *next = core_cursor_moved(c, seq, c->index + 1, held);
  if (! next) {
    Py_DECREF(elem);
    return -1;
  }
  *out_elem = elem;
  *out_next = next;
  return 1;
}


int KosMatchCursor_Bound(
    PyObject *cursor,
    Py_ssize_t w,
    PyObject **out_bound) {

  KosMatchCursor *c = (KosMatchCursor *) cursor;

  *out_bound = NULL;
  if (core_remaining(c) < w)
    return 0;

  *out_bound = core_cursor_new(
      c->seq, c->length, c->index, c->reserve + w, c->kind);
  return *out_bound ? 1 : -1;
}


PyObject *KosMatchCursor_Sync(
    PyObject *cursor,
    PyObject *to,
    Py_ssize_t held) {

  KosMatchCursor *t = (KosMatchCursor *) to;

  return core_cursor_moved(
      (KosMatchCursor *) cursor, t->seq, t->index, held);
}


int KosMatchCursor_Tail(
    PyObject *cursor,
    Py_ssize_t w,
    Py_ssize_t min,
    Py_ssize_t held,
    PyObject **out_span,
    PyObject **out_next) {

  KosMatchCursor *c = (KosMatchCursor *) cursor;
  Py_ssize_t avail = core_remaining(c);
  PyObject *span;
  PyObject *end = c->seq;

  *out_span = NULL;
  *out_next = NULL;

  if (avail < w || avail - w < min)
    return 0;
  Py_ssize_t consume = avail - w;

  switch (c->kind) {
  case KOS_MATCH_SPINE:
    if (consume == 0) {
      /* the empty remainder: the spine's terminator, as the engine
         binds it */
      PyObject *term = core_spine_walk(c->seq, c->length - c->index);
      if (! term)
        return -1;
      span = kos_datum_new(term, Py_None, Py_None);
    } else if (c->index + consume == c->length) {
      span = step_box_cell(c->seq, c->seq);
    } else {
      span = core_spine_boxes(c->seq, consume);
    }
    break;

  case KOS_MATCH_SINGLE:
    span = consume ? PyTuple_Pack(1, c->seq) : PyTuple_New(0);
    break;

  default:
    span = step_seq_slice(c->seq, c->index, c->index + consume);
    break;
  }

  if (! span)
    return -1;

  if (c->kind == KOS_MATCH_SPINE) {
    /* read after the span's allocations, which may run a finalizer */
    end = core_spine_walk(c->seq, consume);
    if (! end) {
      Py_DECREF(span);
      return -1;
    }
  }

  PyObject *next = core_cursor_moved(c, end, c->index + consume, held);
  if (! next) {
    Py_DECREF(span);
    return -1;
  }
  *out_span = span;
  *out_next = next;
  return 1;
}


// The end.
