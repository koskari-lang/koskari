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
   Interned atom types: symbol and keyword

   Symbols and keywords are interned string-like objects. Each unique
   name maps to exactly one instance in memory, allowing for fast
   identity comparisons.

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
  .tp_new = symbol_new,
  .tp_dealloc = symbol_dealloc,

  .tp_as_sequence = &atom_as_sequence,
  .tp_repr = atom_repr,
  .tp_str = atom_str,
};


/* === gensym === */


static Py_uhash_t gen_counter = 97531UL;


PyObject *KosGensym_FromString(
    PyObject *name,
    PyObject *predicate) {

  while (1) {

    // generate a name
    PyObject *maybe_name;
    if (name) {
      maybe_name = PyUnicode_FromFormat("%S#%x", name, gen_counter, NULL);
    } else {
      maybe_name = PyUnicode_FromFormat("<gensym>#%x", gen_counter, NULL);
    }
    if (! maybe_name)
      return NULL;

    // I don't know, some silliness to make the index number bounce
    // all over the damned place
    gen_counter += (Py_uhash_t) maybe_name;
    gen_counter *= _PyHASH_MULTIPLIER;
    if (! gen_counter)
      gen_counter = 97531UL;

    // check whether a symbol with that name is already allocated. If
    // it is, then this is not a unique symbol, try again.
    if (PyDict_GetItem(intern_syms, maybe_name)) {
      Py_CLEAR(maybe_name);
      continue;
    }

    // reserves the symbol for this gensym attempt
    PyObject *maybe_symbol = KosSymbol_FromString(maybe_name);
    Py_CLEAR(maybe_name);

    if (predicate) {
      PyObject *maybe = PyObject_CallFunctionObjArgs(predicate,
                                                     maybe_symbol, NULL);
      if (! maybe) {
        return NULL;

      } else if (PyObject_IsTrue(maybe)) {
        Py_DECREF(maybe);
        return maybe_symbol;

      } else {
        Py_DECREF(maybe);
        Py_CLEAR(maybe_symbol);
        continue;
      }

    } else {
      return maybe_symbol;
    }
  }
}


static PyObject *m_gensym(PyObject *mod, PyObject *args) {

  PyObject *name = NULL;
  PyObject *predicate = NULL;

  if (! PyArg_ParseTuple(args, "|OO:gensym", &name, &predicate))
    return NULL;

  if (predicate == Py_None)
    predicate = NULL;

  return KosGensym_FromString(name, predicate);
}


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
  .tp_new = keyword_new,
  .tp_dealloc = keyword_dealloc,

  .tp_as_sequence = &atom_as_sequence,
  .tp_repr = atom_repr,
  .tp_str = atom_str,
};


/* === module methods === */


static PyMethodDef atom_methods[] = {

  { "gensym", (PyCFunction) m_gensym, METH_VARARGS,
    "gensym(name=None, predicate=None) -> generate a unique symbol" },

  { NULL, NULL, 0, NULL },
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

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosSymbolType))
    return -1;

  if (KosUtils_ReprRegisterBuiltin((PyObject *) &KosKeywordType))
    return -1;

  /* add types to module dict */
  if (PyDict_SetItemString(dict, "symbol", (PyObject *) &KosSymbolType))
    return -1;

  if (PyDict_SetItemString(dict, "keyword", (PyObject *) &KosKeywordType))
    return -1;

  /* add module functions */
  if (PyModule_AddFunctions(mod, atom_methods))
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

  return 0;
}


// The end.
