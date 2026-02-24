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


#ifndef KOSKARI_CONT_H
#define KOSKARI_CONT_H


#include <Python.h>
#include "callable.h"


/* === KosPyDelimitedContinuation === */


/**
   KosPyDelimitedContinuation wraps a KosContinuation captured by shift.

   Cannot be called from Python; application only via CALL inside koskari_eval.
*/

extern KosCallableTypeObject KosPyDelimitedContinuationType;


typedef struct {
  PyObject_HEAD
  KosContinuation *captured;
} KosPyDelimitedContinuation;


#define KosPyDelimitedContinuation_Check(obj)                           \
  ((obj) && PyType_IsSubtype((obj)->ob_type, KOS_CALLABLE_PYTYPE(KosPyDelimitedContinuationType)))

#define KosPyDelimitedContinuation_CAPTURED(pyc) \
  (((KosPyDelimitedContinuation *)(pyc))->captured)


PyObject *KosPyDelimitedContinuation_New(KosContinuation *captured);


/* === KosPyContinuation (undelimited, for future call/cc) === */


/**
   KosPyContinuation wraps an undelimited KosContinuation from call/cc.

   Python-callable: raises KoskariContinuationDead or KoskariContinuationApply.
*/

extern KosCallableTypeObject KosPyContinuationType;


typedef struct {
  PyObject_HEAD
  KosContinuation *captured;
} KosPyContinuation;


#define KosPyContinuation_Check(obj)                                    \
  ((obj) && PyType_IsSubtype((obj)->ob_type, KOS_CALLABLE_PYTYPE(KosPyContinuationType)))

#define KosPyContinuation_CheckExact(obj)               \
  ((obj) && ((obj)->ob_type == KOS_CALLABLE_PYTYPE(KosPyContinuationType)))

#define KosPyContinuation_CAPTURED(pyc) \
  (((KosPyContinuation *)(pyc))->captured)


PyObject *KosPyContinuation_New(KosContinuation *captured);


/* === Unit lifecycle === */


int koskari_cont_init(PyObject *mod);
int koskari_cont_clear(PyObject *mod);


#endif


// The end.
