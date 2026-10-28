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
   coroutine's root frame is built here and tagged with fn, so the
   frame is offered to fn's frame cache at completion. Yielding code
   returns a new AsyncGenerator instead.

   :return: new KosCoroutine, or NULL on error
*/
PyObject *KosCoroutine_NewFromFunctionCall(PyObject *fn, PyObject *ns);


/**
   Create a coroutine for a call of the KosFunction fn whose arguments
   are already bound into frame, a prepared root frame taken from fn's
   frame cache (stolen; freed on failure, which offers it back). The
   frame is tagged with fn. The caller has checked that fn's code does
   not yield.

   :return: new KosCoroutine, or NULL on error
*/
PyObject *KosCoroutine_NewFromFrame(PyObject *fn, KosDynFrame *frame);


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

  /* The bound call an unstarted coroutine is: its root frame (the
     program, the bound namespace, and the function that made the
     call as the frame's fn, NULL for an Expression) and the
     parameterize cells in force when the call was made. Both owned;
     both cleared once the context is built around the frame or the
     frame is taken onto another chain. */
  KosDynFrame *frame;
  PyObject *param_refs;
} KosCoroutine;


#define KosCoroutine_Check(obj)                                         \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosCoroutineType))

#define KosCoroutine_CheckExact(obj)                    \
  ((obj) && ((obj)->ob_type == &KosCoroutineType))


/**
   Take a fresh coroutine's root frame for installation on another
   chain: when coro has not been started, its prepared frame moves to
   the caller in *frame (owned, ready for kos_dyn_push_ready_frame, its
   fn tag kept so the frame returns to the function's cache at
   completion), its dynamic snapshot is dropped, and it is marked done,
   so a later send, throw, or close from Python raises as it does
   after any completion. Returns 1 when taken, 0 when the coroutine
   has already been started and keeps its own drive.
*/
int KosCoroutine_TakeFresh(PyObject *coro, KosDynFrame **frame);


/* === Unit lifecycle === */


int koskari_coro_init(PyObject *mod);
int koskari_coro_clear(PyObject *mod);


#endif


// The end.
