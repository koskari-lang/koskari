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
extern PyTypeObject KosBracketPairType;
extern PyTypeObject KosBracketNilType;


/* === nil singleton === */


extern KosPair _KosNil;

#define KosNil ((PyObject *) &_KosNil)

#define KosNil_Check(val) (((void *) val) == (void *) &_KosNil)


extern KosPair _KosBracketNil;

#define KosBracketNil ((PyObject *) &_KosBracketNil)

#define KosBracketNil_Check(val)                \
  (((void *) val) == (void *) &_KosBracketNil)

/* True if val is either empty-list singleton — the canonical nil or
   the bracket-nil. This is the list-terminator / empty-list test; use
   it instead of KosNil_Check wherever "the end of a list" is meant. */
#define KosNilP(val) (KosNil_Check(val) || KosBracketNil_Check(val))


/* === type check macros === */


/* The pair family is closed: none of the four types is BASETYPE, so a
   type compare covers every pair. */
#define KosPair_Check(obj)                                      \
  ((obj) && (Py_TYPE(obj) == &KosPairType                       \
             || Py_TYPE(obj) == &KosBracketPairType             \
             || Py_TYPE(obj) == &KosNilType                     \
             || Py_TYPE(obj) == &KosBracketNilType))

#define KosPair_CheckExact(obj)                 \
  ((obj) && ((obj)->ob_type == &KosPairType))

#define KosBracketPair_Check(obj)                               \
  ((obj) && (Py_TYPE(obj) == &KosBracketPairType                \
             || Py_TYPE(obj) == &KosBracketNilType))

#define KosBracketPair_CheckExact(obj)                  \
  ((obj) && ((obj)->ob_type == &KosBracketPairType))

/* True for a spine cell to walk along a list: any pair (including a
   bracket-pair subclass) that is not an empty-list terminator (nil or
   bracket-nil). */
#define KosPair_IsCell(obj)                     \
  (KosPair_Check(obj) && ! KosNilP(obj))


/* === accessor macros === */


#define KosPair_CAR(p) (((KosPair *) (p))->head)

#define KosPair_CDR(p) (((KosPair *) (p))->tail)

/* Both stores take the new value and set the slot before the old
   value is released, so a finalizer the release runs sees the slot
   already holding the new value. */
#define KosPair_SETCAR(p, v) {                  \
    PyObject *_kos_new = (v);                   \
    Py_XINCREF(_kos_new);                       \
    Py_XSETREF(KosPair_CAR(p), _kos_new);       \
  }

#define KosPair_SETCDR(p, v) {                  \
    PyObject *_kos_new = (v);                   \
    Py_XINCREF(_kos_new);                       \
    Py_XSETREF(KosPair_CDR(p), _kos_new);       \
  }

#define KosPair_POSITION(p) (((KosPair *)(p))->position)


/* === public API === */


PyObject *KosPair_New(PyObject *head, PyObject *tail);

PyObject *KosBracketPair_New(PyObject *head, PyObject *tail);

PyObject *KosPair_Cons(PyObject *sequence);

/**
   Build a pair chain from a C array. ptr[0] is the first element,
   ptr[count-1] is the tail. Same logical order as KosPair_Cons.
   INCREFs each element when storing into a pair. count may be 0 (returns nil).
 */
PyObject *KosPair_ConsFromPtr(PyObject *const *ptr, Py_ssize_t count);

/**
   Bracket-pair variants of KosPair_Cons / KosPair_ConsFromPtr: identical
   construction but using bracket-pair cells, and an empty build (count 0)
   returns bracket-nil rather than nil. The last element is still the tail.
 */
PyObject *KosBracketPair_Cons(PyObject *sequence);

PyObject *KosBracketPair_ConsFromPtr(PyObject *const *ptr, Py_ssize_t count);

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
