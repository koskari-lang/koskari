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
   koskari/lib/mark.c

   The Mark type. A Mark is immutable; extending or stripping one
   builds a new Mark. Equality is by datum identity and marks
   equality, and the hash is the mangled key's.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === key === */


/**
   The mangled key for datum under marks: the atom's name followed by
   "#" and the lowercase hex of each mark. New reference.
*/
static PyObject *mark_build_key(PyObject *datum, PyObject *marks) {

  PyObject *key = PyObject_Str(datum);
  if (! key)
    return NULL;

  Py_ssize_t n = PyTuple_GET_SIZE(marks);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *m = PyTuple_GET_ITEM(marks, i);
    unsigned long v = PyLong_AsUnsignedLong(m);
    if (v == (unsigned long) -1 && PyErr_Occurred()) {
      Py_DECREF(key);
      return NULL;
    }
    PyObject *next = PyUnicode_FromFormat("%U#%lx", key, v);
    Py_DECREF(key);
    if (! next)
      return NULL;
    key = next;
  }

  return key;
}


/**
   Coerce marks to a tuple of ints: an int becomes a one-tuple, any
   other sequence is copied. New reference, or NULL with an exception
   set.
*/
static PyObject *mark_coerce_marks(PyObject *marks) {

  if (PyLong_Check(marks))
    return PyTuple_Pack(1, marks);

  PyObject *tup = PySequence_Tuple(marks);
  if (! tup)
    return NULL;

  Py_ssize_t n = PyTuple_GET_SIZE(tup);
  if (! n) {
    Py_DECREF(tup);
    PyErr_SetString(PyExc_ValueError, "Mark requires at least one mark");
    return NULL;
  }

  for (Py_ssize_t i = 0; i < n; i++) {
    if (! PyLong_Check(PyTuple_GET_ITEM(tup, i))) {
      Py_DECREF(tup);
      PyErr_SetString(PyExc_TypeError, "Mark marks must be ints");
      return NULL;
    }
  }

  return tup;
}


/* === type === */


static void mark_dealloc(PyObject *self) {
  KosMark *m = (KosMark *) self;
  Py_CLEAR(m->datum);
  Py_CLEAR(m->marks);
  Py_CLEAR(m->key);
  Py_TYPE(self)->tp_free(self);
}


static PyObject *mark_repr(PyObject *self) {
  return PyUnicode_FromFormat("<koskari mark %U>", KosMark_KEY(self));
}


static PyObject *mark_str(PyObject *self) {
  PyObject *key = KosMark_KEY(self);
  Py_INCREF(key);
  return key;
}


static Py_hash_t mark_hash(PyObject *self) {
  return PyObject_Hash(KosMark_KEY(self));
}


static PyObject *mark_richcompare(PyObject *a, PyObject *b, int op) {

  if (op != Py_EQ && op != Py_NE)
    Py_RETURN_NOTIMPLEMENTED;

  if (! KosMark_Check(b))
    Py_RETURN_NOTIMPLEMENTED;

  int same = 0;
  if (KosMark_DATUM(a) == KosMark_DATUM(b)) {
    same = PyObject_RichCompareBool(
        KosMark_MARKS(a), KosMark_MARKS(b), Py_EQ);
    if (same < 0)
      return NULL;
  }

  if ((op == Py_EQ) == (same != 0))
    Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}


static PyObject *mark_get_datum(PyObject *self, void *closure) {
  (void) closure;
  PyObject *d = KosMark_DATUM(self);
  Py_INCREF(d);
  return d;
}


static PyObject *mark_get_marks(PyObject *self, void *closure) {
  (void) closure;
  PyObject *m = KosMark_MARKS(self);
  Py_INCREF(m);
  return m;
}


static PyObject *mark_get_key(PyObject *self, void *closure) {
  (void) closure;
  return mark_str(self);
}


static PyGetSetDef mark_getset[] = {
  { "datum", mark_get_datum, NULL,
    "The symbol or keyword the template wrote", NULL },
  { "marks", mark_get_marks, NULL,
    "Tuple of the expansions that introduced it, outermost first",
    NULL },
  { "key", mark_get_key, NULL,
    "The mangled name the compiler declares and resolves", NULL },
  { NULL }
};


/**
   Source spelling: the atom under the mark. Printed source hides
   provenance; str() and the key carry it.
*/
static PyObject *mark_kos_repr(PyObject *self, PyObject *unused) {
  (void) unused;
  return KosUtils_Repr(KosMark_DATUM(self));
}


static PyMethodDef mark_methods[] = {
  { "__kos_repr__", mark_kos_repr, METH_NOARGS,
    "Koskari source spelling: the atom under the mark" },
  { NULL }
};


static PyObject *mark_tp_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *datum = NULL;
  PyObject *marks = NULL;
  static char *kwlist[] = { "datum", "marks", NULL };

  (void) type;
  if (! PyArg_ParseTupleAndKeywords(
          args, kwds, "OO:Mark", kwlist, &datum, &marks))
    return NULL;

  return KosMark_New(datum, marks);
}


PyTypeObject KosMarkType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.mark",
  .tp_basicsize = sizeof(KosMark),
  .tp_dealloc = mark_dealloc,
  .tp_repr = mark_repr,
  .tp_str = mark_str,
  .tp_hash = mark_hash,
  .tp_richcompare = mark_richcompare,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = "A template-introduced symbol or keyword with the marks of "
            "the expansions that introduced it",
  .tp_methods = mark_methods,
  .tp_getset = mark_getset,
  .tp_new = mark_tp_new,
};


PyObject *KosMark_New(PyObject *datum, PyObject *marks) {

  if (! (KosSymbol_Check(datum) || KosKeyword_Check(datum))) {
    PyErr_Format(PyExc_TypeError,
                 "Mark datum must be a symbol or keyword, not %R", datum);
    return NULL;
  }

  PyObject *tup = mark_coerce_marks(marks);
  if (! tup)
    return NULL;

  PyObject *key = mark_build_key(datum, tup);
  if (! key) {
    Py_DECREF(tup);
    return NULL;
  }

  KosMark *m = PyObject_New(KosMark, &KosMarkType);
  if (! m) {
    Py_DECREF(tup);
    Py_DECREF(key);
    return NULL;
  }

  Py_INCREF(datum);
  m->datum = datum;
  m->marks = tup;
  m->key = key;
  return (PyObject *) m;
}


/* === unit lifecycle === */


int koskari_mark_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosMarkType))
    return -1;

  if (PyDict_SetItemString(dict, "Mark", (PyObject *) &KosMarkType))
    return -1;

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosMarkType))
    return -1;

  return 0;
}


int koskari_mark_clear(PyObject *mod) {
  (void) mod;
  return 0;
}


// The end.
