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


#ifndef KOSKARI_PAIR_H
#define KOSKARI_PAIR_H


#include <Python.h>


/* === pair structure === */


typedef struct KosPair {
  PyObject_HEAD

  PyObject *head;
  PyObject *tail;
  PyObject *position;
} KosPair;


/* === type objects === */


extern PyTypeObject KosPairType;
extern PyTypeObject KosPairIteratorType;
extern PyTypeObject KosPairFollowerType;
extern PyTypeObject KosNilType;


/* === nil singleton === */


extern KosPair _KosNil;

#define KosNil ((PyObject *) &_KosNil)

#define KosNil_Check(val) (((void *) val) == (void *) &_KosNil)


/* === type check macros === */


#define KosPair_Check(obj)                                      \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosPairType))

#define KosPair_CheckExact(obj)                 \
  ((obj) && ((obj)->ob_type == &KosPairType))


/* === accessor macros === */


#define KosPair_CAR(p) (((KosPair *) (p))->head)

#define KosPair_CDR(p) (((KosPair *) (p))->tail)

#define KosPair_SETCAR(p, v) {                  \
    Py_XDECREF(KosPair_CAR(p));                 \
    KosPair_CAR(p) = (v);                       \
    Py_XINCREF(KosPair_CAR(p));                 \
  }

#define KosPair_SETCDR(p, v) {                  \
    Py_XDECREF(KosPair_CDR(p));                 \
    KosPair_CDR(p) = (v);                       \
    Py_XINCREF(KosPair_CDR(p));                 \
  }

#define KosPair_POSITION(p) (((KosPair *)(p))->position)


/* === public API === */


PyObject *KosPair_New(PyObject *head, PyObject *tail);

PyObject *KosPair_Cons(PyObject *sequence);

/**
   Build a pair chain from a C array. ptr[0] is the first element,
   ptr[count-1] is the tail. Same logical order as KosPair_Cons.
   INCREFs each element when storing into a pair. count may be 0 (returns nil).
 */
PyObject *KosPair_ConsFromPtr(PyObject *const *ptr, int count);

/**
   Make the pair chain circular: set the tail-most link's cdr to the head.
   Returns 0 on success, -1 if pair is nil or not a pair.
 */
int KosPair_MakeRecursive(PyObject *pair);

/**
   Break the first link whose cdr equals pair by setting that cdr to nil.
   Returns 0 on success, -1 if pair is nil, not a pair, or no such link exists.
 */
int KosPair_BreakRecursive(PyObject *pair);

PyObject *KosPair_Unpack(PyObject *self);

long KosPair_IsProper(PyObject *self);

long KosPair_IsRecursive(PyObject *self);


/* === unit lifecycle === */


int koskari_pair_init(PyObject *mod);
int koskari_pair_clear(PyObject *mod);


#endif


// The end.
