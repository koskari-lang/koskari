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
  Included by matcher.c. The Datum, DatumIter and DatumFrame types that
  carry captured material with its position.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === Datum === */


/* Freed Datums chain through datum, up to the cap; kos_datum_new sets
   every field of one it takes from the chain. */
#define DATUM_MAX_FREE KOS_FREELIST_CAP(64)

static KosDatum *datum_free_list = NULL;
static int datum_free_count = 0;


static void match_datum_free_list_clear(void) {
  while (datum_free_list) {
    KosDatum *d = datum_free_list;
    datum_free_list = (KosDatum *) d->datum;
    PyObject_GC_Del(d);
  }
  datum_free_count = 0;
}


static PyObject *kos_datum_new(
    PyObject *datum,
    PyObject *position,
    PyObject *binding);

typedef struct {
  PyObject_HEAD
  PyObject *current;   /* KosPair-or-nil; tail pointer */
  PyObject *fallback;  /* fallback position */
  PyObject *binding;   /* capture slot name or Py_None */
} KosDatumIter;


static void datum_iter_dealloc(PyObject *self) {
  KosDatumIter *it = (KosDatumIter *) self;
  PyObject_GC_UnTrack(self);
  Py_CLEAR(it->current);
  Py_CLEAR(it->fallback);
  Py_CLEAR(it->binding);
  Py_TYPE(self)->tp_free(self);
}


static int datum_iter_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosDatumIter *it = (KosDatumIter *) self;
  Py_VISIT(it->current);
  Py_VISIT(it->fallback);
  Py_VISIT(it->binding);
  return 0;
}


static PyObject *datum_iter_iternext(PyObject *self) {
  KosDatumIter *it = (KosDatumIter *) self;

  if (! it->current || KosNilP(it->current)) {
    Py_CLEAR(it->current);
    return NULL;
  }

  if (! KosPair_Check(it->current)) {
    /* Improper tail; terminate */
    Py_CLEAR(it->current);
    return NULL;
  }

  PyObject *cell = it->current;
  PyObject *elem = KosPair_CAR(cell);
  PyObject *pos = KosPair_POSITION(cell);
  if (! pos || pos == Py_None) {
    pos = it->fallback;
  }
  if (! pos)
    pos = Py_None;

  /* the cell owns elem and pos; hold them before the cell can go */
  Py_INCREF(elem);
  Py_INCREF(pos);

  it->current = KosPair_CDR(cell);
  Py_INCREF(it->current);
  Py_DECREF(cell);

  Py_INCREF(it->binding);
  PyObject *out = kos_datum_new(elem, pos, it->binding);
  Py_DECREF(elem);
  Py_DECREF(pos);
  Py_DECREF(it->binding);
  return out;
}


static PyTypeObject KosDatumIterType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.DatumIter",
  .tp_basicsize = sizeof(KosDatumIter),
  .tp_dealloc = datum_iter_dealloc,
  .tp_traverse = datum_iter_traverse,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_iter = PyObject_SelfIter,
  .tp_iternext = datum_iter_iternext,
};


static void datum_dealloc(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  PyObject_GC_UnTrack(self);
  Py_CLEAR(d->datum);
  Py_CLEAR(d->position);
  Py_CLEAR(d->binding);

  if (datum_free_count < DATUM_MAX_FREE) {
    KOS_TRACE_FL_RETURN("datum", 0);
    d->datum = (PyObject *) datum_free_list;
    datum_free_list = d;
    datum_free_count++;

  } else {
    KOS_TRACE_FL_RETURN("datum", 1);
    PyObject_GC_Del(self);
  }
}


static int datum_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosDatum *d = (KosDatum *) self;
  Py_VISIT(d->datum);
  Py_VISIT(d->position);
  Py_VISIT(d->binding);
  return 0;
}


static PyObject *datum_repr(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  return PyUnicode_FromFormat(
      "<koskari datum %R position=%R binding=%R>",
      d->datum, d->position, d->binding);
}


static PyObject *datum_get_datum(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->datum);
  return d->datum;
}


static PyObject *datum_get_position(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->position);
  return d->position;
}


static PyObject *datum_get_binding(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  Py_INCREF(d->binding);
  return d->binding;
}


static PyObject *datum_get_car(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  return PyObject_GetAttrString(d->datum, "car");
}


static PyObject *datum_get_cdr(PyObject *self, void *closure) {
  (void) closure;
  KosDatum *d = (KosDatum *) self;
  return PyObject_GetAttrString(d->datum, "cdr");
}


static PyObject *datum_iter(PyObject *self) {
  KosDatum *d = (KosDatum *) self;
  if (! KosPair_Check(d->datum)) {
    return PyErr_Format(
        PyExc_TypeError,
        "cannot iterate Datum with non-pair datum: %R",
        d->datum);
  }

  KosDatumIter *it = PyObject_GC_New(KosDatumIter, &KosDatumIterType);
  if (! it)
    return NULL;

  Py_INCREF(d->datum);
  Py_INCREF(d->position);
  Py_INCREF(d->binding);
  it->current = d->datum;
  it->fallback = d->position;
  it->binding = d->binding;
  PyObject_GC_Track((PyObject *) it);
  return (PyObject *) it;
}


static PyObject *datum_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  PyObject *datum;
  PyObject *position = Py_None;
  PyObject *binding = Py_None;
  static char *kwlist[] = {"datum", "position", "binding", NULL};

  (void) type;
  if (! PyArg_ParseTupleAndKeywords(
          args, kwds, "O|OO:Datum", kwlist,
          &datum, &position, &binding))
    return NULL;

  return kos_datum_new(datum, position, binding);
}


static PyGetSetDef datum_getset[] = {
  { "datum", datum_get_datum, NULL,
    "Matched value", NULL },
  { "position", datum_get_position, NULL,
    "Source position from enclosing pair cell, or None", NULL },
  { "binding", datum_get_binding, NULL,
    "Capture slot name (str) or None", NULL },
  { "car", datum_get_car, NULL,
    "Underlying datum car (delegates to pair)", NULL },
  { "cdr", datum_get_cdr, NULL,
    "Underlying datum cdr (delegates to pair)", NULL },
  { NULL }
};


PyTypeObject KosDatumType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.datum",
  .tp_basicsize = sizeof(KosDatum),
  .tp_dealloc = datum_dealloc,
  .tp_repr = datum_repr,
  .tp_iter = datum_iter,
  .tp_traverse = datum_traverse,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Matched value with optional KosPair source position and "
            "capture binding name",
  .tp_getset = datum_getset,
  .tp_new = datum_tp_new,
};


static PyObject *kos_datum_new(
    PyObject *datum,
    PyObject *position,
    PyObject *binding) {

  KosDatum *d = datum_free_list;

  if (d) {
    datum_free_list = (KosDatum *) d->datum;
    datum_free_count--;
    KOS_TRACE_FL_ALLOC("datum", 1);
    _Py_NewReference((PyObject *) d);

  } else {
    KOS_TRACE_FL_ALLOC("datum", 0);
    d = PyObject_GC_New(KosDatum, &KosDatumType);
    if (! d)
      return NULL;
  }

  if (! binding)
    binding = Py_None;
  Py_INCREF(datum);
  Py_INCREF(position);
  Py_INCREF(binding);
  d->datum = datum;
  d->position = position;
  d->binding = binding;
  PyObject_GC_Track((PyObject *) d);
  return (PyObject *) d;
}


/* === DatumFrame === */


static int datum_frame_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_VISIT(f->source);
  Py_VISIT(f->position);
  if (PyDict_Type.tp_traverse)
    return PyDict_Type.tp_traverse(self, visit, arg);
  return 0;
}


static int datum_frame_clear(PyObject *self) {

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_CLEAR(f->source);
  Py_CLEAR(f->position);
  if (PyDict_Type.tp_clear)
    return PyDict_Type.tp_clear(self);
  return 0;
}


static void datum_frame_dealloc(PyObject *self) {

  datum_frame_clear(self);
  PyDict_Type.tp_dealloc(self);
}


static PyObject *datum_frame_repr(PyObject *self) {

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_ssize_t nslots = PyDict_Size(self);
  return PyUnicode_FromFormat(
      "<DatumFrame source=%R position=%R captures=%zd>",
      f->source, f->position, nslots);
}


static PyObject *datum_frame_get_source(PyObject *self, void *closure) {
  (void) closure;
  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_INCREF(f->source);
  return f->source;
}


static PyObject *datum_frame_get_position(PyObject *self, void *closure) {
  (void) closure;
  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_INCREF(f->position);
  return f->position;
}


/* Every DatumFrame starts with source and position set to None, from
   whichever side constructs it: the getters and the frame writers in
   mark.c rely on both fields being real objects. */
static PyObject *datum_frame_new(PyTypeObject *type, PyObject *args,
                                 PyObject *kwds) {
  PyObject *self = PyDict_Type.tp_new(type, args, kwds);
  if (! self)
    return NULL;

  KosDatumFrame *f = (KosDatumFrame *) self;
  Py_INCREF(Py_None);
  f->source = Py_None;
  Py_INCREF(Py_None);
  f->position = Py_None;
  return self;
}


static PyGetSetDef datum_frame_getset[] = {
  { "source", datum_frame_get_source, NULL,
    "Matched source represented by this capture frame", NULL },
  { "position", datum_frame_get_position, NULL,
    "Source position from the frame start, or None", NULL },
  { NULL }
};


PyTypeObject KosDatumFrameType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.DatumFrame",
  .tp_basicsize = sizeof(KosDatumFrame),
  .tp_dealloc = datum_frame_dealloc,
  .tp_repr = datum_frame_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Capture dict with source provenance",
  .tp_new = datum_frame_new,
  .tp_traverse = datum_frame_traverse,
  .tp_clear = datum_frame_clear,
  .tp_getset = datum_frame_getset,
  .tp_base = &PyDict_Type,
};


/**
   Borrow through outer ``Datum`` layers for KosPair spine reads. The
   underlying pair tree and the boxes are unchanged; only matching
   sees the inner value. A gensym is a symbol and is never peeled: it
   is the identity a capture hands back to the compiler.
*/
static PyObject *matcher_peel_datum(PyObject *obj) {
  while (obj && KosDatum_Check(obj))
    obj = ((KosDatum *) obj)->datum;
  return obj;
}

// The end.
