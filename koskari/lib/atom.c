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
   Interned atom types: symbol, keyword, and gensym

   Symbols and keywords are interned string-like objects. Each unique
   name maps to exactly one instance in memory, allowing for fast
   identity comparisons. A gensym is a symbol scoped by expansion
   marks, interned in the symbol table under its derived spelling.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "lib.h"


/* === string constants === */


static PyObject *_str_colon = NULL;
static PyObject *_str_rsplit = NULL;
static PyObject *_str_split = NULL;
static PyObject *_str_strip = NULL;


/* === interned atom internals === */


static PyObject *intern_syms = NULL;
static PyObject *intern_kwds = NULL;


static PyObject *atom_new(
    PyObject *name,
    PyObject *intern,
    PyTypeObject *type) {

  if (! name) {
    PyErr_SetString(PyExc_TypeError, "interned atom requires a name");
    return NULL;

  } else if (! PyUnicode_CheckExact(name)) {
    name = PyObject_Str(name);
    if (! name)
      return NULL;

  } else {
    Py_INCREF(name);
  }

  // name is ref'd

  KosInternedAtom *atom = (KosInternedAtom *) PyDict_GetItem(intern, name);
  if (! atom) {
    atom = PyObject_New(KosInternedAtom, type);
    if (! atom) {
      Py_DECREF(name);
      return NULL;
    }

    KosInternedAtom_NAME(atom) = name;

    if (PyDict_SetItem(intern, name, (PyObject *) atom)) {
      Py_DECREF(atom);  /* clears KosInternedAtom_NAME(atom) as well */
      return NULL;
    }

    // make the intern a borrowed ref. This allows refcount to drop to
    // zero, at which point the deallocation of the symbol will clear
    // it from the intern dict.
    Py_DECREF(atom);

  } else {
    Py_INCREF(atom);
    Py_DECREF(name);
  }

  return (PyObject *) atom;
}


static PyObject *atom_repr(PyObject *self) {
  return PyUnicode_FromFormat("<%s %R>",
                              self->ob_type->tp_name,
                              KosInternedAtom_NAME(self));
}


static PyObject *atom_str(PyObject *self) {
  PyObject *name = KosInternedAtom_NAME(self);
  Py_INCREF(name);
  return name;
}


static Py_ssize_t atom_len(PyObject *self) {
  PyObject *name = KosInternedAtom_NAME(self);
  return PyUnicode_GET_LENGTH(name);
}


static int atom_rewrap(PyObject *vals, unaryfunc conv) {
  PyObject *tmp = NULL;

  for (Py_ssize_t index = PyList_Size(vals); index--; ) {
    tmp = conv(PyList_GET_ITEM(vals, index));
    if (! tmp)
      return -1;

    if (PyList_SetItem(vals, index, tmp)) {
      Py_DECREF(tmp);
      return -1;
    }
  }
  return 0;
}


static PySequenceMethods atom_as_sequence = {
  .sq_length = atom_len,
};


/**
   The atom's bare spelling: itself for a symbol or keyword, so a
   reader wanting the spelling under any marks writes ``x.base`` with
   no type test
*/
static PyObject *atom_get_base(PyObject *self, void *closure) {
  (void) closure;
  Py_INCREF(self);
  return self;
}


static PyGetSetDef atom_getset[] = {
  { "base", atom_get_base, NULL,
    "The atom itself: its spelling carries no marks", NULL },
  { NULL }
};


/* === symbol === */


PyObject *KosSymbol_FromString(PyObject *name) {
  return atom_new(name, intern_syms, &KosSymbolType);
}


static void symbol_dealloc(PyObject *self) {
  if (intern_syms) {
    self->ob_refcnt = 3;
    PyDict_DelItem(intern_syms, KosInternedAtom_NAME(self));
  }

  Py_CLEAR(KosInternedAtom_NAME(self));
  Py_TYPE(self)->tp_free(self);
}


static PyObject *symbol_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "symbol takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "O:symbol", &name))
    return NULL;

  return KosSymbol_FromString(name);
}


static PyObject *symbol_split(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = KosInternedAtom_NAME(self);
  PyObject *method = NULL;
  PyObject *result = NULL;

  method = PyObject_GetAttr(name, _str_split);
  if (! method)
    return NULL;

  result = PyObject_Call(method, args, kwds);
  Py_CLEAR(method);
  if (! result)
    return NULL;

  if (atom_rewrap(result, KosSymbol_FromString)) {
    Py_DECREF(result);
    return NULL;
  }
  return result;
}


static PyObject *symbol_rsplit(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = KosInternedAtom_NAME(self);
  PyObject *method = NULL;
  PyObject *result = NULL;

  method = PyObject_GetAttr(name, _str_rsplit);
  if (! method)
    return NULL;

  result = PyObject_Call(method, args, kwds);
  Py_CLEAR(method);
  if (! result)
    return NULL;

  if (atom_rewrap(result, KosSymbol_FromString)) {
    Py_DECREF(result);
    return NULL;
  }
  return result;
}


static PyObject *symbol_kos_repr(PyObject *self, PyObject *Py_UNUSED(ignored)) {

  return PyUnicode_FromFormat("%S", KosInternedAtom_NAME(self));
}


static PyMethodDef symbol_methods[] = {
  { "__kos_repr__", (PyCFunction) symbol_kos_repr, METH_NOARGS,
    "S.__kos_repr__() -> str\n"
    "Koskari-style symbol text (the bare name)." },

  { "split", (PyCFunction) symbol_split, METH_VARARGS|METH_KEYWORDS,
    "S.split(sep=None, maxsplit=-1) -> list of symbols" },

  { "rsplit", (PyCFunction) symbol_rsplit, METH_VARARGS|METH_KEYWORDS,
    "S.rsplit(sep=None, maxsplit=-1) -> list of symbols" },

  { NULL, NULL, 0, NULL },
};


PyTypeObject KosSymbolType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "symbol",
  sizeof(KosInternedAtom),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_methods = symbol_methods,
  .tp_getset = atom_getset,
  .tp_new = symbol_new,
  .tp_dealloc = symbol_dealloc,

  .tp_as_sequence = &atom_as_sequence,
  .tp_repr = atom_repr,
  .tp_str = atom_str,
};


/* === gensym === */


static PyObject *_str_gensym = NULL;

static unsigned long long mark_counter = 0;


unsigned long long KosGensym_NextMark(void) {
  return mark_counter++;
}


/**
   Seed the mark counter once per process from four random bytes,
   plus one so the first mark is never zero.
*/
static int gensym_seed(void) {

  if (mark_counter)
    return 0;

  PyObject *os = PyImport_ImportModule("os");
  if (! os)
    return -1;

  PyObject *buf = PyObject_CallMethod(os, "urandom", "n", (Py_ssize_t) 4);
  Py_DECREF(os);
  if (! buf)
    return -1;

  if (! PyBytes_Check(buf) || PyBytes_GET_SIZE(buf) != 4) {
    Py_DECREF(buf);
    PyErr_SetString(PyExc_RuntimeError, "os.urandom returned a bad seed");
    return -1;
  }

  const unsigned char *b = (const unsigned char *) PyBytes_AS_STRING(buf);
  unsigned long long seed = ((unsigned long long) b[0]
                             | ((unsigned long long) b[1] << 8)
                             | ((unsigned long long) b[2] << 16)
                             | ((unsigned long long) b[3] << 24));
  Py_DECREF(buf);

  mark_counter = seed + 1;
  return 0;
}


/**
   Coerce marks to a non-empty tuple of ints: an int becomes a
   one-tuple, any other sequence is copied. New reference, or NULL
   with an exception set.
*/
static PyObject *gensym_coerce_marks(PyObject *marks) {

  if (PyLong_Check(marks))
    return PyTuple_Pack(1, marks);

  PyObject *tup = PySequence_Tuple(marks);
  if (! tup)
    return NULL;

  Py_ssize_t n = PyTuple_GET_SIZE(tup);
  if (! n) {
    Py_DECREF(tup);
    PyErr_SetString(PyExc_ValueError, "gensym requires at least one mark");
    return NULL;
  }

  for (Py_ssize_t i = 0; i < n; i++) {
    if (! PyLong_Check(PyTuple_GET_ITEM(tup, i))) {
      Py_DECREF(tup);
      PyErr_SetString(PyExc_TypeError, "gensym marks must be ints");
      return NULL;
    }
  }

  return tup;
}


/**
   The derived spelling of base under marks: the base name followed
   by "#" and the lowercase hex of each mark. New reference.
*/
static PyObject *gensym_build_name(PyObject *base, PyObject *marks) {

  PyObject *name = KosInternedAtom_NAME(base);
  Py_INCREF(name);

  Py_ssize_t n = PyTuple_GET_SIZE(marks);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *m = PyTuple_GET_ITEM(marks, i);
    unsigned long long v = PyLong_AsUnsignedLongLong(m);
    if (v == (unsigned long long) -1 && PyErr_Occurred()) {
      Py_DECREF(name);
      return NULL;
    }
    PyObject *next = PyUnicode_FromFormat("%U#%llx", name, v);
    Py_DECREF(name);
    if (! next)
      return NULL;
    name = next;
  }

  return name;
}


/**
   The gensym for base, an exact symbol, under marks, a coerced
   tuple: the interned instance when it exists with that base and
   those marks, else a new one interned under the derived spelling.
   When the spelling is held by any other atom, sets *taken and
   returns NULL with no exception. New reference otherwise, or NULL
   with an exception set.
*/
static PyObject *gensym_intern(
    PyObject *base,
    PyObject *marks,
    int *taken) {

  *taken = 0;

  PyObject *name = gensym_build_name(base, marks);
  if (! name)
    return NULL;

  PyObject *found = PyDict_GetItem(intern_syms, name);
  if (found) {
    Py_DECREF(name);
    if (KosGensym_Check(found) && KosGensym_BASE(found) == base) {
      int same = PyObject_RichCompareBool(
          KosGensym_MARKS(found), marks, Py_EQ);
      if (same < 0)
        return NULL;
      if (same) {
        Py_INCREF(found);
        return found;
      }
    }
    *taken = 1;
    return NULL;
  }

  KosGensym *g = PyObject_New(KosGensym, &KosGensymType);
  if (! g) {
    Py_DECREF(name);
    return NULL;
  }

  KosInternedAtom_NAME(g) = name;
  Py_INCREF(base);
  g->base = base;
  Py_INCREF(marks);
  g->marks = marks;

  if (PyDict_SetItem(intern_syms, name, (PyObject *) g)) {
    Py_DECREF(g);
    return NULL;
  }

  // the intern is a borrowed ref, as for every atom: dealloc removes
  // the entry when the last real reference drops
  Py_DECREF(g);
  return (PyObject *) g;
}


PyObject *KosGensym_New(PyObject *base, PyObject *marks) {

  if (! KosSymbol_CheckExact(base)) {
    PyErr_Format(PyExc_TypeError,
                 "gensym base must be a symbol, not %R", base);
    return NULL;
  }

  PyObject *tup = gensym_coerce_marks(marks);
  if (! tup)
    return NULL;

  int taken = 0;
  PyObject *g = gensym_intern(base, tup, &taken);
  if (taken) {
    PyObject *name = gensym_build_name(base, tup);
    if (name) {
      PyErr_Format(PyExc_ValueError,
                   "%R is already interned as another atom", name);
      Py_DECREF(name);
    }
  }

  Py_DECREF(tup);
  return g;
}


/**
   The base and the marks a fresh gensym extends, from name: a gensym,
   or a str or symbol whose interned atom is one, contributes its base
   and marks; any other spelling is the base under no marks. Both out
   parameters are new references.
*/
static int gensym_prefix(
    PyObject *name,
    PyObject **out_base,
    PyObject **out_marks) {

  PyObject *atom = KosSymbol_FromString(name ? name : _str_gensym);
  if (! atom)
    return -1;

  if (KosGensym_Check(atom)) {
    *out_base = KosGensym_BASE(atom);
    Py_INCREF(*out_base);
    *out_marks = KosGensym_MARKS(atom);
    Py_INCREF(*out_marks);
    Py_DECREF(atom);
    return 0;
  }

  *out_base = atom;
  *out_marks = PyTuple_New(0);
  if (! *out_marks) {
    Py_DECREF(atom);
    return -1;
  }
  return 0;
}


PyObject *KosGensym_FromString(
    PyObject *name,
    PyObject *predicate) {

  PyObject *base = NULL;
  PyObject *prefix = NULL;
  PyObject *result = NULL;

  if (gensym_prefix(name, &base, &prefix))
    return NULL;

  while (1) {
    PyObject *mark = PyLong_FromUnsignedLongLong(KosGensym_NextMark());
    if (! mark)
      break;

    PyObject *one = PyTuple_Pack(1, mark);
    Py_DECREF(mark);
    if (! one)
      break;

    PyObject *marks = PySequence_Concat(prefix, one);
    Py_DECREF(one);
    if (! marks)
      break;

    // a spelling some other atom holds is skipped: the next mark is
    // as good as this one
    int taken = 0;
    PyObject *cand = gensym_intern(base, marks, &taken);
    Py_DECREF(marks);
    if (taken)
      continue;
    if (! cand)
      break;

    if (! predicate) {
      result = cand;
      break;
    }

    // every arm that does not hand back the candidate releases it:
    // atoms are weakly interned, so holding one keeps its spelling
    // reserved for the life of the process
    PyObject *maybe = PyObject_CallFunctionObjArgs(predicate, cand, NULL);
    if (! maybe) {
      Py_DECREF(cand);
      break;
    }

    int accept = PyObject_IsTrue(maybe);
    Py_DECREF(maybe);
    if (accept < 0) {
      Py_DECREF(cand);
      break;
    }

    if (accept) {
      result = cand;
      break;
    }

    Py_DECREF(cand);
  }

  Py_DECREF(base);
  Py_DECREF(prefix);
  return result;
}


static void gensym_dealloc(PyObject *self) {
  Py_CLEAR(KosGensym_BASE(self));
  Py_CLEAR(KosGensym_MARKS(self));
  symbol_dealloc(self);
}


static PyObject *gensym_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *predicate = NULL;
  PyObject *marks = NULL;
  static char *kwlist[] = { "name", "predicate", "marks", NULL };

  (void) type;
  if (! PyArg_ParseTupleAndKeywords(
          args, kwds, "|OO$O:gensym", kwlist, &name, &predicate, &marks))
    return NULL;

  if (name == Py_None)
    name = NULL;
  if (predicate == Py_None)
    predicate = NULL;
  if (marks == Py_None)
    marks = NULL;

  if (! marks)
    return KosGensym_FromString(name, predicate);

  if (predicate) {
    PyErr_SetString(PyExc_TypeError,
                    "gensym takes no predicate with marks");
    return NULL;
  }

  if (! name) {
    PyErr_SetString(PyExc_TypeError,
                    "gensym with marks requires a base symbol");
    return NULL;
  }

  if (PyUnicode_Check(name)) {
    PyObject *base = KosSymbol_FromString(name);
    if (! base)
      return NULL;
    PyObject *g = KosGensym_New(base, marks);
    Py_DECREF(base);
    return g;
  }

  return KosGensym_New(name, marks);
}


static PyObject *gensym_get_base(PyObject *self, void *closure) {
  (void) closure;
  PyObject *b = KosGensym_BASE(self);
  Py_INCREF(b);
  return b;
}


static PyObject *gensym_get_marks(PyObject *self, void *closure) {
  (void) closure;
  PyObject *m = KosGensym_MARKS(self);
  Py_INCREF(m);
  return m;
}


static PyGetSetDef gensym_getset[] = {
  { "base", gensym_get_base, NULL,
    "The symbol the gensym spells, without its marks", NULL },
  { "marks", gensym_get_marks, NULL,
    "Tuple of the expansions that introduced it, outermost first",
    NULL },
  { NULL }
};


/**
   Source spelling: the base symbol. Printed source hides expansion
   provenance; str() carries it.
*/
static PyObject *gensym_kos_repr(PyObject *self, PyObject *unused) {
  (void) unused;
  return PyUnicode_FromFormat(
      "%S", KosInternedAtom_NAME(KosGensym_BASE(self)));
}


static PyObject *gensym_next_mark(PyObject *unused, PyObject *ignored) {
  (void) unused;
  (void) ignored;
  return PyLong_FromUnsignedLongLong(KosGensym_NextMark());
}


static PyMethodDef gensym_methods[] = {
  { "__kos_repr__", (PyCFunction) gensym_kos_repr, METH_NOARGS,
    "G.__kos_repr__() -> str\n"
    "Koskari-style symbol text: the base spelling, without marks." },

  { "next_mark", (PyCFunction) gensym_next_mark, METH_NOARGS|METH_STATIC,
    "gensym.next_mark() -> int\n"
    "The next expansion mark from the process counter." },

  { NULL, NULL, 0, NULL },
};


PyTypeObject KosGensymType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "gensym",
  sizeof(KosGensym),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_base = &KosSymbolType,
  .tp_doc = "A symbol scoped by expansion marks: gensym(name=None, "
            "predicate=None) mints a fresh mark, gensym(base, marks=...) "
            "spells base under the given marks",
  .tp_methods = gensym_methods,
  .tp_getset = gensym_getset,
  .tp_new = gensym_new,
  .tp_dealloc = gensym_dealloc,
};


/* === keyword === */


PyObject *KosKeyword_FromString(PyObject *name) {
  PyObject *clean;
  PyObject *result;

  if (! name) {
    PyErr_SetString(PyExc_TypeError, "keyword requires a name");
    return NULL;

  } else if (! PyUnicode_CheckExact(name)) {
    name = PyObject_Str(name);
    if (! name)
      return NULL;

  } else {
    Py_INCREF(name);
  }

  clean = PyObject_CallMethodObjArgs(name, _str_strip, _str_colon, NULL);
  if (! clean) {
    Py_DECREF(name);
    return NULL;
  }

  result = atom_new(clean, intern_kwds, &KosKeywordType);

  Py_DECREF(name);
  Py_DECREF(clean);

  return result;
}


static PyObject *keyword_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "keyword takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "O:keyword", &name))
    return NULL;

  return KosKeyword_FromString(name);
}


static void keyword_dealloc(PyObject *self) {
  if (intern_kwds) {
    self->ob_refcnt = 3;
    PyDict_DelItem(intern_kwds, KosInternedAtom_NAME(self));
  }

  Py_CLEAR(KosInternedAtom_NAME(self));
  Py_TYPE(self)->tp_free(self);
}


static PyObject *keyword_split(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = KosInternedAtom_NAME(self);
  PyObject *method = NULL;
  PyObject *result = NULL;

  method = PyObject_GetAttr(name, _str_split);
  if (! method)
    return NULL;

  result = PyObject_Call(method, args, kwds);
  Py_CLEAR(method);
  if (! result)
    return NULL;

  if (atom_rewrap(result, KosKeyword_FromString)) {
    Py_DECREF(result);
    return NULL;
  }
  return result;
}


static PyObject *keyword_rsplit(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = KosInternedAtom_NAME(self);
  PyObject *method = NULL;
  PyObject *result = NULL;

  method = PyObject_GetAttr(name, _str_rsplit);
  if (! method)
    return NULL;

  result = PyObject_Call(method, args, kwds);
  Py_CLEAR(method);
  if (! result)
    return NULL;

  if (atom_rewrap(result, KosKeyword_FromString)) {
    Py_DECREF(result);
    return NULL;
  }
  return result;
}


static PyObject *keyword_kos_repr(PyObject *self, PyObject *Py_UNUSED(ignored)) {

  PyObject *name = KosInternedAtom_NAME(self);

  if (! PyUnicode_GET_LENGTH(name))
    return PyUnicode_FromString("::");

  return PyUnicode_FromFormat("%S:", name);
}


static PyMethodDef keyword_methods[] = {
  { "__kos_repr__", (PyCFunction) keyword_kos_repr, METH_NOARGS,
    "K.__kos_repr__() -> str\n"
    "Koskari-style keyword text (name plus trailing colon)." },

  { "split", (PyCFunction) keyword_split, METH_VARARGS|METH_KEYWORDS,
    "K.split(sep=None, maxsplit=-1) -> list of keywords" },

  { "rsplit", (PyCFunction) keyword_rsplit, METH_VARARGS|METH_KEYWORDS,
    "K.rsplit(sep=None, maxsplit=-1) -> list of keywords" },

  { NULL, NULL, 0, NULL },
};


PyTypeObject KosKeywordType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "keyword",
  sizeof(KosInternedAtom),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_methods = keyword_methods,
  .tp_getset = atom_getset,
  .tp_new = keyword_new,
  .tp_dealloc = keyword_dealloc,

  .tp_as_sequence = &atom_as_sequence,
  .tp_repr = atom_repr,
  .tp_str = atom_str,
};


/* === unit lifecycle === */


int koskari_atom_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* initialize string constants */
  STR_CONST(_str_colon, ":");
  STR_CONST(_str_rsplit, "rsplit");
  STR_CONST(_str_split, "split");
  STR_CONST(_str_strip, "strip");

  /* create intern dictionaries */
  if (! intern_syms) {
    intern_syms = PyDict_New();
    if (! intern_syms)
      return -1;
  }

  if (! intern_kwds) {
    intern_kwds = PyDict_New();
    if (! intern_kwds)
      return -1;
  }

  /* initialize types */
  if (PyType_Ready(&KosSymbolType))
    return -1;

  if (PyType_Ready(&KosKeywordType))
    return -1;

  if (PyType_Ready(&KosGensymType))
    return -1;

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosSymbolType))
    return -1;

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosKeywordType))
    return -1;

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosGensymType))
    return -1;

  /* add types to module dict */
  if (PyDict_SetItemString(dict, "symbol", (PyObject *) &KosSymbolType))
    return -1;

  if (PyDict_SetItemString(dict, "keyword", (PyObject *) &KosKeywordType))
    return -1;

  if (PyDict_SetItemString(dict, "gensym", (PyObject *) &KosGensymType))
    return -1;

  /* seed the expansion mark counter */
  STR_CONST(_str_gensym, "<gensym>");
  if (gensym_seed())
    return -1;

  return 0;
}


/* Tear down an intern table. Its entries are uncounted, so letting
   the dict simply decref its values would steal a real reference from
   every atom still held elsewhere (a pair in a namespace torn down
   later, say) and free it out from under that holder. Detach the table
   first, so an atom dealloc during teardown does not reach back into
   it, then give the dict one counted reference per entry to drop. */
static void intern_release(PyObject **table) {
  PyObject *dict = *table;
  if (! dict)
    return;
  *table = NULL;

  PyObject *key = NULL;
  PyObject *val = NULL;
  Py_ssize_t pos = 0;
  while (PyDict_Next(dict, &pos, &key, &val))
    Py_INCREF(val);

  Py_DECREF(dict);
}


int koskari_atom_clear(PyObject *mod) {

  /* clear intern dictionaries */
  intern_release(&intern_syms);
  intern_release(&intern_kwds);

  /* clear string constants */
  Py_CLEAR(_str_colon);
  Py_CLEAR(_str_rsplit);
  Py_CLEAR(_str_split);
  Py_CLEAR(_str_strip);
  Py_CLEAR(_str_gensym);

  return 0;
}


// The end.
