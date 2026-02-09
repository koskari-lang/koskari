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


#ifndef KOSKARI_FUNC_H
#define KOSKARI_FUNC_H


#include <Python.h>


/* === Expression === */


extern PyTypeObject KosExpressionType;


/**
   Create an Expression (code, ns). Used by Code.expression(parent_ns).

   Calling the returned Expression with () runs the code in the namespace
   and returns a Coroutine.

   :param code: KosCode instance
   :param ns: KosNamespace instance (parent/global namespace)
   :return: new Expression, or NULL on error
*/
PyObject *KosExpression_New(PyObject *code, PyObject *ns);


/* === Function === */


extern PyTypeObject KosFunctionType;


/**
   Create a Function from (code, formals, parent_ns). Used by
   Code.function(formals, parent_ns). Same assembly as sibilant-ceval
   ByteCode.function(formals, parent); calling the result returns a
   Coroutine.

   :param code: KosCode instance
   :param formals: KosFormals instance (resolved from FormalsSpec + defaults)
   :param parent_ns: KosNamespace instance (base namespace)
   :return: new Function, or NULL on error
*/
PyObject *KosFunction_NewFromFormals(PyObject *code,
                                     PyObject *formals,
                                     PyObject *parent_ns);


/* === Run code as coroutine === */


/**
   Create a coroutine that runs the given Code with the given namespace.

   The coroutine is not started; the caller drives it via send(None) or
   await. code must be a KosCode; ns is the initial frame namespace
   (e.g. a KosNamespace or dict), reference is borrowed.

   :return: new KosCoroutine, or NULL on error
*/
PyObject *kos_run_code_as_coroutine(PyObject *code, PyObject *ns);


/* === KosCoroutine === */


/**
   KosCoroutine wraps a KosContext for Python's coroutine protocol.

   States: KOS_CORO_NEW, KOS_CORO_RUNNING, KOS_CORO_WAITING_PY,
   KOS_CORO_DONE.
*/

extern PyTypeObject KosCoroutineType;


typedef enum {
  KOS_CORO_NEW = 0,
  KOS_CORO_RUNNING,
  KOS_CORO_WAITING_PY,
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


int koskari_func_init(PyObject *mod);
int koskari_func_clear(PyObject *mod);


#endif


// The end.
