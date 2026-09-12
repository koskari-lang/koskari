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


/* Every per-type freelist caps how many freed objects it keeps for
   reuse. A recycled object's memory stays mapped and is zeroed on
   return, so a read of a freed node yields quiet nonsense instead of
   the fault ASan or Valgrind would report. Building with
   -DKOS_NOFREELIST=1 (the KOS_NOFREELIST=1 environment variable through
   setup.py, tests.mk, and the container targets) sets every cap to zero
   so each free reaches the allocator and such reads become findable. */
#ifdef KOS_NOFREELIST
#define KOS_FREELIST_CAP(n) 0
#else
#define KOS_FREELIST_CAP(n) (n)
#endif


/* === Forward declarations ===
   All named struct types used across headers are typedef'd here, before
   any individual header is included, so that include order never matters.
   Individual headers define the struct bodies; they do not re-typedef. */

struct KosPyAsyncGen;       typedef struct KosPyAsyncGen       KosPyAsyncGen;
struct KosCode;             typedef struct KosCode             KosCode;
struct KosSegment;          typedef struct KosSegment          KosSegment;
struct KosContext;          typedef struct KosContext          KosContext;
struct KosDynNode;          typedef struct KosDynNode          KosDynNode;
struct KosContinuation;     typedef struct KosContinuation     KosContinuation;
struct KosXferGoal;         typedef struct KosXferGoal         KosXferGoal;
struct KosEvaluatorToken;   typedef struct KosEvaluatorToken   KosEvaluatorToken;
struct KosEvalState;        typedef struct KosEvalState        KosEvalState;
struct KosStackSteal;       typedef struct KosStackSteal       KosStackSteal;
struct KosOpResult;         typedef struct KosOpResult         KosOpResult;


#include "arena.h"
#include "atom.h"
#include "bytecode.h"
#include "evaluated.h"
#include "callable.h"
#include "compat.h"
#include "domain.h"
#include "exception.h"
#include "dynchain.h"
#include "eval.h"
#include "formals.h"
#include "func.h"
#include "demand.h"
#include "coro.h"
#include "cont.h"
#include "iter.h"
#include "iterate.h"
#include "transform.h"
#include "filter.h"
#include "batched.h"
#include "compress.h"
#include "dropwhile.h"
#include "islice.h"
#include "map.h"
#include "pairwise.h"
#include "starmap.h"
#include "zip.h"
#include "zip_longest.h"
#include "matcher.h"
#include "namespace.h"
#include "object.h"
#include "operator.h"
#include "pair.h"
#include "parse.h"
#include "ref.h"
#include "stack.h"
#include "utils.h"
#include "values.h"
#include "defer.h"
#include "async_loop.h"
#include "property.h"
#include "partial.h"


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
      (which) = PyUnicode_InternFromString(val); \
  }

static inline PyObject *kos_key_to_str(PyObject *key) {
  if (PyUnicode_Check(key)) {
    Py_INCREF(key);
    return key;
  }

  if (KosSymbol_Check(key)) {
    PyObject *name = KosInternedAtom_NAME(key);
    Py_INCREF(name);
    return name;
  }

  PyErr_Format(PyExc_TypeError, "expected str or symbol, not '%.200s'",
               key ? Py_TYPE(key)->tp_name : "NULL");
  return NULL;
}


#endif


// The end.
