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


#ifndef KOSKARI_DEFER_H
#define KOSKARI_DEFER_H


#include <Python.h>


/* === defer state === */


typedef enum {
  KOS_DEFER_PENDING = 0,
  KOS_DEFER_DELEGATING,
  KOS_DEFER_DONE,
  KOS_DEFER_FAILED,
  KOS_DEFER_MOVED_FROM,
} KosDeferState;


/* === defer structure === */


typedef struct KosDefer {
  PyObject_HEAD
  KosDeferState state;
  union {
    struct {
      PyObject *fn;
      PyObject *args;
      PyObject *kwds;
    } pending;
    PyObject *inner;
    PyObject *value;
    PyObject *exc;
  } u;
} KosDefer;


/* === type objects === */


extern PyTypeObject KosDeferType;


/* === type check macros === */


#define KosDefer_Check(obj)                                      \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosDeferType))

#define KosDefer_CheckExact(obj)                 \
  ((obj) && ((obj)->ob_type == &KosDeferType))


/* === public API === */


/**
   Create a new defer in PENDING state capturing fn, args, kwds.
   args and kwds may be NULL (treated as empty).
   Returns new reference.
*/
PyObject *KosDefer_New(PyObject *fn, PyObject *args, PyObject *kwds);

/**
   Single step: if PENDING invoke fn(*args,**kwds), normalize (including
   defer-of-defer adopt). If DONE return value (new ref). If DELEGATING
   set *suspended=1 and *yielded=inner, return NULL. If FAILED/MOVED_FROM
   set exception, return NULL.
*/
PyObject *KosDefer_Start(KosDefer *self, int *suspended, PyObject **yielded);

/**
   Transition DELEGATING to DONE(result). Only valid when state is
   KOS_DEFER_DELEGATING. Call on AWAIT resume when inner completed.
*/
void KosDefer_FeedResult(KosDefer *self, PyObject *result);


/* === unit lifecycle === */


int koskari_defer_init(PyObject *mod);
int koskari_defer_clear(PyObject *mod);


#endif


// The end.
