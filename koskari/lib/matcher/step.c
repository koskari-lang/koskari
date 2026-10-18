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
  Included by matcher.c. The KosMatchCursor type and its freelist, the
  sequence and spine reads the cursor and mark steps share, and the
  element tests and the field and item reads the MATCH_* instructions
  call, each over one element (design/interpreter/matcher-core.md).

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/


/* === KosMatchCursor === */


/* The cursor is untracked: it is reached only from an operand stack
   entry, never from Python, and the peak count is the pattern's
   nesting. Freed cursors chain through seq, up to the cap. */
#define CURSOR_MAX_FREE KOS_FREELIST_CAP(8)

static KosMatchCursor *cursor_free_list = NULL;
static int cursor_free_count = 0;


static void match_cursor_dealloc(PyObject *self) {
  KosMatchCursor *c = (KosMatchCursor *) self;

  Py_DECREF(c->seq);
  if (cursor_free_count < CURSOR_MAX_FREE) {
    KOS_TRACE_FL_RETURN("cursor", 0);
    c->seq = (PyObject *) cursor_free_list;
    cursor_free_list = c;
    cursor_free_count++;

  } else {
    KOS_TRACE_FL_RETURN("cursor", 1);
    Py_TYPE(self)->tp_free(self);
  }
}


static void match_cursor_free_list_clear(void) {
  while (cursor_free_list) {
    KosMatchCursor *c = cursor_free_list;
    cursor_free_list = (KosMatchCursor *) c->seq;
    PyObject_Free(c);
  }
  cursor_free_count = 0;
}


PyTypeObject KosMatchCursorType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.MatchCursor",
  .tp_basicsize = sizeof(KosMatchCursor),
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "A match instruction's cursor over a sequence subject",
  .tp_dealloc = match_cursor_dealloc,
};


static PyObject *match_cursor_new(
    PyObject *seq,
    Py_ssize_t length,
    Py_ssize_t index,
    unsigned char kind) {

  KosMatchCursor *c;

  if (cursor_free_list) {
    c = cursor_free_list;
    cursor_free_list = (KosMatchCursor *) c->seq;
    cursor_free_count--;
    KOS_TRACE_FL_ALLOC("cursor", 1);
    _Py_NewReference((PyObject *) c);

  } else {
    KOS_TRACE_FL_ALLOC("cursor", 0);
    c = PyObject_New(KosMatchCursor, &KosMatchCursorType);
    if (! c)
      return NULL;
  }

  Py_INCREF(seq);
  c->seq = seq;
  c->length = length;
  c->index = index;
  c->reserve = 0;
  c->kind = kind;
  return (PyObject *) c;
}


/**
   The element of seq at index. New reference.
*/
static PyObject *step_seq_item(PyObject *seq, Py_ssize_t index) {
  PyObject *item;

  if (PyTuple_CheckExact(seq)) {
    item = PyTuple_GET_ITEM(seq, index);
    Py_INCREF(item);
  } else if (PyList_CheckExact(seq)) {
    item = PyList_GET_ITEM(seq, index);
    Py_INCREF(item);
  } else {
    item = PySequence_GetItem(seq, index);
  }
  return item;
}


/**
   The consumed range of a sequence cursor as a tuple; a list or list
   subclass is read within its live size, so a shrink under user code
   shortens the range instead of reading past the end. New reference.
*/
static PyObject *step_seq_slice(
    PyObject *seq,
    Py_ssize_t start,
    Py_ssize_t end) {

  if (PyTuple_CheckExact(seq))
    return PyTuple_GetSlice(seq, start, end);
  if (PyList_Check(seq)) {
    Py_ssize_t have = PyList_GET_SIZE(seq);
    if (end > have)
      end = have;
    if (start > end)
      start = end;
  }

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


/* === the element tests === */


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

  /* an exact int or str against one of its own exact type: the type's
     own comparison answers, with no reflected operand or subclass to
     consult */
  PyTypeObject *t = Py_TYPE(lit);
  if (Py_TYPE(got) == t && (t == &PyLong_Type || t == &PyUnicode_Type)) {
    PyObject *res = t->tp_richcompare(lit, got, Py_EQ);
    if (! res)
      return -1;
    int r = (res == Py_True);
    Py_DECREF(res);
    return r;
  }
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
  int is_dc = PyObject_HasAttrString(cls, "__dataclass_fields__");
  if (is_dc) {
    if (! kos_dc_fields) {
      PyObject *dc = PyImport_ImportModule("dataclasses");
      if (! dc)
        return NULL;
      kos_dc_fields = PyObject_GetAttrString(dc, "fields");
      Py_DECREF(dc);
      if (! kos_dc_fields)
        return NULL;
    }

    PyObject *flds = PyObject_CallOneArg(kos_dc_fields, cls);
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
   cls.__match_args__ as a new tuple reference: held, the tuple given,
   which a later read of an attribute may drop from the class; else read
   from cls, with the 3.9 synthesis for dataclasses and namedtuples.
   NULL with an exception set when the class has none or it is not a
   tuple.
*/
static PyObject *step_match_args(
    PyObject *cls,
    Py_ssize_t npos,
    PyObject *held) {

  if (held) {
    Py_INCREF(held);
    return held;
  }

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


PyObject *KosMatch_PlainMatchArgs(PyObject *cls) {
  if (Py_TYPE(cls) != &PyType_Type)
    return NULL;

  PyObject *match_args = PyType_Lookup((PyTypeObject *) cls,
                                       kos_match_args_name);
  if (match_args && PyTuple_CheckExact(match_args))
    return match_args;
  return NULL;
}


int KosMatch_ClassArgs(
    PyObject *cls,
    Py_ssize_t npos,
    PyObject *match_args) {

  if (npos <= 0)
    return 0;

  match_args = step_match_args(cls, npos, match_args);
  if (! match_args)
    return -1;

  Py_ssize_t have = PyTuple_GET_SIZE(match_args);
  Py_DECREF(match_args);
  if (have < npos) {
    PyErr_Format(PyExc_TypeError,
                 "%R.__match_args__ has %zd entries; cannot satisfy "
                 "%zd positional field pattern(s)",
                 cls, have, npos);
    return -1;
  }
  return 0;
}


int KosMatch_FieldKw(PyObject *elem, PyObject *name, PyObject **out) {
  PyObject *val = PyObject_GetAttr(matcher_peel_datum(elem), name);

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
    PyObject *match_args,
    PyObject **out) {

  match_args = step_match_args(cls, pos + 1, match_args);
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

  PyObject *val = PyObject_CallMethod(
      mapping, "get", "OO", key, kos_map_absent);
  if (val == kos_map_absent) {
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


int KosMatch_SameTest(PyObject *elem, PyObject *prev) {
  PyObject *got = matcher_peel_datum(elem);

  prev = matcher_peel_datum(prev);
  if (KosDatumFrame_Check(got) || KosDatumFrame_Check(prev))
    return 0;
  if (got == prev)
    return 1;

  /* the comparison may run user code; prev is held across it, the
     element by its stack entry */
  Py_INCREF(prev);
  int r = PyObject_RichCompareBool(got, prev, Py_EQ);
  Py_DECREF(prev);
  return r;
}


// The end.
