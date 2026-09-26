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
  KOS_DEFER_DELEGATING = 0,
  KOS_DEFER_DONE,
  KOS_DEFER_ITERATING,
  KOS_DEFER_SPENT,
  KOS_DEFER_MOVED_FROM,
} KosDeferState;


/* === defer structure === */


typedef struct KosDefer {
  PyObject_HEAD
  KosDeferState state;
  union {
    PyObject *inner;
    PyObject *value;
    PyObject *iter;
  } u;
} KosDefer;


/* === type objects === */


extern PyTypeObject KosDeferType;


/* === type check macros === */


/* Defer is a final marker type (no Py_TPFLAGS_BASETYPE, so subclassing
   is impossible): the check is an exact type compare, no mro walk. */
#define KosDefer_Check(obj)                      \
  ((obj) && ((obj)->ob_type == &KosDeferType))

#define KosDefer_CheckExact(obj)                 \
  ((obj) && ((obj)->ob_type == &KosDeferType))


/* === public API === */


/**
  Create a new defer: invoke fn(*args, **kwds) immediately, normalize
  (including defer-of-defer adopt). On call failure, return NULL with
  active exception. args and kwds may be NULL (treated as empty).
  Returns new reference.

  The handle is single-shot: driving it to completion (await, send,
  throw, or close) transitions to KOS_DEFER_SPENT; later use raises
  RuntimeError like a finished coroutine. The type carries the full
  coroutine protocol (__await__ returning self, send, throw, close,
  __next__), so it passes collections.abc.Coroutine's structural hook
  and asyncio.iscoroutine.
*/
PyObject *KosDefer_New(PyObject *fn, PyObject *args, PyObject *kwds);


/**
   Take the fresh Koskari coroutine a delegating defer holds, spending
   the defer as a drive to completion would: a new reference to the
   coroutine, or NULL with no error set when the defer is not
   delegating to one that has not been started.
*/
PyObject *KosDefer_TakeFreshCoroutine(PyObject *defer);


/* === unit lifecycle === */


int koskari_defer_init(PyObject *mod);
int koskari_defer_clear(PyObject *mod);


#endif


// The end.
