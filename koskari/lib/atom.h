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


/* === gensym structure === */


/**
   A symbol scoped by expansion marks. The inherited name is the
   derived spelling, base followed by one "#hex" group per mark, and
   the gensym is interned under it beside the plain symbols, so the
   symbol of that spelling is the gensym itself.
*/
typedef struct KosGensym {
  KosInternedAtom atom;

  PyObject *base;    /* the interned symbol it spells */
  PyObject *marks;   /* tuple of int, outermost expansion first */
} KosGensym;


#define KosGensym_BASE(o) (((KosGensym *)(o))->base)
#define KosGensym_MARKS(o) (((KosGensym *)(o))->marks)


/* === type objects === */


extern PyTypeObject KosSymbolType;
extern PyTypeObject KosKeywordType;
extern PyTypeObject KosGensymType;


/* === type check macros === */


#define KosSymbol_Check(obj)                                    \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosSymbolType))

#define KosSymbol_CheckExact(obj)               \
  ((obj) && ((obj)->ob_type == &KosSymbolType))

#define KosKeyword_Check(obj)                                   \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosKeywordType))

#define KosKeyword_CheckExact(obj)              \
  ((obj) && ((obj)->ob_type == &KosKeywordType))

/* gensym is not a base type, so the check is one pointer compare */
#define KosGensym_Check(obj)                                    \
  ((obj) && Py_IS_TYPE((obj), &KosGensymType))


/* === public API === */


PyObject *KosSymbol_FromString(PyObject *name);

PyObject *KosKeyword_FromString(PyObject *name);


/**
   The gensym spelling base under marks, a non-empty tuple of ints.
   The interned instance when one exists with that base and those
   marks. New reference, or NULL with an exception set: ValueError
   when the derived spelling is already interned as a different atom.
*/
PyObject *KosGensym_New(PyObject *base, PyObject *marks);


/**
   A gensym under a fresh mark, spelled from name: a str or symbol
   becomes the base, a gensym is extended by one more mark, and NULL
   spells "<gensym>". Candidates the predicate rejects are released
   and the mark advances until one is accepted. New reference, or
   NULL with an exception set.
*/
PyObject *KosGensym_FromString(PyObject *name, PyObject *predicate);


/**
   The next expansion mark. The counter is seeded once per process
   from a random 32-bit base, so the spellings one process mints are
   unlikely to repeat those a module compiled in another process
   carries; the chance is the birthday bound over 32 bits, not zero.
*/
unsigned long long KosGensym_NextMark(void);


/* === unit lifecycle === */


int koskari_atom_init(PyObject *mod);
int koskari_atom_clear(PyObject *mod);


#endif


// The end.
