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


#ifndef KOSKARI_CORO_H
#define KOSKARI_CORO_H


#include <Python.h>


/* === Run code as coroutine === */


/**
   Create a coroutine that runs the given Code with the given namespace.

   The coroutine is not started; the caller drives it via send(None) or
   await. code must be a KosCode; ns is the initial frame namespace
   (e.g. a KosNamespace or dict), reference is borrowed.

   :return: new KosCoroutine, or NULL on error
*/
PyObject *KosCoroutine_NewFromCode(PyObject *code, PyObject *ns);


/* === KosCoroutine === */


/**
   KosCoroutine wraps a KosContext for Python's coroutine protocol.

   States: KOS_CORO_NEW, KOS_CORO_RUNNING, KOS_CORO_WAITING_PY,
   KOS_CORO_YIELDED (suspended due to yield, awaiting send()),
   KOS_CORO_DONE.
*/

extern PyTypeObject KosCoroutineType;


typedef enum {
  KOS_CORO_NEW = 0,
  KOS_CORO_RUNNING,
  KOS_CORO_WAITING_PY,
  KOS_CORO_YIELDED,
  KOS_CORO_DONE,
} KosCoroState;


typedef struct {
  PyObject_HEAD
  KosContext *ctx;
  KosCoroState state;
  PyObject *pending_await;  /* yielded object to bubble outward */
  int in_send;              /* re-entrancy guard */
} KosCoroutine;


#define KosCoroutine_Check(obj)                                         \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosCoroutineType))

#define KosCoroutine_CheckExact(obj)                    \
  ((obj) && ((obj)->ob_type == &KosCoroutineType))


PyObject *KosCoroutine_New(KosContext *ctx);


/* === Unit lifecycle === */


int koskari_coro_init(PyObject *mod);
int koskari_coro_clear(PyObject *mod);


#endif


// The end.
