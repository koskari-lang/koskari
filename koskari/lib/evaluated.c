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


/**
   koskari/lib/evaluated.c

   KosEvaluatedType: the abstract marker root for extended type objects
   carrying in-eval interface suites. The suites themselves are defined
   and dispatched by their own units (callable.h for the call suite);
   this unit is only the shared substrate.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === KosEvaluatedType === */


KosEvaluatedTypeObject KosEvaluatedType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib._KosEvaluated",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Internal base type for native types the evaluator"
              " dispatches to directly",
    .tp_new = NULL,
  },
  .kos_call = { 0 },
};


/* === Unit lifecycle === */


int koskari_evaluated_init(PyObject *mod) {
  (void) mod;
  return PyType_Ready(KOS_EVALUATED_PYTYPE(KosEvaluatedType));
}


int koskari_evaluated_clear(PyObject *mod) {
  (void) mod;
  return 0;
}


// The end.
