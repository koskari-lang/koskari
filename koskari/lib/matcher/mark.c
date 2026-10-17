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
  Included by matcher.c after cursor.c. The match mark: a
  capture scope on the operand stack holding a cursor position and the
  scope's slots, and the steps that read a span from it, write its
  slots and build captures from them
  (design/interpreter/matcher-core.md).

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/


/* === KosMatchMark === */


/* Freed default-size marks chain through seq, up to the cap; a larger
   mark goes back to the allocator. */
#define MARK_MAX_FREE KOS_FREELIST_CAP(8)

static KosMatchMark *mark_free_list = NULL;
static int mark_free_count = 0;


static int match_mark_traverse(PyObject *self, visitproc visit, void *arg) {
  KosMatchMark *m = (KosMatchMark *) self;

  Py_VISIT(m->seq);
  for (Py_ssize_t i = 0; i < m->count; i++)
    Py_VISIT(m->slots[i]);
  return 0;
}


static int match_mark_clear(PyObject *self) {
  KosMatchMark *m = (KosMatchMark *) self;

  for (Py_ssize_t i = 0; i < m->count; i++)
    Py_CLEAR(m->slots[i]);
  Py_CLEAR(m->seq);
  return 0;
}


static void match_mark_dealloc(PyObject *self) {
  KosMatchMark *m = (KosMatchMark *) self;

  PyObject_GC_UnTrack(self);
  match_mark_clear(self);

  if (Py_SIZE(m) == KOS_MATCH_MARK_SLOTS
      && mark_free_count < MARK_MAX_FREE) {
    KOS_TRACE_FL_RETURN("mark", 0);
    m->seq = (PyObject *) mark_free_list;
    mark_free_list = m;
    mark_free_count++;

  } else {
    KOS_TRACE_FL_RETURN("mark", 1);
    PyObject_GC_Del(self);
  }
}


static void match_mark_free_list_clear(void) {
  while (mark_free_list) {
    KosMatchMark *m = mark_free_list;
    mark_free_list = (KosMatchMark *) m->seq;
    PyObject_GC_Del(m);
  }
  mark_free_count = 0;
}


PyTypeObject KosMatchMarkType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.MatchMark",
  .tp_basicsize = offsetof(KosMatchMark, slots),
  .tp_itemsize = sizeof(PyObject *),
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "A match instruction's capture scope",
  .tp_dealloc = match_mark_dealloc,
  .tp_traverse = match_mark_traverse,
  .tp_clear = match_mark_clear,
};


/**
   A mark at seq, index and kind with count unbound slots, from the
   freelist when count fits a default mark. A mark with slots is
   tracked; one without holds only its position, as a cursor does, and
   is left untracked. New reference; NULL with an exception set.
*/
static KosMatchMark *match_mark_new(
    PyObject *seq,
    Py_ssize_t index,
    unsigned char kind,
    Py_ssize_t count) {

  KosMatchMark *m;

  if (count < 0) {
    PyErr_Format(PyExc_SystemError,
                 "match mark with a negative slot count %zd", count);
    return NULL;
  }

  if (count <= KOS_MATCH_MARK_SLOTS && mark_free_list) {
    m = mark_free_list;
    mark_free_list = (KosMatchMark *) m->seq;
    mark_free_count--;
    KOS_TRACE_FL_ALLOC("mark", 1);
    _Py_NewReference((PyObject *) m);

  } else {
    KOS_TRACE_FL_ALLOC("mark", 0);
    m = PyObject_GC_NewVar(
        KosMatchMark, &KosMatchMarkType,
        count <= KOS_MATCH_MARK_SLOTS ? KOS_MATCH_MARK_SLOTS : count);
    if (! m)
      return NULL;
  }

  Py_INCREF(seq);
  m->seq = seq;
  m->index = index;
  m->kind = kind;
  m->count = count;
  for (Py_ssize_t i = 0; i < count; i++)
    m->slots[i] = NULL;

  if (count)
    PyObject_GC_Track((PyObject *) m);
  return m;
}


/**
   The slot index item names in mark m. -1 with an exception set,
   SystemError for an index outside the slot count.
*/
static Py_ssize_t mark_slot_index(KosMatchMark *m, PyObject *item) {
  Py_ssize_t i = PyLong_AsSsize_t(item);

  if (i == -1 && PyErr_Occurred())
    return -1;
  if (i < 0 || i >= m->count) {
    PyErr_Format(PyExc_SystemError,
                 "match slot %zd outside a mark of %zd slots", i, m->count);
    return -1;
  }
  return i;
}


/* === the mark steps === */


PyObject *KosMatchMark_New(PyObject *cursor, Py_ssize_t count) {
  KosMatchCursor *c = (KosMatchCursor *) cursor;

  return (PyObject *) match_mark_new(c->seq, c->index, c->kind, count);
}


PyObject *KosMatchMark_Reset(
    PyObject *cursor,
    PyObject *mark,
    Py_ssize_t held) {

  KosMatchMark *m = (KosMatchMark *) mark;

  return core_cursor_moved((KosMatchCursor *) cursor, m->seq, m->index, held);
}


PyObject *KosMatchMark_At(PyObject *mark) {
  KosMatchMark *m = (KosMatchMark *) mark;

  return core_element(m->kind, m->seq, m->index);
}


/**
   The count of elements from mark m's position to cursor c's. -1 with
   SystemError set when the cursor is behind the mark.
*/
static Py_ssize_t mark_span_count(KosMatchMark *m, KosMatchCursor *c) {
  Py_ssize_t n = c->index - m->index;

  if (n < 0) {
    PyErr_Format(PyExc_SystemError,
                 "match span ends %zd elements before its mark", -n);
    return -1;
  }
  return n;
}


PyObject *KosMatchMark_Span(PyObject *mark, PyObject *cursor) {
  KosMatchMark *m = (KosMatchMark *) mark;
  KosMatchCursor *c = (KosMatchCursor *) cursor;
  Py_ssize_t n = mark_span_count(m, c);

  if (n < 0)
    return NULL;

  if (m->kind == KOS_MATCH_SPINE
      && core_spine_walk(m->seq, n) != c->seq) {
    if (! PyErr_Occurred())
      core_changed();
    return NULL;
  }

  if (n == 1)
    return core_element(m->kind, m->seq, m->index);

  switch (m->kind) {
  case KOS_MATCH_SPINE:
    return core_spine_boxes(m->seq, n);

  case KOS_MATCH_SINGLE:
    return PyTuple_New(0);

  default:
    if (PyList_Check(m->seq) && m->index + n > PyList_GET_SIZE(m->seq))
      return core_changed();
    return step_seq_slice(m->seq, m->index, m->index + n);
  }
}


PyObject *KosMatchMark_Writable(PyObject *mark, Py_ssize_t held) {
  KosMatchMark *m = (KosMatchMark *) mark;

  if (Py_REFCNT(m) == held) {
    Py_INCREF(m);
    return mark;
  }

  KosMatchMark *copy = match_mark_new(m->seq, m->index, m->kind, m->count);
  if (! copy)
    return NULL;
  for (Py_ssize_t i = 0; i < m->count; i++) {
    Py_XINCREF(m->slots[i]);
    copy->slots[i] = m->slots[i];
  }
  return (PyObject *) copy;
}


/**
   The binding a stored value takes: name on a Datum, and on each
   Datum item of a tuple.
*/
static void mark_bind_datum(PyObject *value, PyObject *name) {
  if (KosDatum_Check(value)) {
    Py_INCREF(name);
    Py_SETREF(((KosDatum *) value)->binding, name);
    return;
  }

  if (PyTuple_Check(value)) {
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(value); i++) {
      PyObject *item = PyTuple_GET_ITEM(value, i);
      if (KosDatum_Check(item)) {
        Py_INCREF(name);
        Py_SETREF(((KosDatum *) item)->binding, name);
      }
    }
  }
}


int KosMatchMark_Store(
    PyObject *mark,
    Py_ssize_t i,
    PyObject *value,
    PyObject *name) {

  KosMatchMark *m = (KosMatchMark *) mark;

  if (i < 0 || i >= m->count) {
    Py_DECREF(value);
    PyErr_Format(PyExc_SystemError,
                 "match slot %zd outside a mark of %zd slots", i, m->count);
    return -1;
  }

  if (name)
    mark_bind_datum(value, name);
  Py_XSETREF(m->slots[i], value);
  return 0;
}


int KosMatchMark_Clear(PyObject *mark, PyObject *slots) {
  KosMatchMark *m = (KosMatchMark *) mark;

  for (Py_ssize_t j = 0; j < PyTuple_GET_SIZE(slots); j++) {
    Py_ssize_t i = mark_slot_index(m, PyTuple_GET_ITEM(slots, j));
    if (i < 0)
      return -1;
    Py_CLEAR(m->slots[i]);
  }
  return 0;
}


/**
   Fill out with m's bound slots under names, one name per slot. 0, or
   -1 with an exception set.
*/
static int mark_fill(PyObject *out, KosMatchMark *m, PyObject *names) {
  if (PyTuple_GET_SIZE(names) != m->count) {
    PyErr_Format(PyExc_SystemError,
                 "match captures name %zd slots of a mark of %zd",
                 PyTuple_GET_SIZE(names), m->count);
    return -1;
  }

  for (Py_ssize_t i = 0; i < m->count; i++) {
    if (! m->slots[i])
      continue;
    if (PyDict_SetItem(out, PyTuple_GET_ITEM(names, i), m->slots[i]) < 0)
      return -1;
  }
  return 0;
}


PyObject *KosMatchMark_Dict(PyObject *mark, PyObject *names) {
  PyObject *out = PyDict_New();

  if (! out)
    return NULL;
  if (mark_fill(out, (KosMatchMark *) mark, names) < 0) {
    Py_DECREF(out);
    return NULL;
  }
  return out;
}


/**
   The source a DatumFrame records for the n cells from m's cell to
   c's: the first cell itself when the span is empty or runs to the
   spine's end, the car read through Datum for one cell, else a tuple
   of the cars read through Datum. New reference; NULL with an
   exception set, ValueError when the spine no longer reaches c's cell
   from m's.
*/
static PyObject *mark_span_source(
    KosMatchMark *m,
    KosMatchCursor *c,
    Py_ssize_t n) {

  /* a span empty or running to the spine's end is its first cell,
     which the mark holds; the cells after it are not read */
  if (n == 0 || c->index == c->length) {
    Py_INCREF(m->seq);
    return m->seq;
  }

  if (core_spine_walk(m->seq, n) != c->seq) {
    if (! PyErr_Occurred())
      core_changed();
    return NULL;
  }

  if (n == 1) {
    PyObject *car = matcher_peel_datum(KosPair_CAR(m->seq));
    Py_INCREF(car);
    return car;
  }

  PyObject *t = PyTuple_New(n);
  if (! t)
    return NULL;
  PyObject *cell = m->seq;
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *car = matcher_peel_datum(KosPair_CAR(cell));
    Py_INCREF(car);
    PyTuple_SET_ITEM(t, i, car);
    cell = KosPair_CDR(cell);
  }
  return t;
}


PyObject *KosMatchMark_Captures(
    PyObject *mark,
    PyObject *cursor,
    PyObject *names) {

  KosMatchMark *m = (KosMatchMark *) mark;
  KosMatchCursor *c = (KosMatchCursor *) cursor;

  if (m->kind != KOS_MATCH_SPINE)
    return KosMatchMark_Dict(mark, names);

  Py_ssize_t n = mark_span_count(m, c);
  if (n < 0)
    return NULL;

  PyObject *source = mark_span_source(m, c, n);
  if (! source)
    return NULL;

  PyObject *out = kos_datum_frame_new();
  if (! out) {
    Py_DECREF(source);
    return NULL;
  }
  if (mark_fill(out, m, names) < 0) {
    Py_DECREF(source);
    Py_DECREF(out);
    return NULL;
  }

  KosDatumFrame *f = (KosDatumFrame *) out;
  Py_SETREF(f->source, source);
  Py_SETREF(f->position, step_cell_position(m->seq));
  return out;
}


PyObject *KosMatchMark_Pass(
    PyObject *mark,
    PyObject *cursor,
    PyObject *names,
    int captures) {

  KosMatchMark *m = (KosMatchMark *) mark;

  for (Py_ssize_t i = 0; i < m->count; i++) {
    if (! m->slots[i])
      continue;
    if (captures)
      return KosMatchMark_Captures(mark, cursor, names);
    return KosMatchMark_Dict(mark, names);
  }
  return KosMatchMark_Span(mark, cursor);
}


int KosMatchMark_Columns(
    PyObject *mark,
    PyObject *items,
    PyObject *names,
    PyObject *slots) {

  KosMatchMark *m = (KosMatchMark *) mark;
  Py_ssize_t count = PyTuple_GET_SIZE(names);
  Py_ssize_t passes = PyList_GET_SIZE(items);

  if (PyTuple_GET_SIZE(slots) != count) {
    PyErr_Format(PyExc_SystemError,
                 "match columns name %zd slots for %zd names",
                 PyTuple_GET_SIZE(slots), count);
    return -1;
  }

  for (Py_ssize_t j = 0; j < count; j++) {
    PyObject *name = PyTuple_GET_ITEM(names, j);
    Py_ssize_t i = mark_slot_index(m, PyTuple_GET_ITEM(slots, j));
    if (i < 0)
      return -1;

    PyObject *column = PyList_New(0);
    if (! column)
      return -1;

    /* a pass that left the name unbound contributes nothing */
    for (Py_ssize_t p = 0; p < passes; p++) {
      PyObject *value = PyDict_GetItemWithError(
          PyList_GET_ITEM(items, p), name);
      if (! value) {
        if (! PyErr_Occurred())
          continue;
        Py_DECREF(column);
        return -1;
      }
      if (PyList_Append(column, value) < 0) {
        Py_DECREF(column);
        return -1;
      }
    }

    Py_XSETREF(m->slots[i], column);
  }
  return 0;
}


// The end.
