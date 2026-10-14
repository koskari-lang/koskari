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
  Included by matcher.c. The steps the MATCH_* instructions call: the
  sequence cursor, the pull, the descent, the tests and the field and
  item reads, each the engine's own body over one element instead of
  a walk (design/interpreter/matcher-bytecode.md).

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/


/* === KosMatchCursor === */


static void match_cursor_dealloc(PyObject *self) {
  KosMatchCursor *c = (KosMatchCursor *) self;
  PyObject_GC_UnTrack(self);
  Py_CLEAR(c->seq);
  PyObject_GC_Del(self);
}


static int match_cursor_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  Py_VISIT(((KosMatchCursor *) self)->seq);
  return 0;
}


PyTypeObject KosMatchCursorType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.MatchCursor",
  .tp_basicsize = sizeof(KosMatchCursor),
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "A match instruction's cursor over a sequence subject",
  .tp_dealloc = match_cursor_dealloc,
  .tp_traverse = match_cursor_traverse,
};


static PyObject *match_cursor_new(PyObject *seq, Py_ssize_t length) {
  KosMatchCursor *c = PyObject_GC_New(KosMatchCursor, &KosMatchCursorType);

  if (! c)
    return NULL;
  Py_INCREF(seq);
  c->seq = seq;
  c->length = length;
  c->index = 0;
  PyObject_GC_Track((PyObject *) c);
  return (PyObject *) c;
}


/* === spine walks === */


/**
   The cell position as a Datum carries it: the pair's position, or
   None. New reference.
*/
static PyObject *step_cell_position(PyObject *cell) {
  PyObject *pos = Py_None;

  if (cell && KosPair_Check(cell) && ! KosNilP(cell) && KosPair_POSITION(cell))
    pos = KosPair_POSITION(cell);
  Py_INCREF(pos);
  return pos;
}


/**
   Datum(car, position) for one spine cell. New reference.
*/
static PyObject *step_box_cell(PyObject *cell, PyObject *car) {
  PyObject *pos = step_cell_position(cell);
  PyObject *out = kos_datum_new(car, pos, Py_None);

  Py_DECREF(pos);
  return out;
}


/**
   Count the cells of the spine from cursor. Returns the count with
   *out_term the terminator, or -1 with no exception set for an
   improper tail, or -2 with an exception set for a looping spine.
*/
static Py_ssize_t step_count_spine(PyObject *cursor, PyObject **out_term) {
  Py_ssize_t n = 0;
  PyObject *slow = cursor;
  PyObject *fast = cursor;

  while (KosPair_Check(fast) && ! KosNilP(fast)) {
    n++;
    fast = KosPair_CDR(fast);
    if (n & 1) {
      slow = KosPair_CDR(slow);
    } else if (fast == slow && KosPair_Check(fast) && ! KosNilP(fast)) {
      PyErr_SetString(PyExc_ValueError, "looping spine in match subject");
      return -2;
    }
  }
  if (! KosNilP(fast))
    return -1;
  *out_term = fast;
  return n;
}


/* === the steps === */


int KosMatch_Descend(PyObject *elem, uint32_t widths, PyObject **out_cursor) {
  PyObject *peeled = matcher_peel_datum(elem);
  int gate = matcher_sequence_gate(peeled);

  if (gate <= 0)
    return gate;

  if (KosPair_Check(peeled) || KosNilP(peeled)) {
    Py_INCREF(peeled);
    *out_cursor = peeled;
    return 1;
  }

  Py_ssize_t have;
  if (PyList_CheckExact(peeled) || PyTuple_CheckExact(peeled)) {
    have = Py_SIZE(peeled);
  } else {
    have = PyObject_Length(peeled);
    if (have < 0)
      return -1;
  }

  uint32_t mn = KOS_MATCH_HI(widths);
  uint32_t mx = KOS_MATCH_LO(widths);
  if (have < (Py_ssize_t) mn
      || (mx != KOS_MATCH_UNBOUNDED8 && have > (Py_ssize_t) mx))
    return 0;

  PyObject *cursor = match_cursor_new(peeled, have);
  if (! cursor)
    return -1;
  *out_cursor = cursor;
  return 1;
}


int KosMatch_Pull(PyObject *cursor, KosMatchStep *out) {
  out->elem = NULL;
  out->cell = NULL;
  out->cursor = NULL;
  out->next = NULL;

  if (KosMatchCursor_Check(cursor)) {
    KosMatchCursor *c = (KosMatchCursor *) cursor;
    PyObject *seq = c->seq;
    PyObject *item;

    if (c->index >= c->length)
      return 0;
    if (PyTuple_CheckExact(seq)) {
      item = PyTuple_GET_ITEM(seq, c->index);
      Py_INCREF(item);
    } else if (PyList_CheckExact(seq)) {
      if (c->index >= PyList_GET_SIZE(seq))
        return 0;
      item = PyList_GET_ITEM(seq, c->index);
      Py_INCREF(item);
    } else {
      item = PySequence_GetItem(seq, c->index);
      if (! item)
        return -1;
    }
    out->cursor = cursor;
    out->elem = item;
    return 1;
  }

  if (KosNilP(cursor) || ! KosPair_Check(cursor))
    return 0;

  out->cell = cursor;
  out->elem = KosPair_CAR(cursor);
  Py_INCREF(out->elem);
  out->next = KosPair_CDR(cursor);
  Py_INCREF(out->next);
  return 1;
}


PyObject *KosMatch_Box(const KosMatchStep *step) {
  if (! step->cell) {
    Py_INCREF(step->elem);
    return step->elem;
  }
  /* the engine reads a spine cell through Datum before it boxes, so
     spliced material is boxed once, at the cell it now sits in */
  return step_box_cell(step->cell, matcher_peel_datum(step->elem));
}


void KosMatch_StepAdvance(KosMatchStep *step) {
  if (step->cursor)
    ((KosMatchCursor *) step->cursor)->index++;
}


void KosMatch_StepClear(KosMatchStep *step) {
  Py_CLEAR(step->elem);
  Py_CLEAR(step->next);
  step->cell = NULL;
  step->cursor = NULL;
}


int KosMatch_End(PyObject *cursor) {
  if (KosMatchCursor_Check(cursor)) {
    KosMatchCursor *c = (KosMatchCursor *) cursor;
    return c->index >= c->length;
  }
  return KosNilP(cursor);
}


/**
   The consumed range of a sequence cursor as a tuple. New reference.
*/
static PyObject *step_seq_slice(
    PyObject *seq,
    Py_ssize_t start,
    Py_ssize_t end) {

  if (PyTuple_CheckExact(seq))
    return PyTuple_GetSlice(seq, start, end);

  PyObject *t = PyTuple_New(end - start);
  if (! t)
    return NULL;
  for (Py_ssize_t i = start; i < end; i++) {
    PyObject *item;
    if (PyList_CheckExact(seq)) {
      item = PyList_GET_ITEM(seq, i);
      Py_INCREF(item);
    } else {
      item = PySequence_GetItem(seq, i);
      if (! item) {
        Py_DECREF(t);
        return NULL;
      }
    }
    PyTuple_SET_ITEM(t, i - start, item);
  }
  return t;
}


int KosMatch_Rest(
    PyObject *cursor,
    uint32_t counts,
    PyObject **out_value,
    PyObject **out_next) {

  Py_ssize_t min_tail = (Py_ssize_t) KOS_MATCH_HI(counts);
  Py_ssize_t suffix = (Py_ssize_t) KOS_MATCH_LO(counts);

  *out_value = NULL;
  *out_next = NULL;

  if (KosMatchCursor_Check(cursor)) {
    KosMatchCursor *c = (KosMatchCursor *) cursor;
    Py_ssize_t avail = c->length - c->index;

    if (avail < suffix)
      return 0;
    Py_ssize_t consume = avail - suffix;
    if (consume < min_tail)
      return 0;

    PyObject *value = step_seq_slice(c->seq, c->index, c->index + consume);
    if (! value)
      return -1;
    c->index += consume;
    *out_value = value;
    return 1;
  }

  PyObject *term = NULL;
  Py_ssize_t n = step_count_spine(cursor, &term);
  if (n == -2)
    return -1;
  if (n < 0 || n < suffix)
    return 0;
  Py_ssize_t consume = n - suffix;
  if (consume < min_tail)
    return 0;

  if (consume == 0) {
    /* the empty remainder: the terminator, as the engine binds it */
    PyObject *value = kos_datum_new(term, Py_None, Py_None);
    if (! value)
      return -1;
    *out_value = value;
    Py_INCREF(cursor);
    *out_next = cursor;
    return 1;
  }

  if (suffix == 0) {
    PyObject *value = step_box_cell(cursor, cursor);
    if (! value)
      return -1;
    *out_value = value;
    Py_INCREF(term);
    *out_next = term;
    return 1;
  }

  PyObject *t = PyTuple_New(consume);
  if (! t)
    return -1;
  PyObject *cell = cursor;
  for (Py_ssize_t i = 0; i < consume; i++) {
    PyObject *boxed = step_box_cell(cell, KosPair_CAR(cell));
    if (! boxed) {
      Py_DECREF(t);
      return -1;
    }
    PyTuple_SET_ITEM(t, i, boxed);
    cell = KosPair_CDR(cell);
  }
  *out_value = t;
  Py_INCREF(cell);
  *out_next = cell;
  return 1;
}


int KosMatch_TypeTest(PyObject *elem, PyObject *type) {
  return matcher_isinstance(matcher_peel_datum(elem), type);
}


int KosMatch_CoreTypeTest(PyObject *elem, uint32_t core) {
  PyObject *got = matcher_peel_datum(elem);

  switch ((KosCoreType) core) {
  case KOS_CORE_PAIR:
    return KosPair_Check(got) ? 1 : 0;
  case KOS_CORE_SYMBOL:
    return KosSymbol_Check(got) ? 1 : 0;
  case KOS_CORE_KEYWORD:
    return KosKeyword_Check(got) ? 1 : 0;
  case KOS_CORE_INT:
    return PyLong_Check(got) ? 1 : 0;
  case KOS_CORE_STR:
    return PyUnicode_Check(got) ? 1 : 0;
  case KOS_CORE_TUPLE:
    return PyTuple_Check(got) ? 1 : 0;
  case KOS_CORE_LIST:
    return PyList_Check(got) ? 1 : 0;
  case KOS_CORE_COUNT:
    break;
  }
  PyErr_Format(PyExc_SystemError, "unknown core type ordinal %u", core);
  return -1;
}


int KosMatch_LiteralTest(PyObject *elem, PyObject *lit) {
  PyObject *got = matcher_peel_datum(elem);

  if (lit == got)
    return 1;
  /* a literal names an atom by its bare spelling, so a gensym matches
     the symbol it spells */
  if (KosGensym_Check(got) && KosGensym_BASE(got) == lit)
    return 1;
  return PyObject_RichCompareBool(lit, got, Py_EQ);
}


#if PY_MINOR_VERSION < 10
/**
   Python 3.9 predates __match_args__ (PEP 634 added it, and dataclass
   / namedtuple generation of it, in 3.10). Fill that one API gap
   predictably: synthesize the 3.10-equivalent tuple for a dataclass
   (its init fields, via the public dataclasses.fields) or a namedtuple
   (_fields). Gated to < 3.10 only: on newer Pythons an absent
   __match_args__ on a dataclass means @dataclass(match_args=False),
   a deliberate opt-out this fallback must never override.

   Returns a new tuple reference; NULL with no exception set when cls
   is neither shape (the caller raises its usual TypeError); NULL with
   an exception set on internal failure.
*/
static PyObject *synth_match_args_39(PyObject *cls) {
  static PyObject *dc_fields = NULL;

  int is_dc = PyObject_HasAttrString(cls, "__dataclass_fields__");
  if (is_dc) {
    if (! dc_fields) {
      PyObject *dc = PyImport_ImportModule("dataclasses");
      if (! dc)
        return NULL;
      dc_fields = PyObject_GetAttrString(dc, "fields");
      Py_DECREF(dc);
      if (! dc_fields)
        return NULL;
    }

    PyObject *flds = PyObject_CallOneArg(dc_fields, cls);
    if (! flds)
      return NULL;
    PyObject *fast = PySequence_Fast(
        flds, "dataclasses.fields must return a sequence");
    Py_DECREF(flds);
    if (! fast)
      return NULL;

    PyObject *names = PyList_New(0);
    if (! names) {
      Py_DECREF(fast);
      return NULL;
    }

    Py_ssize_t count = PySequence_Fast_GET_SIZE(fast);
    for (Py_ssize_t i = 0; i < count; i++) {
      PyObject *fld = PySequence_Fast_GET_ITEM(fast, i);
      PyObject *init = PyObject_GetAttrString(fld, "init");
      if (! init)
        goto dc_fail;
      int wanted = PyObject_IsTrue(init);
      Py_DECREF(init);
      if (wanted < 0)
        goto dc_fail;
      if (! wanted)
        continue;
      PyObject *name = PyObject_GetAttrString(fld, "name");
      if (! name)
        goto dc_fail;
      int rc = PyList_Append(names, name);
      Py_DECREF(name);
      if (rc < 0)
        goto dc_fail;
    }

    Py_DECREF(fast);
    PyObject *result = PyList_AsTuple(names);
    Py_DECREF(names);
    return result;

  dc_fail:
    Py_DECREF(fast);
    Py_DECREF(names);
    return NULL;
  }

  PyObject *nt_fields = PyObject_GetAttrString(cls, "_fields");
  if (! nt_fields) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError))
      PyErr_Clear();
    return NULL;
  }
  if (PyTuple_Check(nt_fields))
    return nt_fields;
  Py_DECREF(nt_fields);
  return NULL;
}
#endif


/**
   cls.__match_args__ as a new tuple reference, with the 3.9 synthesis
   for dataclasses and namedtuples. NULL with an exception set when the
   class has none or it is not a tuple.
*/
static PyObject *step_match_args(PyObject *cls, Py_ssize_t npos) {
  PyObject *match_args = PyObject_GetAttr(cls, kos_match_args_name);

  if (! match_args) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError))
      PyErr_Clear();
    else
      return NULL;
#if PY_MINOR_VERSION < 10
    match_args = synth_match_args_39(cls);
    if (! match_args && PyErr_Occurred())
      return NULL;
#endif
    if (! match_args) {
      PyErr_Format(PyExc_TypeError,
                   "%R has no __match_args__ but %zd positional field "
                   "pattern(s) were supplied",
                   cls, npos);
      return NULL;
    }
  }
  if (! PyTuple_Check(match_args)) {
    PyErr_Format(PyExc_TypeError,
                 "%R.__match_args__ must be a tuple (got %.200s)",
                 cls, Py_TYPE(match_args)->tp_name);
    Py_DECREF(match_args);
    return NULL;
  }
  return match_args;
}


int KosMatch_ClassTest(PyObject *elem, PyObject *cls, Py_ssize_t npos) {
  int isinst = matcher_isinstance(elem, cls);

  if (isinst <= 0)
    return isinst;
  if (npos <= 0)
    return 1;

  PyObject *match_args = step_match_args(cls, npos);
  if (! match_args)
    return -1;
  if (PyTuple_GET_SIZE(match_args) < npos) {
    PyErr_Format(PyExc_TypeError,
                 "%R.__match_args__ has %zd entries; cannot satisfy "
                 "%zd positional field pattern(s)",
                 cls, PyTuple_GET_SIZE(match_args), npos);
    Py_DECREF(match_args);
    return -1;
  }
  Py_DECREF(match_args);
  return 1;
}


int KosMatch_FieldKw(PyObject *elem, PyObject *name, PyObject **out) {
  PyObject *val = PyObject_GetAttr(elem, name);

  if (! val) {
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
      PyErr_Clear();
      return 0;
    }
    return -1;
  }
  *out = val;
  return 1;
}


int KosMatch_FieldPos(
    PyObject *elem,
    PyObject *cls,
    Py_ssize_t pos,
    PyObject **out) {

  PyObject *match_args = step_match_args(cls, pos + 1);
  if (! match_args)
    return -1;
  if (pos >= PyTuple_GET_SIZE(match_args)) {
    PyErr_Format(PyExc_TypeError,
                 "%R.__match_args__ has %zd entries; cannot satisfy "
                 "%zd positional field pattern(s)",
                 cls, PyTuple_GET_SIZE(match_args), pos + 1);
    Py_DECREF(match_args);
    return -1;
  }

  PyObject *attr_name = PyTuple_GET_ITEM(match_args, pos);
  if (! PyUnicode_Check(attr_name)) {
    PyErr_Format(PyExc_TypeError,
                 "__match_args__[%zd] must be str (got %.200s)",
                 pos, Py_TYPE(attr_name)->tp_name);
    Py_DECREF(match_args);
    return -1;
  }

  int r = KosMatch_FieldKw(elem, attr_name, out);
  Py_DECREF(match_args);
  return r;
}


/* Probe a mapping for key without changing it. An exact dict is read
   directly; any other mapping answers through its own get() with a
   private default, as the Python match statement does, so a
   defaultdict or another __missing__ mapping neither inserts nor
   invents the key. New reference; NULL with no error on a miss, NULL
   with an error on failure. */
static PyObject *map_probe(PyObject *mapping, PyObject *key) {
  if (PyDict_CheckExact(mapping)) {
    PyObject *val = PyDict_GetItemWithError(mapping, key);
    Py_XINCREF(val);
    return val;
  }

  static PyObject *absent = NULL;
  if (! absent) {
    absent = PyObject_CallNoArgs((PyObject *) &PyBaseObject_Type);
    if (! absent)
      return NULL;
  }

  PyObject *val = PyObject_CallMethod(mapping, "get", "OO", key, absent);
  if (val == absent) {
    Py_DECREF(val);
    return NULL;
  }
  return val;
}


int KosMatch_MapTest(PyObject *elem) {
  return matcher_mapping_gate(matcher_peel_datum(elem));
}


int KosMatch_Item(PyObject *elem, PyObject *key, PyObject **out) {
  PyObject *val = map_probe(matcher_peel_datum(elem), key);

  if (! val)
    return PyErr_Occurred() ? -1 : 0;
  *out = val;
  return 1;
}



/* === SAME, positions, COLLECT === */


int KosMatch_Available(PyObject *cursor, Py_ssize_t needed) {
  if (KosMatchCursor_Check(cursor)) {
    KosMatchCursor *c = (KosMatchCursor *) cursor;
    return (c->length - c->index) >= needed;
  }
  PyObject *cell = cursor;
  for (Py_ssize_t i = 0; i < needed; i++) {
    if (! KosPair_Check(cell) || KosNilP(cell))
      return 0;
    cell = KosPair_CDR(cell);
  }
  return 1;
}


int KosMatch_SameTest(PyObject *elem, PyObject *prev) {
  PyObject *got = matcher_peel_datum(elem);

  prev = matcher_peel_datum(prev);
  if (KosDatumFrame_Check(got) || KosDatumFrame_Check(prev))
    return 0;
  if (got == prev)
    return 1;
  return PyObject_RichCompareBool(got, prev, Py_EQ);
}


PyObject *KosMatch_Position(PyObject *cursor) {
  if (KosMatchCursor_Check(cursor))
    return PyLong_FromSsize_t(((KosMatchCursor *) cursor)->index);
  Py_INCREF(cursor);
  return cursor;
}


int KosMatch_Reset(PyObject *cursor, PyObject *saved, PyObject **out_cursor) {
  *out_cursor = NULL;
  if (KosMatchCursor_Check(cursor)) {
    Py_ssize_t idx = PyLong_AsSsize_t(saved);
    if (idx == -1 && PyErr_Occurred())
      return -1;
    ((KosMatchCursor *) cursor)->index = idx;
    return 0;
  }
  Py_INCREF(saved);
  *out_cursor = saved;
  return 0;
}


int KosMatch_Collect(PyObject *cursor, PyObject *list, PyObject **out_next) {
  *out_next = NULL;

  if (KosMatchCursor_Check(cursor)) {
    KosMatchCursor *c = (KosMatchCursor *) cursor;
    while (c->index < c->length) {
      KosMatchStep step;
      int r = KosMatch_Pull(cursor, &step);
      if (r < 0)
        return -1;
      if (! r)
        break;
      int rc = PyList_Append(list, step.elem);
      KosMatch_StepClear(&step);
      if (rc < 0)
        return -1;
      c->index++;
    }
    return 1;
  }

  PyObject *term = NULL;
  Py_ssize_t n = step_count_spine(cursor, &term);
  if (n == -2)
    return -1;
  if (n < 0)
    return 0;

  PyObject *cell = cursor;
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *boxed = step_box_cell(cell, matcher_peel_datum(KosPair_CAR(cell)));
    if (! boxed)
      return -1;
    int rc = PyList_Append(list, boxed);
    Py_DECREF(boxed);
    if (rc < 0)
      return -1;
    cell = KosPair_CDR(cell);
  }
  Py_INCREF(term);
  *out_next = term;
  return 1;
}


/* === captures === */


/**
   The cells of a pair span from start to the cursor's cell, counted;
   -1 when the walk never reaches the cursor.
*/
static Py_ssize_t step_span_count(PyObject *start, PyObject *cursor) {
  Py_ssize_t n = 0;
  PyObject *cell = start;

  while (cell != cursor) {
    if (! KosPair_Check(cell) || KosNilP(cell))
      return -1;
    n++;
    cell = KosPair_CDR(cell);
  }
  return n;
}


/**
   The source a DatumFrame records for a span: the start cell itself
   when the span runs to the spine's end or is empty, the peeled car
   for one cell, else a tuple of the peeled cars. New reference.
*/
static PyObject *step_span_source(
    PyObject *start,
    PyObject *cursor,
    Py_ssize_t n) {

  if (n <= 0 || KosNilP(cursor)) {
    Py_INCREF(start);
    return start;
  }
  if (n == 1) {
    PyObject *car = matcher_peel_datum(KosPair_CAR(start));
    Py_INCREF(car);
    return car;
  }
  PyObject *t = PyTuple_New(n);
  if (! t)
    return NULL;
  PyObject *cell = start;
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *car = matcher_peel_datum(KosPair_CAR(cell));
    Py_INCREF(car);
    PyTuple_SET_ITEM(t, i, car);
    cell = KosPair_CDR(cell);
  }
  return t;
}


/**
   Fill a fresh container with the bound values under their names.
   0, or -1 with an exception set.
*/
static int step_fill_captures(
    PyObject *out,
    PyObject *names,
    PyObject *const *values) {

  Py_ssize_t n = PyTuple_GET_SIZE(names);

  for (Py_ssize_t i = 0; i < n; i++) {
    if (! values[i])
      continue;
    if (PyDict_SetItem(out, PyTuple_GET_ITEM(names, i), values[i]) < 0)
      return -1;
  }
  return 0;
}


PyObject *KosMatch_Captures(
    PyObject *names,
    PyObject *const *values,
    PyObject *start,
    PyObject *cursor) {

  if (! (KosPair_Check(start) || KosNilP(start))) {
    PyObject *out = PyDict_New();
    if (! out)
      return NULL;
    if (step_fill_captures(out, names, values) < 0) {
      Py_DECREF(out);
      return NULL;
    }
    return out;
  }

  Py_ssize_t n = step_span_count(start, cursor);
  if (n < 0) {
    PyErr_SetString(PyExc_SystemError,
                    "match captures: the start is not on the cursor's spine");
    return NULL;
  }

  PyObject *out = kos_datum_frame_new();
  if (! out)
    return NULL;
  if (step_fill_captures(out, names, values) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  PyObject *source = step_span_source(start, cursor, n);
  if (! source) {
    Py_DECREF(out);
    return NULL;
  }
  PyObject *position = step_cell_position(start);
  KosDatumFrame *f = (KosDatumFrame *) out;
  PyObject *old_source = f->source;
  PyObject *old_position = f->position;
  f->source = source;
  f->position = position;
  Py_DECREF(old_source);
  Py_DECREF(old_position);
  return out;
}


PyObject *KosMatch_Iteration(
    PyObject *names,
    PyObject *const *values,
    PyObject *start,
    PyObject *cursor) {

  Py_ssize_t count = PyTuple_GET_SIZE(names);
  for (Py_ssize_t i = 0; i < count; i++) {
    if (values[i])
      return KosMatch_Captures(names, values, start, cursor);
  }

  if (KosMatchCursor_Check(cursor)) {
    KosMatchCursor *c = (KosMatchCursor *) cursor;
    Py_ssize_t from = PyLong_AsSsize_t(start);
    if (from == -1 && PyErr_Occurred())
      return NULL;
    Py_ssize_t span = c->index - from;
    if (span == 1)
      return PySequence_GetItem(c->seq, from);
    if (span > 1)
      return step_seq_slice(c->seq, from, c->index);
  } else {
    Py_ssize_t span = step_span_count(start, cursor);
    if (span == 1)
      return step_box_cell(start, matcher_peel_datum(KosPair_CAR(start)));
    if (span > 1) {
      PyObject *t = PyTuple_New(span);
      if (! t)
        return NULL;
      PyObject *cell = start;
      for (Py_ssize_t i = 0; i < span; i++) {
        PyObject *boxed = step_box_cell(
            cell, matcher_peel_datum(KosPair_CAR(cell)));
        if (! boxed) {
          Py_DECREF(t);
          return NULL;
        }
        PyTuple_SET_ITEM(t, i, boxed);
        cell = KosPair_CDR(cell);
      }
      return t;
    }
  }

  PyErr_SetString(PyExc_RuntimeError,
                  "internal: REPEAT iteration matched without advancing "
                  "cursor");
  return NULL;
}


// The end.
