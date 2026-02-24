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


#ifndef KOSKARI_LIB_H
#define KOSKARI_LIB_H


#include <Python.h>
#include <structmember.h>


/* === Forward declarations ===
   All named struct types used across headers are typedef'd here, before
   any individual header is included, so that include order never matters.
   Individual headers define the struct bodies; they do not re-typedef. */

struct KosCode;             typedef struct KosCode             KosCode;
struct KosStackBlock;       typedef struct KosStackBlock       KosStackBlock;
struct KosContext;          typedef struct KosContext          KosContext;
struct KosDynNode;          typedef struct KosDynNode          KosDynNode;
struct KosContinuation;     typedef struct KosContinuation     KosContinuation;
struct KosTransferPlan;     typedef struct KosTransferPlan     KosTransferPlan;
struct KosHookRunner;       typedef struct KosHookRunner       KosHookRunner;
struct KosEvaluatorToken;   typedef struct KosEvaluatorToken   KosEvaluatorToken;
struct KosEvent;


#include "atom.h"
#include "bytecode.h"
#include "callable.h"
#include "compat.h"
#include "domain.h"
#include "dynchain.h"
#include "eval.h"
#include "formals.h"
#include "func.h"
#include "coro.h"
#include "cont.h"
#include "iter.h"
#include "namespace.h"
#include "operator.h"
#include "pair.h"
#include "parse.h"
#include "ref.h"
#include "stack.h"
#include "utils.h"
#include "values.h"
#include "defer.h"


/* === unit initializers === */

/*
  As implementation units are added, their init and clear function
  declarations should be added here in their respective .h files.
  Each unit provides:

  int koskari_UNIT_init(PyObject *mod);
  int koskari_UNIT_clear(PyObject *mod);

  These are called from lib.c during module initialization and
  cleanup. The declarations come from including the unit's header.
  Units can get the module dict via PyModule_GetDict(mod).
*/

/** Call all unit clear functions and clear the module dict. Use before
    releasing the module (e.g. in test harness teardown). */
int koskari_lib_clear(PyObject *mod);


/* === macros === */


#define Py_EQUALS(left, right)                                          \
  (((left) == (right)) ||                                               \
   PyObject_RichCompareBool((left), (right), Py_EQ))


#define STR_CONST(which, val) {                 \
    if (! (which))                              \
      (which) = PyUnicode_FromString(val);      \
  }


#endif


// The end.
