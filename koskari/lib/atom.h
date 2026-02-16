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


#ifndef KOSKARI_ATOM_H
#define KOSKARI_ATOM_H


#include <Python.h>


/* === interned atom base structure === */


typedef struct KosInternedAtom {
  PyObject_HEAD

  PyObject *name;
} KosInternedAtom;


#define KosInternedAtom_NAME(o) (((KosInternedAtom *)(o))->name)


/* === type objects === */


extern PyTypeObject KosSymbolType;
extern PyTypeObject KosKeywordType;


/* === type check macros === */


#define KosSymbol_Check(obj)                                    \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosSymbolType))

#define KosSymbol_CheckExact(obj)               \
  ((obj) && ((obj)->ob_type == &KosSymbolType))

#define KosKeyword_Check(obj)                                   \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosKeywordType))

#define KosKeyword_CheckExact(obj)              \
  ((obj) && ((obj)->ob_type == &KosKeywordType))


/* === public API === */


PyObject *KosSymbol_FromString(PyObject *name);

PyObject *KosKeyword_FromString(PyObject *name);

PyObject *KosGensym_FromString(PyObject *name, PyObject *predicate);


/* === unit lifecycle === */


int koskari_atom_init(PyObject *mod);
int koskari_atom_clear(PyObject *mod);


#endif


// The end.
