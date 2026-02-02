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
#include <frameobject.h>
#include <structmember.h>

#include "compat.h"
#include "atom.h"
#include "pair.h"
#include "ref.h"
#include "namespace.h"
#include "bytecode.h"
#include "formals.h"
#include "values.h"
#include "operator.h"
#include "stack.h"
#include "dynchain.h"
#include "utils.h"
#include "parse.h"
#include "eval.h"


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


/* === macros === */


#define Py_EQUALS(left, right)                                          \
  (((left) == (right)) ||                                               \
   PyObject_RichCompareBool((left), (right), Py_EQ))


#define STR_CONST(which, val) {                 \
    if (! (which))                              \
      (which) = PyUnicode_FromString(val);      \
  }


#ifndef VERBOSE_DEBUG
#define VERBOSE_DEBUG 0
#endif


#if VERBOSE_DEBUG
#define SAY(m)                                  \
  PySys_WriteStdout(m "\n")
#define SAYF(m, a...)                           \
  PySys_FormatStdout(m "\n", a);

#else
#define SAY(m) {}
#define SAYF(m, a...) {}

#endif


#endif


// The end.
