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


/**
   Create a coroutine for a call of the KosFunction fn whose arguments
   are already bound into ns (stolen; released on failure). The
   coroutine holds fn, and its root frame, built at the first send, is
   tagged with fn so the frame is offered to fn's frame cache at
   completion. Yielding code returns a new AsyncGenerator instead.

   :return: new KosCoroutine, or NULL on error
*/
PyObject *KosCoroutine_NewFromFunctionCall(PyObject *fn, PyObject *ns);


/* === KosCoroutine === */


/**
   KosCoroutine wraps a Koskari call for Python's coroutine protocol.
   Unstarted, it holds the bound call and the dynamic snapshot of its
   creation; the drive context exists only once Python drives it. A
   Koskari await takes the program straight onto its own chain
   (KosCoroutine_TakeFresh) and never builds one.

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
  KosContext *ctx;          /* the drive context; NULL until the first
                               drive from Python builds it, and NULL
                               again once the coroutine is done */
  KosCoroState state;
  int in_send;              /* re-entrancy guard */

  /* The bound call an unstarted coroutine is: the program, its bound
     namespace, the dynamic snapshot (domain and parameterize cells)
     taken when the call was made, and the function that made the call
     (NULL for an Expression). All owned; all cleared once the frame
     and context are built or the program is taken onto another
     chain. */
  KosCode *code;
  PyObject *ns;
  PyObject *domain;
  PyObject *param_refs;
  PyObject *fn;
} KosCoroutine;


#define KosCoroutine_Check(obj)                                         \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosCoroutineType))

#define KosCoroutine_CheckExact(obj)                    \
  ((obj) && ((obj)->ob_type == &KosCoroutineType))


/**
   Take a fresh coroutine's program for installation on another chain:
   when coro has not been started, its bound code and namespace move to
   the caller in *code and *ns, its dynamic snapshot and function are
   dropped, and it is marked done, so a later send, throw, or close
   from Python raises as it does after any completion. Returns 1 when
   taken, 0 when the coroutine has already been started and keeps its
   own drive.
*/
int KosCoroutine_TakeFresh(PyObject *coro, KosCode **code, PyObject **ns);


/* === Unit lifecycle === */


int koskari_coro_init(PyObject *mod);
int koskari_coro_clear(PyObject *mod);


#endif


// The end.
